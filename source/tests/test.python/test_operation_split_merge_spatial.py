# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import json
import re

from pxr import UsdGeom

from .test_utils import Test_Operation, _get_context, _get_meshes

METHOD_GEOM_SUBSETS = 0
METHOD_MESH_PRIMS = 1

SPLIT_ON_VERTICES = 0
SPLIT_ON_SUBSETS = 1

# Spatial Mode values
SPATIAL_MODE_NONE = 0
SPATIAL_MODE_BOUNDING = 1
SPATIAL_MODE_VERTEX = 2

# MergePointOption values
MERGE_POINT_DEFAULT = 0  # Use pseudo root prim
MERGE_POINT_XFORM = 1  # Use the first xformable parent
MERGE_POINT_KINDASSEMBLY = 2  # Use the first parent of kind assembly
MERGE_POINT_KINDGROUP = 3  # Use the first parent of kind group
MERGE_POINT_KINDCOMPONENT = 4  # Use the first parent of kind component
MERGE_POINT_KINDMODEL = 5  # Use the first parent of kind model
MERGE_POINT_KINDSUBCOMPONENT = 6  # Use the first parent of kind subcomponent
MERGE_POINT_ROOTPRIM = 7  # Use root prims
MERGE_POINT_PARENTPRIM = 8  # Use parent prims
MERGE_POINT_ORIGINALPRIM = 9  # Original prim

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "splitOn": SPLIT_ON_VERTICES,
    "method": METHOD_MESH_PRIMS,
    "weld": False,
    "splitCollocatedPoints": False,
    "originalGeomOption": 2,  # Deactivate
}


def _get_active_meshes(stage):
    """Return all active meshes in the stage"""
    return [x for x in _get_meshes(stage) if x.GetPrim().IsActive()]


def _get_worldspace_points(prim, xformCache):
    """Returns points in worldspace"""
    # Get the points and local to world transform matrix.
    points = UsdGeom.PointBased(prim).GetPointsAttr().Get()
    if not points:
        return []
    matrix = xformCache.GetLocalToWorldTransform(prim)

    # Return the points with the matrix applied.
    return [matrix.Transform(x) for x in points]


