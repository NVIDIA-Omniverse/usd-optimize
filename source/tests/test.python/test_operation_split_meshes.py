# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core import SceneOptimizerCore
from omni.scene.optimizer.core.scripts import standalone
from pxr import Sdf, Usd, UsdGeom, UsdShade

from .test_utils import Test_Operation, _get_context

METHOD_GEOM_SUBSETS = 0
METHOD_MESH_PRIMS = 1

SPLIT_ON_VERTICES = 0
SPLIT_ON_SUBSETS = 1

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "splitOn": SPLIT_ON_VERTICES,
    "method": METHOD_MESH_PRIMS,
    "splitCollocatedPoints": True,
    "originalGeomOption": 2,  # Deactivate
}


def _get_colors(prim):
    """Get flattened displayColor values from a prim"""
    papi = UsdGeom.PrimvarsAPI(prim)
    return papi.GetPrimvar("displayColor").ComputeFlattened()


def _is_bound_material(prim, material_path):
    """Returns true if the prim exists and is bound to a material with the given path"""
    if prim:
        materialBindingAPI = UsdShade.MaterialBindingAPI(prim)
        material, _ = materialBindingAPI.ComputeBoundMaterial()

        if material and material.GetPath() == material_path:
            return True

    # Doesn't return False unless there's a problem/regression.
    return False  # pragma: no cover


