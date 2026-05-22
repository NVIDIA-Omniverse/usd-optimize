# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Gf, Sdf, Usd, UsdGeom

from .test_utils import Test_Operation, _get_context, _get_test_data_file_path

APPLY_TO_MESHES = 0
APPLY_TO_MESHES_AND_XFORMS = 1

METHOD_WEIGHTED = 0
METHOD_CENTER = 1

DEFAULT_ARGS = {"meshPrimPaths": [], "overwrite": False, "applyTo": APPLY_TO_MESHES, "method": METHOD_WEIGHTED}


def get_test_files():
    return [
        "pivot/one_transformed_mesh.usda",
        "pivot/three_boxes.usda",
        "pivot/SimpleReproFail.usda",
        "pivot/SimpleReproSuccess.usda",
    ]


def _get_point_based_prim_paths(stage):
    """Compute the list of paths of all point based prims in the given stage."""
    result = []
    for prim in stage.Traverse():
        point_based = UsdGeom.PointBased(prim)
        if not point_based:
            continue
        result.append(point_based.GetPath())
    return result


def _get_worldspace_points(prim, xformCache):
    """Returns points in worldspace"""
    # Get the points and local to world transform matrix.
    points = UsdGeom.PointBased(prim).GetPointsAttr().Get()
    matrix = xformCache.GetLocalToWorldTransform(prim)

    # Return the points with the matrix applied.
    return [matrix.Transform(x) for x in points]


def _get_pivot_op(prim):
    """Get the translate:pivot xform op"""

    xformable = UsdGeom.Xformable(prim)
    return xformable.GetXformOp(UsdGeom.XformOp.TypeTranslate, "pivot")


def _get_translate_op(prim):
    """Get the translate xform op"""
    xformable = UsdGeom.Xformable(prim)
    return xformable.GetXformOp(UsdGeom.XformOp.TypeTranslate)


def _get_bbox(prim):
    """Get bounding box of prim"""

    bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), includedPurposes=[UsdGeom.Tokens.default_])
    return bboxCache.ComputeWorldBound(prim)


# Note: primvars:omni:kit:isGizmo prims are authored with dedicated purpose and should not be changed (e.g. Cameras).
# For now we handle nested Gprims by checking that the inverse has been added to the bottom of the local transform stack of each child.