class Test_Operation_Split_Merge_Spatial(Test_Operation):

    OPERATION = "splitMeshes"

    def _assert_prim_equals_expected(self, stage, xformCache, prim_path, expected_path=None):
        """Asserts that the prim at the given path is equal to the in-active expected prim in the stage"""
        # ensure the expected group is active
        stage.GetPrimAtPath("/expected").SetActive(True)
        # get prims
        if expected_path is None:
            expected_path = prim_path
        expected_prim = stage.GetPrimAtPath("/expected" + expected_path)
        self.assertTrue(expected_prim.IsValid(), f"Reference prim at path /expected{expected_path} is not valid")
        check_prim = stage.GetPrimAtPath(prim_path)
        self.assertTrue(check_prim.IsValid(), f"Check prim at path {prim_path} is not valid")
        # compare applied schemas
        expected_schemas = expected_prim.GetAppliedSchemas()
        check_schemas = check_prim.GetAppliedSchemas()
        self.assertEqual(
            len(check_schemas),
            len(expected_schemas),
            f"Check prim at path {prim_path} does not have the expected applied schemas",
        )
        for a, b in zip(check_schemas, expected_schemas):
            self.assertEqual(a, b, f"Check prim at path {prim_path} does not have the expected applied schemas")
        # get world space points
        expected_points = _get_worldspace_points(expected_prim, xformCache)
        check_points = _get_worldspace_points(check_prim, xformCache)
        # compare points
        for a, b in zip(expected_points, check_points):
            for i in range(3):
                self.assertAlmostEqual(a[i], b[i], places=4)
        # compare primvars
        expected_primvars_api = UsdGeom.PrimvarsAPI(expected_prim)
        check_primvars_api = UsdGeom.PrimvarsAPI(check_prim)
        for expected_primvar in expected_primvars_api.GetPrimvarsWithAuthoredValues():
            primvar_name = expected_primvar.GetName()
            check_primvar = check_primvars_api.GetPrimvar(primvar_name)
            # check the prim has the expected primvar
            self.assertTrue(
                check_primvar.GetAttr().IsValid(), f"Prim {prim_path} does not have expected primvar: {primvar_name}"
            )
            # compare interpolation
            self.assertEqual(
                check_primvar.GetInterpolation(),
                expected_primvar.GetInterpolation(),
                f"Primvar {primvar_name} on prim {prim_path} has different interpolation than expceted",
            )
            # compare values
            expected_values = expected_primvar.Get()
            check_values = check_primvar.Get()
            self.assertEqual(
                len(check_values),
                len(expected_values),
                f"Primvar {primvar_name} on prim {prim_path} has a different number of values than expected",
            )

            for a, b in zip(check_values, expected_values):
                # If types that don't support abs(), like GfVec3f, have small precision
                # differences then the abs() check inside assertAlmostEqual will throw
                # an exception. Convert to list and test the values individually
                list_a = None
                list_b = None
                try:
                    # convert iterable (vec3f etc) to a list
                    list_a = list(a)
                    list_b = list(b)
                except TypeError:
                    # or assume scalar and put it in a list
                    list_a = [a]
                    list_b = [b]

                self.assertEqual(len(list_a), len(list_b))
                for i in range(len(list_a)):
                    self.assertAlmostEqual(list_a[i], list_b[i], places=4)

            # compare indices if this primvar has them
            self.assertEqual(
                expected_primvar.IsIndexed(),
                check_primvar.IsIndexed(),
                f"Primvar {primvar_name} on prim {prim_path} does not match expected indexing state",
            )
            if expected_primvar.IsIndexed():
                expected_indices = expected_primvar.GetIndicesAttr().Get()
                check_indices = check_primvar.GetIndicesAttr().Get()
                self.assertEqual(
                    len(expected_indices),
                    len(check_indices),
                    f"Primvar {primvar_name} on prim {prim_path} has a different number of indices than expected",
                )
                for a, b in zip(expected_indices, check_indices):
                    self.assertEqual(
                        a,
                        b,
                        f"Primvar {primvar_name} on prim {prim_path} has indices that don't match expected indices",
                    )

    async def test_split_spatial_basic(self):
        """Basic splitting with spatial clustering test on a simple scene"""

        stage = self._open_stage("splitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Verify there is 1 original mesh
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 1)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 22)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/clustered")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_1")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_2")
        self._assert_prim_equals_expected(stage, xformCache, "/merged_part")
        self._assert_prim_equals_expected(stage, xformCache, "/merged_part_1")

    async def test_ignore_original_mesh(self):
        """Test that using ignore original geometry option leaves it as is"""

        stage = self._open_stage("splitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["originalGeomOption"] = 0
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify the original prim has been removed
        original_prim = stage.GetPrimAtPath("/merged")
        self.assertTrue(original_prim.IsValid())
        self.assertTrue(original_prim.IsActive())

    async def test_delete_original_mesh(self):
        """Test that using delete original geometry option removes the original prim"""

        stage = self._open_stage("splitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["originalGeomOption"] = 1
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify the original prim has been removed
        original_prim = stage.GetPrimAtPath("/merged")
        self.assertFalse(original_prim.IsValid())

    async def test_deactivate_original_mesh(self):
        """Test that using deactivate original geometry option deactivates the original prim"""

        stage = self._open_stage("splitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["originalGeomOption"] = 2
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify the original prim has been removed
        original_prim = stage.GetPrimAtPath("/merged")
        self.assertTrue(original_prim.IsValid())
        self.assertFalse(original_prim.IsActive())

    async def test_hide_original_mesh(self):
        """Test that using hide original geometry option hides the original prim"""

        stage = self._open_stage("splitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["originalGeomOption"] = 3
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify the original prim has been removed
        original_prim = stage.GetPrimAtPath("/merged")
        self.assertTrue(original_prim.IsValid())
        self.assertTrue(original_prim.IsActive())
        self.assertEqual(UsdGeom.Imageable(original_prim).GetVisibilityAttr().Get(), "invisible")

    async def test_split_spatial_xforms(self):
        """Basic splitting with spatial clustering test on meshes with xforms"""

        stage = self._open_stage("xformsSplitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 2.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 15)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/clustered")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_1")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_2")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_3")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_4")

    async def test_split_spatial_primvars(self):
        """Splitting with spatial clustering test on a scene with primvars using different interpolation"""

        stage = self._open_stage("primvarSplitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 10.0
        split_args["spatialMaxSize"] = 1000.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 18)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/clustered")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_1")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_2")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_3")

    async def test_split_spatial_normals(self):
        """Splitting with spatial clustering test on a scene with primvar:normals using different interpolation that are
        expected to be written as flattened normals"""

        stage = self._open_stage("normalsSplitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 300.0
        split_args["spatialMaxSize"] = 3000.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 3)

        # compare against the expect prim
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/clustered")

    async def test_split_spatial_inhertied_primvars(self):
        """Splitting with spatial clustering test on a scene with primvars using inheritance"""
        red = (1.0, 0.0, 0.0)
        green = (0.0, 1.0, 0.0)
        blue = (0.0, 0.0, 1.0)

        stage = self._open_stage("primvarInheritanceSplitSpatial.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 10.0
        split_args["spatialMaxSize"] = 1000.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 4)

        # there should now be a single clustered prim with red, green, and blue display colors
        prim = stage.GetPrimAtPath("/clustered")
        primvar = UsdGeom.PrimvarsAPI(prim).GetPrimvar(UsdGeom.Tokens.primvarsDisplayColor)
        self.assertEqual(primvar.GetInterpolation(), UsdGeom.Tokens.uniform)
        self.assertEqual(primvar.ComputeFlattened(), [blue, red, red, blue, green, green])

    async def test_split_spatial_merge_subsets(self):
        """Splitting a prim with multiple geom subsets and merging with a single material prims"""
        stage = self._open_stage("groupingSceneWithSubsets.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["considerMaterials"] = True
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 13)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/clustered")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_1")

    async def test_split_spatial_merge_materials_separate(self):
        """Splitting a prim with multiple geom subsets and merging with other prims to a single prim with with multiple
        materials"""
        stage = self._open_stage("groupingSceneWithSubsets.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["considerMaterials"] = False
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 12)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/clustered", "/clusteredWithSubsets")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered/lambert2", "/clusteredWithSubsets/lambert2")

    async def test_split_spatial_merge_point(self):
        """Test that the merge point options are correctly handled"""

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["weld"] = True
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["considerMaterials"] = False
        split_args["spatialThreshold"] = 1000.0
        split_args["spatialMaxSize"] = 10000.0

        # paths, 'mergePoint', expected_path
        test_matrix = [
            # Strict kind hierarchy
            ("/World", MERGE_POINT_DEFAULT, ""),
            ("/World", MERGE_POINT_XFORM, "/World/Assembly/Group/Component/Subcomponent/Leaf"),
            ("/World", MERGE_POINT_KINDASSEMBLY, "/World/Assembly"),
            ("/World", MERGE_POINT_KINDGROUP, "/World/Assembly/Group"),
            ("/World", MERGE_POINT_KINDCOMPONENT, "/World/Assembly/Group/Component"),
            ("/World", MERGE_POINT_KINDSUBCOMPONENT, "/World/Assembly/Group/Component/Subcomponent"),
            ("/World", MERGE_POINT_ROOTPRIM, "/World"),
            ("/World", MERGE_POINT_PARENTPRIM, "/World/Assembly/Group/Component/Subcomponent/Leaf/Parent"),
            # Relaxed kind hierarchy
            ("/World_kindNone", MERGE_POINT_DEFAULT, ""),
            ("/World_kindNone", MERGE_POINT_XFORM, "/World_kindNone/Assembly/Group/Component/Subcomponent/Leaf"),
            ("/World_kindNone", MERGE_POINT_KINDASSEMBLY, "/World_kindNone/Assembly"),
            ("/World_kindNone", MERGE_POINT_KINDGROUP, "/World_kindNone/Assembly/Group"),
            ("/World_kindNone", MERGE_POINT_KINDCOMPONENT, "/World_kindNone/Assembly/Group/Component"),
            ("/World_kindNone", MERGE_POINT_KINDSUBCOMPONENT, "/World_kindNone/Assembly/Group/Component/Subcomponent"),
            ("/World_kindNone", MERGE_POINT_ROOTPRIM, "/World_kindNone"),
            (
                "/World_kindNone",
                MERGE_POINT_PARENTPRIM,
                "/World_kindNone/Assembly/Group/Component/Subcomponent/Leaf/Parent",
            ),
            # Sparse kind hierarchy
            ("/World_fallback", MERGE_POINT_DEFAULT, ""),
            ("/World_fallback", MERGE_POINT_XFORM, "/World_fallback/Assembly/Group/Component/Subcomponent/Leaf"),
            ("/World_fallback", MERGE_POINT_KINDASSEMBLY, ""),
            ("/World_fallback", MERGE_POINT_KINDGROUP, "/World_fallback/Assembly/Group"),
            ("/World_fallback", MERGE_POINT_KINDCOMPONENT, "/World_fallback/Assembly/Group"),
            ("/World_fallback", MERGE_POINT_KINDSUBCOMPONENT, "/World_fallback/Assembly/Group"),
            ("/World_fallback", MERGE_POINT_ROOTPRIM, "/World_fallback"),
            (
                "/World_fallback",
                MERGE_POINT_PARENTPRIM,
                "/World_fallback/Assembly/Group/Component/Subcomponent/Leaf/Parent",
            ),
        ]
        msg_pattern = (
            'Split and Spatially Cluster geometry for "{}" with merge point "{}" does not produce expected prim "{}"'
        )

        # Iterate over all the cases in the test matrix asserting that they generate the expected result
        for primPath, mergePoint, parent_path in test_matrix:

            # Modify the arguments for this test case
            split_args["paths"] = [primPath]
            split_args["mergePoint"] = mergePoint

            # Build the expected merged mesh path and assertion message for failures
            expected_path = parent_path + "/clustered"
            msg = msg_pattern.format(primPath, mergePoint, expected_path)

            # Open a fresh copy of the stage.
            stage = self._open_stage("mergePointSplitSpatial.usda")

            # Assert that the expected prim does not already exist.
            self.assertFalse(stage.GetPrimAtPath(expected_path).IsValid(), msg)

            # execute the operation via commands
            self._execute_command(split_args)

            # Assert that the expected prim does exist.
            self.assertTrue(stage.GetPrimAtPath(expected_path).IsValid(), msg)

    async def test_split_spatial_strict_attribute_mode(self):
        """Splitting with spatial clustering with Strict Attribute Mode on"""

        stage = self._open_stage("splitSpatialStrictAttributeMode.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["considerAllAttributes"] = True
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 30)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/clustered")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_1")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered_2")

    async def test_split_spatial_merge_point_original_prim(self):
        """Splitting and clustering only on split boundaries"""
        stage = self._open_stage("splitSpatialMergePointOriginalPrim.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["considerMaterials"] = False
        split_args["mergePoint"] = MERGE_POINT_ORIGINALPRIM
        split_args["spatialThreshold"] = 20.0
        split_args["spatialMaxSize"] = 500.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 14)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/merged_1_clustered")
        self._assert_prim_equals_expected(stage, xformCache, "/merged_2_clustered")
        self._assert_prim_equals_expected(stage, xformCache, "/World/group3/group3a/pPlane3")
        self._assert_prim_equals_expected(stage, xformCache, "/World/group4/group4b/group4c/merged_clustered")

    async def test_split_hole_indices(self):
        """
        Splitting while ensuring we retain correct hole indices
        """
        stage = self._open_stage("splitHoleIndices.usda")

        split_args = DEFAULT_ARGS.copy()

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 4)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/World/mesh1_part")
        self._assert_prim_equals_expected(stage, xformCache, "/World/mesh1_part_1")

    async def test_split_subdiv(self):
        """
        Splitting while ensuring we retain correct subdivision surface data
        """
        stage = self._open_stage("splitSubdiv.usda")

        split_args = DEFAULT_ARGS.copy()

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 5)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/merged_part")
        self._assert_prim_equals_expected(stage, xformCache, "/merged_part_1")
        self._assert_prim_equals_expected(stage, xformCache, "/merged_part_2")
        self._assert_prim_equals_expected(stage, xformCache, "/merged_part_3")

    async def test_split_spatial_invisible(self):
        """Splitting with spatial clustering on a scene with an invisible prim to ensure the prim isn't clustered"""
        stage = self._open_stage("splitSpatialInvisible.usda")

        # Run split with spatial settings enabled
        split_args = DEFAULT_ARGS.copy()
        split_args["spatialMode"] = SPATIAL_MODE_BOUNDING
        split_args["spatialThreshold"] = 10000.0
        split_args["spatialMaxSize"] = 10000.0

        # Custom execution context
        context = _get_context(stage, report=True)

        # Run the operation
        self._execute_command(split_args, context)

        # Verify expected actual mesh count after spatial merge
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 13)

        # compare some of the prims from the resulting stage with the expected prims
        xformCache = UsdGeom.XformCache()
        self._assert_prim_equals_expected(stage, xformCache, "/World/Xform/meshA_part")
        self._assert_prim_equals_expected(stage, xformCache, "/World/Xform/meshA_part_1")
        self._assert_prim_equals_expected(stage, xformCache, "/clustered")

    async def test_spatial_multi_cluster(self):
        """Test split + spatial clustering with multiCluster arguments"""

        stage = self._open_stage("analysis/multiCluster.usda")

        # Assert initial state
        meshCount = len(_get_active_meshes(stage))
        self.assertEqual(meshCount, 4)

        primClose = stage.GetPrimAtPath("/Close")
        self.assertTrue(primClose.IsActive())
        self.assertEqual(len(primClose.GetAttribute("faceVertexIndices").Get()), 12672)

        primSpaced = stage.GetPrimAtPath("/Spaced")
        self.assertTrue(primSpaced.IsActive())
        self.assertEqual(len(primSpaced.GetAttribute("faceVertexIndices").Get()), 288)

        primDistant = stage.GetPrimAtPath("/Distant")
        self.assertTrue(primDistant.IsActive())
        self.assertEqual(len(primDistant.GetAttribute("faceVertexIndices").Get()), 3968)

        primIgnored = stage.GetPrimAtPath("/Ignored")
        self.assertTrue(primIgnored.IsActive())
        self.assertEqual(len(primIgnored.GetAttribute("faceVertexIndices").Get()), 48)

        # Run multiCluster
        args = DEFAULT_ARGS.copy()
        multiArg = [
            {
                "paths": ["/Close"],
                "spatialMode": SPATIAL_MODE_BOUNDING,
                "spatialThreshold": 50.0,
                "spatialMaxSize": 1000.0,
                "spatialVertexCount": 0,
            },
            {
                "paths": ["/Spaced"],
                "spatialMode": SPATIAL_MODE_BOUNDING,
                "spatialThreshold": 1000.0,
                "spatialMaxSize": 5000.0,
                "spatialVertexCount": 0,
            },
            {
                "paths": ["/Distant"],
                "spatialMode": 0,
            },
        ]
        args["multiCluster"] = json.dumps(multiArg)
        args["mergePoint"] = MERGE_POINT_ORIGINALPRIM

        # Run command
        result, success = self._execute_command(args)
        self.assertTrue(success[0])

        self.assertFalse(primClose.IsActive())
        self.assertFalse(primSpaced.IsActive())
        self.assertFalse(primDistant.IsActive())

        meshes = _get_active_meshes(stage)

        # Assert new mesh count
        meshCount = len(meshes)
        self.assertEqual(meshCount, 9)

        closeMeshes = [m for m in meshes if m.GetName().startswith("Close")]
        spacedMeshes = [m for m in meshes if m.GetName().startswith("Spaced")]
        distantMeshes = [m for m in meshes if m.GetName().startswith("Distant")]

        # Expected number of new individual meshes, and then a quick check that
        # the vertex count looks good
        self.assertEqual(len(closeMeshes), 4)
        close0 = len(closeMeshes[0].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(close0, 2304)

        close1 = len(closeMeshes[1].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(close1, 3456)

        close2 = len(closeMeshes[2].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(close2, 3456)

        close3 = len(closeMeshes[3].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(close3, 3456)

        # Spaced out, two chunks
        self.assertEqual(len(spacedMeshes), 2)
        spaced0 = len(spacedMeshes[0].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(spaced0, 144)

        spaced1 = len(spacedMeshes[1].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(spaced1, 144)

        # These just get split
        self.assertEqual(len(distantMeshes), 2)
        distant0 = len(distantMeshes[0].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(distant0, 1984)

        distant1 = len(distantMeshes[1].GetAttribute("faceVertexIndices").Get())
        self.assertEqual(distant1, 1984)

        # Ignored should remain as is
        self.assertTrue(primIgnored.IsActive())
        self.assertEqual(len(primIgnored.GetAttribute("faceVertexIndices").Get()), 48)

    async def test_spatial_multi_cluster_invalid(self):
        """Test invalid multiCluster argument"""

        stage = self._open_stage("analysis/multiCluster.usda")

        # Initial state
        meshCount = len(_get_active_meshes(stage))
        self.assertEqual(meshCount, 4)

        args = DEFAULT_ARGS.copy()
        args["multiCluster"] = "{invalid json}"

        # Command should fail - but not crash
        success, result = self._execute_command(args)
        self.assertFalse(result[0])

        # Mesh count remains the same
        meshCount = len(_get_active_meshes(stage))
        self.assertEqual(meshCount, 4)

    async def test_spatial_multi_cluster_sparse_args(self):
        """Test multiCluster config with sparse JSON"""

        stage = self._open_stage("analysis/multiCluster.usda")

        # Initial state
        meshCount = len(_get_active_meshes(stage))
        self.assertEqual(meshCount, 4)

        # Minimal args - just the mode (to split) and no cluster args.
        multiArg = [
            {
                "paths": ["/Close"],
                "spatialMode": SPATIAL_MODE_NONE,
            },
            {
                "paths": ["/Spaced"],
                "spatialMode": SPATIAL_MODE_NONE,
            },
            {
                "paths": ["/Distant"],
                "spatialMode": SPATIAL_MODE_NONE,
            },
        ]

        args = DEFAULT_ARGS.copy()
        args["multiCluster"] = json.dumps(multiArg)

        # Run command
        result, success = self._execute_command(args)
        self.assertTrue(success[0])

        meshCount = len(_get_active_meshes(stage))
        self.assertEqual(meshCount, 26)