class Test_Operation_Split_Meshes(Test_Operation):

    OPERATION = "splitMeshes"

    async def test_one_part_split(self):
        """A Mesh that only has a single part should not be modified by split meshes"""
        # This case covers a mesh made up of 1 disjoint part being split into unique mesh prims.

        # Path of the input prim.
        path = "/world/onePart/mesh"

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["paths"] = [path]

        # Open the stage and execute the operation with "Mesh Prims" as the "Method".
        args["method"] = METHOD_MESH_PRIMS
        stage = self._open_stage("splitMeshes_various.usda")

        print("Stats Before:")
        context = _get_context(stage)

        success, _, _ = SceneOptimizerCore.getInstance().executeOperation("printStats", context, {})
        self.assertTrue(success)

        self._execute_command(args)

        # The input prim should still exist on the stage and be active.
        prim = stage.GetPrimAtPath(path)
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())

    async def test_two_part_split(self):
        """Test splitting"""
        # This case covers a mesh made up of 2 disjoint cubes being split;
        # 1. into unique mesh prims
        # 2. into geom subsets

        # Path of the input prim.
        path = "/world/twoPart/mesh"

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["paths"] = [path]

        # Open the stage and execute the operation with "Mesh Prims" as the "Method".
        args["method"] = METHOD_MESH_PRIMS
        stage = self._open_stage("splitMeshes_various.usda")
        self._execute_command(args)

        # The input prim should still exist on the stage but should be inactive.
        prim = stage.GetPrimAtPath(path)
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())

        # There should be 3 new Mesh prims each with 6 faces
        new_prim_paths = [
            "/world/twoPart/mesh_part",
            "/world/twoPart/mesh_part_1",
        ]
        for new_prim_path in new_prim_paths:

            # The new prim should be valid and active.
            prim = stage.GetPrimAtPath(new_prim_path)
            self.assertTrue(prim.IsValid())
            self.assertTrue(prim.IsActive())

            # The new prim should be a Mesh and have 6 faces.
            mesh = UsdGeom.Mesh(prim)
            self.assertTrue(mesh)
            faces = mesh.GetFaceVertexCountsAttr().Get()
            self.assertEqual(len(faces), 6)

    async def test_unwelded_part(self):
        """A Mesh with unwelded vertices can be split in different ways based on the split collocated points argument"""
        # This case covers a mesh made up of 2 cube parts where the cube corners are not welded.

        # Path of the input prim.
        path = "/world/unwelded"
        meshPath = "/world/unwelded/mesh"

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["paths"] = [path]
        args["method"] = METHOD_MESH_PRIMS

        # Open the stage and execute the operation with "splitCollocatedPoints" on.
        args["splitCollocatedPoints"] = True

        stage = self._open_stage("splitMeshes_various.usda")

        # There should be 48 points in the original mesh.
        meshPrim = stage.GetPrimAtPath(meshPath)
        points = list(meshPrim.GetAttribute("points").Get())
        self.assertEqual(len(points), 48)

        # Run split
        self._execute_command(args)

        # There should be 12 parts if "splitCollocatedPoints" is on.
        prim = stage.GetPrimAtPath(path)
        meshes = [x for x in Usd.PrimRange(prim) if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 12)

        # Open the stage and execute the operation with "splitCollocatedPoints" off.
        args["splitCollocatedPoints"] = False
        stage = self._open_stage("splitMeshes_various.usda")
        self._execute_command(args)

        # There should be 2 parts if "splitCollocatedPoints" is off.
        prim = stage.GetPrimAtPath(path)
        meshes = [x for x in Usd.PrimRange(prim) if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 2)

        # There should still be 48 points, welding is only for the purpose of
        # splitting and does not modify the topology.
        totalPoints = 0
        for mesh in meshes:
            totalPoints += len(list(mesh.GetAttribute("points").Get()))

        self.assertEqual(totalPoints, 48)

    async def test_unweldable_part(self):
        """A Mesh with unwelded vertices that do not perfectly align cannot be welded"""
        # This case covers a mesh made up of 2 cube parts where the cube corners are not welded and are slightly offset.

        # Path of the input prim.
        path = "/world/unweldable"

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["paths"] = [path]
        args["method"] = METHOD_MESH_PRIMS

        # Open the stage and execute the operation with "splitCollocatedPoints" off.
        args["splitCollocatedPoints"] = False
        stage = self._open_stage("splitMeshes_various.usda")
        self._execute_command(args)

        # There should be 12 parts even if "weld" is on.
        prim = stage.GetPrimAtPath(path)
        meshes = [x for x in Usd.PrimRange(prim) if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 12)

    async def test_unique_names(self):
        """Test that unique names are always used for new prims"""
        # This case covers a mesh made up of 2 disjoint cubes being split, but the prim names that are used by default
        # are already in use by and active and an inactive prim.

        # Path of the input prim.
        path = "/world/twoPart/mesh"

        # Open the stage and create prims with the names that would be used.
        stage = self._open_stage("splitMeshes_various.usda")
        stage.DefinePrim("/world/twoPart/mesh_part").SetActive(True)
        stage.DefinePrim("/world/twoPart/mesh_part_1").SetActive(False)

        # Execute split operation
        args = DEFAULT_ARGS.copy()
        args["paths"] = [path]
        args["method"] = METHOD_MESH_PRIMS
        self._execute_command(args)

        # There should be 2 new Mesh prims with the next available names
        new_prim_paths = [
            "/world/twoPart/mesh_part_2",
            "/world/twoPart/mesh_part_3",
        ]
        for new_prim_path in new_prim_paths:

            # The new prim should be valid and active.
            prim = stage.GetPrimAtPath(new_prim_path)
            self.assertTrue(prim.IsValid())
            self.assertTrue(prim.IsActive())

    async def test_maintain_specifier(self):
        """Test the specifier of parent prims are maintained when parts are created"""
        # This case covers a mesh made up of 2 disjoint cubes being split, but the parent prim has an "over" specifier
        # and that needs to be maintained. This is important for cases where "over" or "class" specifiers are used to
        # mask the prototypes of geometry libraries from the renderer.

        # Path of the input prim.
        path = "/world/twoPart/mesh"

        # Open the stage and change the specifier of the parent
        stage = self._open_stage("splitMeshes_various.usda")
        stage.GetPrimAtPath("/world").SetSpecifier(Sdf.SpecifierOver)

        # Execute split operation
        args = DEFAULT_ARGS.copy()
        args["paths"] = [path]
        args["method"] = METHOD_MESH_PRIMS
        self._execute_command(args)

        # There should be 2 new Mesh prims
        new_prim_paths = [
            "/world/twoPart/mesh_part",
            "/world/twoPart/mesh_part_1",
        ]
        for new_prim_path in new_prim_paths:

            # The new prim should be valid and active ...
            prim = stage.GetPrimAtPath(new_prim_path)
            self.assertTrue(prim.IsValid())
            self.assertTrue(prim.IsActive())
            # but not defined as the parent should still have the "over" specifier.
            self.assertFalse(prim.IsDefined())
            self.assertEqual(prim.GetSpecifier(), Sdf.SpecifierDef)

    async def test_mesh_with_xform(self):
        """Test that splitting meshes respects the local xform"""

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["method"] = METHOD_MESH_PRIMS

        # Open the stage and execute the operation with "Mesh Prims" as the "Method".
        stage = self._open_stage("splitMeshes_xform.usda")
        self._execute_command(args)

        # Validate expected number of split meshes
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 6)

        mesh0_part0 = stage.GetPrimAtPath("/World/mesh_0_part")
        self.assertTrue(mesh0_part0)
        mesh1_part0 = stage.GetPrimAtPath("/World/mesh_01_part")
        self.assertTrue(mesh1_part0)

        xformCache = UsdGeom.XformCache()
        mesh0_xform = xformCache.GetLocalToWorldTransform(mesh0_part0)
        mesh1_xform = xformCache.GetLocalToWorldTransform(mesh1_part0)

        # Test that duplicate meshes that have a different xform have been authored as such
        self.assertNotEqual(mesh0_xform, mesh1_xform)

    async def test_mesh_with_apischemas(self):
        """Test split meshes does the right thing with various API schemas"""

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["method"] = METHOD_MESH_PRIMS

        # Open the stage and execute the operation with "Mesh Prims" as the "Method".
        stage = self._open_stage("splitMeshes_skip.usda")
        self._execute_command(args)

        # Validate expected number of split meshes
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 3)

        # Get the original mesh that should be skipped and validate that it is still active
        skipped_mesh = stage.GetPrimAtPath("/OmniNavMeshViz")
        self.assertTrue(skipped_mesh.IsActive())

        # Test meshes with MaterialBindingAPI applied, which we can split no problem
        mesh0_part0 = stage.GetPrimAtPath("/World/mesh_part")
        self.assertTrue(mesh0_part0)
        mesh0_part1 = stage.GetPrimAtPath("/World/mesh_part_1")
        self.assertTrue(mesh0_part1)

    async def test_primvar_slicing(self):
        """Test splitting meshes containing primvars with various interpolations"""

        stage = self._open_stage("primvarSplit.usda")

        # Validate expected number of initial merged meshes
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 5)

        # Assert one of the original meshes, which contains a random array value
        constant_mesh = stage.GetPrimAtPath("/MergedConstant")
        self.assertTrue(constant_mesh)
        attr = constant_mesh.GetAttribute("randomArrayData")
        self.assertTrue(attr)
        random_values = attr.Get()
        self.assertEqual(len(random_values), 8)

        # Run split meshes
        json = """[{"operation": "splitMeshes", "paths": [], "splitCollocatedPoints": true}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertTrue(status)

        # First, check that the random array data was NOT carried over
        constant_1 = stage.GetPrimAtPath("/MergedConstant_part")
        self.assertTrue(constant_1)
        attr = constant_1.GetAttribute("randomArrayData")
        self.assertFalse(attr)
        self.assertEqual(len(constant_1.GetAttribute("points").Get()), 9)

        # Then assert the expected sliced primvar values
        # Note: These were originally constant, but "merged" to uniform. So they are uniform
        # values, although the meshes are called "constant".
        colors = _get_colors(constant_1)
        self.assertEqual(len(colors), 4)
        self.assertEqual(list(colors), [(1, 0, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0)])

        constant_2 = stage.GetPrimAtPath("/MergedConstant_part_1")
        colors = _get_colors(constant_2)
        self.assertEqual(len(colors), 4)
        self.assertEqual(list(colors), [(0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0)])
        self.assertEqual(len(constant_2.GetAttribute("points").Get()), 9)

        vertex_1 = stage.GetPrimAtPath("/MergedVertex_part")
        colors = _get_colors(vertex_1)
        self.assertEqual(len(colors), 9)
        self.assertEqual(len(vertex_1.GetAttribute("points").Get()), len(colors))
        self.assertEqual(
            list(colors),
            [(1, 0, 0), (1, 0, 0), (1, 0, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 0, 1), (0, 0, 1), (0, 0, 1)],
        )

        vertex_2 = stage.GetPrimAtPath("/MergedVertex_part_1")
        colors = _get_colors(vertex_2)
        self.assertEqual(len(colors), 9)
        self.assertEqual(len(vertex_2.GetAttribute("points").Get()), len(colors))
        self.assertEqual(
            list(colors),
            [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 1, 0), (0, 1, 0), (0, 1, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0)],
        )

        face_varying_1 = stage.GetPrimAtPath("/MergedFaceVarying_part")
        colors = _get_colors(face_varying_1)
        self.assertEqual(len(colors), 16)
        self.assertEqual(len(face_varying_1.GetAttribute("faceVertexIndices").Get()), len(colors))
        self.assertEqual(
            list(colors),
            [
                (1, 1, 1),
                (1, 0, 0),
                (1, 0, 0),
                (1, 0, 0),
                (1, 1, 1),
                (0, 1, 0),
                (0, 1, 0),
                (0, 1, 0),
                (1, 1, 1),
                (0, 0, 1),
                (0, 0, 1),
                (0, 0, 1),
                (1, 1, 1),
                (1, 0, 0),
                (1, 0, 0),
                (1, 0, 0),
            ],
        )

        face_varying_2 = stage.GetPrimAtPath("/MergedFaceVarying_part_1")
        colors = _get_colors(face_varying_2)
        self.assertEqual(len(colors), 16)
        self.assertEqual(len(face_varying_2.GetAttribute("faceVertexIndices").Get()), len(colors))
        self.assertEqual(
            list(colors),
            [
                (1, 0, 0),
                (1, 0, 0),
                (1, 0, 0),
                (1, 1, 1),
                (0, 0, 1),
                (0, 0, 1),
                (0, 0, 1),
                (1, 1, 1),
                (0, 1, 0),
                (0, 1, 0),
                (0, 1, 0),
                (1, 1, 1),
                (1, 0, 0),
                (1, 0, 0),
                (1, 0, 0),
                (1, 1, 1),
            ],
        )

        varying_1 = stage.GetPrimAtPath("/MergedVarying_part")
        colors = _get_colors(varying_1)
        self.assertEqual(len(colors), 9)
        self.assertEqual(len(varying_1.GetAttribute("points").Get()), len(colors))
        self.assertEqual(
            list(colors),
            [(1, 0, 0), (1, 0, 0), (1, 0, 0), (0, 1, 0), (0, 1, 0), (0, 1, 0), (0, 0, 1), (0, 0, 1), (0, 0, 1)],
        )

        varying_2 = stage.GetPrimAtPath("/MergedVarying_part_1")
        colors = _get_colors(varying_2)
        self.assertEqual(len(colors), 9)
        self.assertEqual(len(varying_2.GetAttribute("points").Get()), len(colors))
        self.assertEqual(
            list(colors),
            [(0, 0, 1), (0, 0, 1), (0, 0, 1), (0, 1, 0), (0, 1, 0), (0, 1, 0), (1, 0, 0), (1, 0, 0), (1, 0, 0)],
        )

        uniform_1 = stage.GetPrimAtPath("/MergedUniform_part")
        colors = _get_colors(uniform_1)
        self.assertEqual(len(colors), 4)
        self.assertEqual(len(uniform_1.GetAttribute("faceVertexCounts").Get()), len(colors))
        self.assertEqual(list(colors), [(1, 0, 0), (0, 1, 0), (0, 0, 1), (1, 0, 0)])

        uniform_2 = stage.GetPrimAtPath("/MergedUniform_part_1")
        colors = _get_colors(uniform_2)
        self.assertEqual(len(colors), 4)
        self.assertEqual(len(uniform_2.GetAttribute("faceVertexCounts").Get()), len(colors))
        self.assertEqual(list(colors), [(1, 0, 0), (0, 0, 1), (0, 1, 0), (1, 0, 0)])

    async def test_schemas(self):
        """Test splitting meshes retains their schemas"""

        stage = self._open_stage("primvarSplit.usda")

        # Assert an API schema exists
        constant_mesh = stage.GetPrimAtPath("/MergedConstant")
        self.assertTrue(constant_mesh)
        schemas = constant_mesh.GetAppliedSchemas()
        self.assertIn("ShadowAPI", schemas)

        # Run split meshes
        json = """[{"operation": "splitMeshes", "paths": [], "splitCollocatedPoints": true}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertTrue(status)

        # Assert the parts also contain the schema
        constant_1 = stage.GetPrimAtPath("/MergedConstant_part")
        self.assertTrue(constant_1)
        schemas = constant_1.GetAppliedSchemas()
        self.assertIn("ShadowAPI", schemas)

        constant_2 = stage.GetPrimAtPath("/MergedConstant_part_1")
        self.assertTrue(constant_2)
        schemas = constant_2.GetAppliedSchemas()
        self.assertIn("ShadowAPI", schemas)

    def _test_subset_part(self, stage, mesh_path, material_path, counts=[8, 6, 24]):
        """Helper function to validate a mesh cube with a material"""

        mesh = stage.GetPrimAtPath(mesh_path)

        self.assertTrue(mesh)

        # Assert topology
        points = mesh.GetAttribute("points").Get()
        self.assertEqual(len(points), counts[0])

        fvc = mesh.GetAttribute("faceVertexCounts").Get()
        self.assertEqual(len(fvc), counts[1])

        fvi = mesh.GetAttribute("faceVertexIndices").Get()
        self.assertEqual(len(fvi), counts[2])

        # Assert expected material
        self.assertTrue(_is_bound_material(mesh, material_path))

    async def test_splitting_subsets(self):
        """Test splitting meshes based on existing UsdGeomSubsets"""

        stage = self._open_stage("splitMeshes_subsets.usda")

        # Quick initial assert of expected mesh numbers
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 3)

        thing1 = stage.GetPrimAtPath("/World/Thing1")
        self.assertTrue(thing1)
        self.assertTrue(thing1.IsActive())

        thing2 = stage.GetPrimAtPath("/World/Thing2")
        self.assertTrue(thing2)
        self.assertTrue(thing2.IsActive())

        thing3 = stage.GetPrimAtPath("/World/Thing3")
        self.assertTrue(thing2)
        self.assertTrue(thing2.IsActive())

        # Execute command
        json = """[{"operation": "splitMeshes", "paths": [], "splitOn": 1, "splitCollocatedPoints": true, "originalGeomOption": 2}]"""
        status = standalone.execute_commands_from_json(stage, json)

        # Assert new expected mesh count
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 8)

        # Two of the meshes should get merged, one has no subsets and should remain
        # active.
        self.assertFalse(thing1.IsActive())
        self.assertFalse(thing2.IsActive())
        self.assertTrue(thing3.IsActive())

        # Assert Thing1 split in to 5 new meshes and each retained the correct material
        self._test_subset_part(stage, "/World/Thing1_part", "/World/Looks/Material1")
        self._test_subset_part(stage, "/World/Thing1_part_1", "/World/Looks/Material2")
        self._test_subset_part(stage, "/World/Thing1_part_2", "/World/Looks/Material3")
        self._test_subset_part(stage, "/World/Thing1_part_3", "/World/Looks/Material4")
        self._test_subset_part(stage, "/World/Thing1_part_4", "/World/Looks/Material5")

        # Assert Thing2 split in to two meshes - one described via subset, the rest
        # from the unassigned indices
        self._test_subset_part(stage, "/World/Thing2_part", "/World/Looks/Material1", counts=[32, 24, 96])
        self._test_subset_part(stage, "/World/Thing2_part_1", "/World/Looks/Material2")

    async def test_output_subsets(self):
        """Test splitting meshes to debug subsets"""

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["method"] = METHOD_GEOM_SUBSETS

        # Open the stage and execute the operation with "Mesh Prims" as the "Method".
        stage = self._open_stage("splitMeshes_simple.usd")

        # Meshes were not split
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 3)

        # None have children
        for mesh in meshes:
            self.assertEqual(len(mesh.GetChildren()), 0)

        self._execute_command(args)

        # Meshes were not split
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 3)

        # Both should have three subset children now
        mesh1 = stage.GetPrimAtPath("/merged")
        self.assertEqual(len(mesh1.GetChildren()), 8)

        mesh2 = stage.GetPrimAtPath("/merged_01")
        self.assertEqual(len(mesh2.GetChildren()), 8)

        mesh3 = stage.GetPrimAtPath("/merged_1")
        self.assertEqual(len(mesh3.GetChildren()), 16)

    async def test_points_with_hash_collision(self):
        """Test welding points where VtHashValue collides"""

        # Get a copy of the default args then customize them.
        args = DEFAULT_ARGS.copy()
        args["splitCollocatedPoints"] = False

        stage = self._open_stage("splitMeshesHashCollision.usd")

        # One mesh
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 1)

        # Execute
        self._execute_command(args)

        # Should now be 6 active meshes.
        # The original code used VtHashValue to find unique points, but
        # a bunch of these collide, causing the mesh to not be split.
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 6)

    async def test_split_subsets_shared_vertex(self):
        """Test splitting on GeomSubsets, where faces within the subsets share vertices"""

        stage = self._open_stage("bollard_01_inst_base.usd")

        # Assert the input mesh is valid
        originalMesh = stage.GetPrimAtPath("/RootNode/bollard_01/bollard_01")
        self.assertTrue(originalMesh.IsA(UsdGeom.Mesh))
        originalPoints = originalMesh.GetAttribute("points").Get()

        args = DEFAULT_ARGS.copy()
        args["splitOn"] = SPLIT_ON_SUBSETS

        self._execute_command(args)

        # Should be two meshes now (two subsets, split)
        meshes = [x for x in stage.Traverse() if UsdGeom.Mesh(x)]
        self.assertEqual(len(meshes), 2)

        part1 = stage.GetPrimAtPath("/RootNode/bollard_01/bollard_01_part")
        points1_expected = 1040
        points1 = part1.GetAttribute("points").Get()
        face_indices_1 = part1.GetAttribute("faceVertexIndices").Get()
        face_max_1 = max(face_indices_1)

        self.assertEqual(len(points1), points1_expected)
        self.assertLess(face_max_1, len(points1))

        # One of the vertices is shared between faces which are split, we need to
        # ensure it was not mapped to the index from the previous part. In this
        # scene that works out to be out of bounds, so we just check there are no
        # indices greater than the points.
        part2 = stage.GetPrimAtPath("/RootNode/bollard_01/bollard_01_part_1")
        points2_expected = 383
        points2 = part2.GetAttribute("points").Get()
        face_indices_2 = part2.GetAttribute("faceVertexIndices").Get()
        face_max_2 = max(face_indices_2)

        self.assertEqual(len(points2), points2_expected)
        self.assertLess(face_max_2, len(points2))

        # Because of the shared vertex between the faces we will end up with
        # more points in total.
        self.assertGreater(points1_expected + points2_expected, len(originalPoints))

    async def test_time_varying_meshes(self):
        """Test split meshes operation on meshes with authored time varying attributes"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

    async def test_split_vertices_with_subsets(self):
        """Test splitting on vertices, where the mesh has subsets with materials, keeps
        the bound materials"""

        # Open the stage
        stage = self._open_stage("splitMeshes_subsets.usda")

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)

        prim1 = stage.GetPrimAtPath("/World/Thing1_part")
        self.assertTrue(_is_bound_material(prim1, "/World/Looks/Material5"))

        prim2 = stage.GetPrimAtPath("/World/Thing1_part_1")
        self.assertTrue(_is_bound_material(prim2, "/World/Looks/Material4"))

        prim3 = stage.GetPrimAtPath("/World/Thing1_part_2")
        self.assertTrue(_is_bound_material(prim3, "/World/Looks/Material3"))

        prim4 = stage.GetPrimAtPath("/World/Thing1_part_3")
        self.assertTrue(_is_bound_material(prim4, "/World/Looks/Material2"))

        prim5 = stage.GetPrimAtPath("/World/Thing1_part_4")
        self.assertTrue(_is_bound_material(prim5, "/World/Looks/Material1"))