class Test_Operation_Pivot(Test_Operation):

    OPERATION = "pivot"

    def verify_world_space_points(self, old_stage, new_stage):
        # Construct an xform cache to speed up local to world calculations.
        xformCache = UsdGeom.XformCache()
        old_paths = _get_point_based_prim_paths(old_stage)
        new_paths = _get_point_based_prim_paths(new_stage)
        self.assertEqual(old_paths, new_paths)

        for path in old_paths:
            old_prim = old_stage.GetPrimAtPath(path)
            new_prim = new_stage.GetPrimAtPath(path)

            old_points = _get_worldspace_points(old_prim, xformCache)
            new_points = _get_worldspace_points(new_prim, xformCache)
            for old_p, new_p in zip(old_points, new_points):
                for i in [0, 1, 2]:
                    self.assertTrue(old_p[i] - new_p[i] < 0.0001)

    async def test_Pivot(self):

        for file in get_test_files():
            file_path = _get_test_data_file_path(file)
            layer = Sdf.Layer.OpenAsAnonymous(file_path)
            stage_old = Usd.Stage.Open(layer)

            stage_new = self._open_stage(file)
            self._execute_json(stage_new, "pivot/pivot.json")

            self.verify_world_space_points(stage_old, stage_new)

            for prim in stage_new.Traverse():
                if prim.IsA(UsdGeom.Mesh) and not prim.IsInstanceProxy():
                    xformable = UsdGeom.Xformable(prim)
                    xformCommon = UsdGeom.XformCommonAPI(prim)
                    if xformCommon:
                        self.assertTrue(
                            "xformOp:translate:pivot" in [op.GetName() for op in xformable.GetOrderedXformOps()]
                        )

    async def test_Pivot_command(self):

        for file in get_test_files():
            file_path = _get_test_data_file_path(file)
            layer = Sdf.Layer.OpenAsAnonymous(file_path)
            stage_old = Usd.Stage.Open(layer)

            stage_new = self._open_stage(file)

            args = {"meshPrimPaths": []}
            self._execute_command(args)

            self.verify_world_space_points(stage_old, stage_new)

            for prim in stage_new.Traverse():
                if prim.IsA(UsdGeom.Mesh) and not prim.IsInstanceProxy():
                    xformable = UsdGeom.Xformable(prim)
                    xformCommon = UsdGeom.XformCommonAPI(prim)
                    if xformCommon:
                        self.assertTrue(
                            "xformOp:translate:pivot" in [op.GetName() for op in xformable.GetOrderedXformOps()]
                        )

    async def test_pivot_overwrite(self):
        """Test overwriting an existing authored pivot"""

        stage = self._open_stage("pivot.usda")

        context = _get_context(stage)

        prim = stage.GetPrimAtPath("/World/Xform/pivotTool")

        # Assert existing pivot value
        pivot = _get_pivot_op(prim).Get()
        self.assertEqual(pivot, Gf.Vec3d(50, 50, -50))

        # Cache the before bbox, so we can assert it never changes
        bbox = _get_bbox(prim)

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # After running with default args, pivot has not been modified,
        # nor has bbox
        pivot = _get_pivot_op(prim).Get()
        self.assertEqual(pivot, Gf.Vec3d(50, 50, -50))
        self.assertEqual(_get_bbox(prim), bbox)

        # Re-run with overwrite enabled
        args["overwrite"] = True
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # Pivot has been overwritten, bbox remains the same
        pivot = _get_pivot_op(prim).Get()
        self.assertEqual(pivot, Gf.Vec3d(0, 0, 0))
        self.assertEqual(_get_bbox(prim), bbox)

    async def test_pivot_time_varying(self):
        """Test we do not author to a time varying prim"""

        stage = self._open_stage("pivot.usda")

        context = _get_context(stage)

        args = DEFAULT_ARGS.copy()
        args["overwrite"] = True
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        primTimeVarying = stage.GetPrimAtPath("/World/Xform_01/CubeTimeVarying")

        # Time varying prim, should not have a pivot authored
        op = _get_pivot_op(primTimeVarying)
        self.assertFalse(op)

    async def test_pivot_xforms_center(self):
        """Test setting pivots on xforms, and at centre vs weighted"""

        stage = self._open_stage("pivot.usda")

        context = _get_context(stage)

        prim = stage.GetPrimAtPath("/World/Xform")

        # Initially has no pivot
        self.assertFalse(_get_pivot_op(prim))

        # Use default settings (weighted pivot)
        args = DEFAULT_ARGS.copy()
        args["overwrite"] = True
        args["applyTo"] = APPLY_TO_MESHES_AND_XFORMS
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # Should be a pivot
        op = _get_pivot_op(prim)
        self.assertTrue(op)

        # Assert (approx) value
        pivotWeighted = op.Get()
        self.assertAlmostEqual(pivotWeighted[0], -10.0, delta=0.000001)
        self.assertAlmostEqual(pivotWeighted[1], 100.0, delta=0.000001)
        self.assertAlmostEqual(pivotWeighted[2], 443.33, delta=0.01)

        # Run again, but using center method
        args["method"] = METHOD_CENTER
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # Should be a pivot
        op = _get_pivot_op(prim)
        self.assertTrue(op)

        # Assert (approx) value
        pivotCenter = op.Get()
        self.assertAlmostEqual(pivotCenter[0], -15.0, delta=0.00000001)
        self.assertAlmostEqual(pivotCenter[1], 100.0, delta=0.00000001)
        self.assertAlmostEqual(pivotCenter[2], 420, delta=0.00000001)

    async def test_pivot_leaf_xform(self):
        """Test attempting to author pivot on an xform with no descendants"""

        stage = self._open_stage("pivot.usda")

        context = _get_context(stage)

        prim = stage.GetPrimAtPath("/World/XformNoMeshes")

        # Initially has no pivot
        self.assertFalse(_get_pivot_op(prim))

        # Make sure xforms are processed, then execute
        args = DEFAULT_ARGS.copy()
        args["overwrite"] = True
        args["applyTo"] = APPLY_TO_MESHES_AND_XFORMS
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # Still no pivot
        op = _get_pivot_op(prim)
        self.assertFalse(op)

    async def test_pivot_scaled_xform(self):
        """Test adjusting pivot on a scaled xform"""

        stage = self._open_stage("pivot.usda")

        context = _get_context(stage)

        prim = stage.GetPrimAtPath("/World/ScaledXform")

        # Initially has no pivot
        self.assertFalse(_get_pivot_op(prim))

        bbox = _get_bbox(prim)
        print(bbox)

        # Make sure xforms are processed, then execute
        args = DEFAULT_ARGS.copy()
        args["applyTo"] = APPLY_TO_MESHES_AND_XFORMS
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # Assert bbox has not changed
        self.assertEqual(_get_bbox(prim), bbox)

    async def test_pivot_mesh_no_points(self):
        """Test that a mesh with no points does not end up with a pivot"""

        stage = self._open_stage("pivot.usda")

        # Assert initial state
        prim = stage.GetPrimAtPath("/World/CubeNoPoints")
        self.assertFalse(_get_pivot_op(prim))

        # Make sure xforms are processed, then execute
        args = DEFAULT_ARGS.copy()
        args["applyTo"] = APPLY_TO_MESHES_AND_XFORMS
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # Assert there is still no pivot
        self.assertFalse(_get_pivot_op(prim))

        # An xform with an empty mesh underneath also gets no pivot
        xform = stage.GetPrimAtPath("/World/XformNoGeometry")
        self.assertFalse(_get_pivot_op(xform))

        # Also test that a pivot that has an *empty* points attr
        # does not fail.
        stage = self._open_stage("pivotBadMesh.usda")

        prim = stage.GetPrimAtPath("/World/Model/Mesh")
        self.assertFalse(_get_pivot_op(prim))

        success, result = self._execute_command(args)
        self.assertTrue(success)

        # After processing, still no pivot (but no crash!)
        self.assertFalse(_get_pivot_op(prim))

    async def test_pivot_common_api_meshes(self):
        """Ex golden-file test"""

        stage = self._open_stage("common_api_meshes.usda")

        prim1 = stage.GetPrimAtPath("/World/DistoredCube_0")
        self.assertFalse(_get_pivot_op(prim1))

        prim2 = stage.GetPrimAtPath("/World/DistoredCube_1")
        self.assertFalse(_get_pivot_op(prim2))

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # Assert (approx) values of first mesh
        op1 = _get_pivot_op(prim1)
        self.assertTrue(op1)
        pivot1 = op1.Get()
        self.assertAlmostEqual(pivot1[0], 1.125, delta=0.00000001)
        self.assertAlmostEqual(pivot1[1], 2.3125, delta=0.00000001)
        self.assertAlmostEqual(pivot1[2], 3.5, delta=0.00000001)

        top1 = _get_translate_op(prim1)
        self.assertTrue(top1)
        translate1 = top1.Get()
        self.assertAlmostEqual(translate1[0], -7.346607208251953, delta=0.000001)
        self.assertAlmostEqual(translate1[1], 15.861201763153076, delta=0.000001)
        self.assertAlmostEqual(translate1[2], -17.945528030395508, delta=0.000001)

        # Assert translate and pivot of second mesh
        op2 = _get_pivot_op(prim2)
        self.assertTrue(op2)
        pivot2 = op2.Get()
        self.assertAlmostEqual(pivot2[0], 1.125, delta=0.00000001)
        self.assertAlmostEqual(pivot2[1], 2.3125, delta=0.00000001)
        self.assertAlmostEqual(pivot2[2], 3.5, delta=0.00000001)

        top2 = _get_translate_op(prim2)
        self.assertTrue(top2)
        translate2 = top2.Get()
        self.assertAlmostEqual(translate2[0], 6.125, delta=0.000001)
        self.assertAlmostEqual(translate2[1], 12.625, delta=0.000001)
        self.assertAlmostEqual(translate2[2], 0.5, delta=0.000001)
