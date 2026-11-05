# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import logging

from pxr import Gf, Usd, UsdGeom

from .test_utils import Test_Operation, _get_context, _get_meshes

logger = logging.getLogger(__name__)

_KEY_PATHS = "paths"
_KEY_INPUT_MODE = "inputMode"
_KEY_USE_STAGE_TIME_RANGE = "useStageTimeRange"
_KEY_START_TIME = "startTime"
_KEY_END_TIME = "endTime"
_KEY_TIME_STEP = "timeStep"
_KEY_CONNECT_TIME_SAMPLES = "connectTimeSamples"
_KEY_OUTPUT_PATH = "outputPath"
_KEY_DIM = "dim"
_KEY_VOXEL_SIZE = "voxelSize"
_KEY_ERODE = "erode"
_KEY_THRESHOLD = "threshold"
_KEY_ADAPTIVITY = "adaptivity"
_KEY_EXTRACT_LOD_PYRAMID = "extractLodPyramid"

DEFAULT_ARGS = {
    _KEY_PATHS: [],
    _KEY_INPUT_MODE: 0,
    _KEY_TIME_STEP: 1.0,
    _KEY_CONNECT_TIME_SAMPLES: True,
    _KEY_OUTPUT_PATH: "",
    _KEY_DIM: 512,
    _KEY_VOXEL_SIZE: 0.0,
    _KEY_ERODE: 8.0,
    _KEY_THRESHOLD: 0.0,
    _KEY_ADAPTIVITY: 0.0,
    _KEY_EXTRACT_LOD_PYRAMID: False,
}


_CUBE_POINTS = [
    Gf.Vec3f(-1, -1, -1),
    Gf.Vec3f(1, -1, -1),
    Gf.Vec3f(-1, 1, -1),
    Gf.Vec3f(1, 1, -1),
    Gf.Vec3f(-1, -1, 1),
    Gf.Vec3f(1, -1, 1),
    Gf.Vec3f(-1, 1, 1),
    Gf.Vec3f(1, 1, 1),
]
_CUBE_FACE_COUNTS = [4, 4, 4, 4, 4, 4]
_CUBE_FACE_INDICES = [
    0,
    1,
    3,
    2,
    4,
    6,
    7,
    5,
    0,
    4,
    5,
    1,
    2,
    3,
    7,
    6,
    0,
    2,
    6,
    4,
    1,
    5,
    7,
    3,
]


def _translated_points(x):
    return [Gf.Vec3f(point[0] + x, point[1], point[2]) for point in _CUBE_POINTS]


def _define_cube(stage, path, x=0.0):
    mesh = UsdGeom.Mesh.Define(stage, path)
    mesh.GetPointsAttr().Set(_translated_points(x))
    mesh.GetFaceVertexCountsAttr().Set(_CUBE_FACE_COUNTS)
    mesh.GetFaceVertexIndicesAttr().Set(_CUBE_FACE_INDICES)
    mesh.GetSubdivisionSchemeAttr().Set(UsdGeom.Tokens.none)
    return mesh


def _world_range(stage, prim_path, time):
    cache = UsdGeom.BBoxCache(Usd.TimeCode(time), [UsdGeom.Tokens.default_])
    return cache.ComputeWorldBound(stage.GetPrimAtPath(prim_path)).ComputeAlignedRange()


def _temporal_args(output_path, paths, start=1.0, end=3.0, step=1.0):
    args = DEFAULT_ARGS.copy()
    args.update(
        {
            _KEY_PATHS: paths,
            _KEY_INPUT_MODE: 1,
            _KEY_TIME_STEP: step,
            _KEY_OUTPUT_PATH: output_path,
            _KEY_DIM: 128,
            _KEY_VOXEL_SIZE: 0.25,
            _KEY_ERODE: 0.0,
        }
    )
    if start is not None or end is not None:
        args[_KEY_USE_STAGE_TIME_RANGE] = False
    if start is not None:
        args[_KEY_START_TIME] = start
    if end is not None:
        args[_KEY_END_TIME] = end
    return args


class Test_Operation_Shrinkwrap(Test_Operation):
    """Tests for the Shrinkwrap operation, which converts meshes to watertight level-set surfaces."""

    OPERATION = "shrinkwrap"

    def _assert_valid_shrinkwrap_mesh(self, stage, prim_path):
        """Assert that a valid shrinkwrap mesh exists at prim_path with non-zero geometry."""
        prim = stage.GetPrimAtPath(prim_path)
        self.assertTrue(prim.IsValid(), f"Shrinkwrap mesh not found at {prim_path}")
        self.assertTrue(prim.IsA(UsdGeom.Mesh), f"Prim at {prim_path} is not a Mesh")

        mesh = UsdGeom.Mesh(prim)
        points = mesh.GetPointsAttr().Get()
        self.assertIsNotNone(points, f"Mesh at {prim_path} has no points attribute")
        self.assertGreater(len(points), 0, f"Mesh at {prim_path} has zero vertices")

        face_vertex_counts = mesh.GetFaceVertexCountsAttr().Get()
        self.assertIsNotNone(face_vertex_counts, f"Mesh at {prim_path} has no face vertex counts")
        self.assertGreater(len(face_vertex_counts), 0, f"Mesh at {prim_path} has zero faces")

        face_vertex_indices = mesh.GetFaceVertexIndicesAttr().Get()
        self.assertIsNotNone(face_vertex_indices, f"Mesh at {prim_path} has no face vertex indices")
        self.assertEqual(sum(face_vertex_counts), len(face_vertex_indices))
        self.assertTrue(all(0 <= index < len(points) for index in face_vertex_indices))

        return mesh

    def _shrinkwrap_cube(self, stage, lod_pyramid=False):
        """Shrinkwrap /World/Cube in the given stage, asserting that the operation succeeds."""
        args = {**DEFAULT_ARGS, _KEY_PATHS: ["/World/Cube"], _KEY_DIM: 64, _KEY_EXTRACT_LOD_PYRAMID: lod_pyramid}
        success, result = self._execute_command(args, _get_context(stage))
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])

    def _assert_aligned(self, stage, output_path, source_path, time=0.0, tolerance=0.5):
        """Assert that the output's world-space bounds match its source's."""
        output = _world_range(stage, output_path, time)
        source = _world_range(stage, source_path, time)
        self.assertTrue(
            Gf.IsClose(output.GetMin(), source.GetMin(), tolerance)
            and Gf.IsClose(output.GetMax(), source.GetMax(), tolerance),
            f"{output_path} {output} is not aligned with {source_path} {source}",
        )

    async def test_shrinkwrap_with_dim(self):
        """Test shrinkwrap using grid dimension for resolution"""
        stage = self._open_stage("simpleCube.usda")
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 1)

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 32
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        self._assert_valid_shrinkwrap_mesh(stage, "/World/Cube_shrinkwrap")

    async def test_shrinkwrap_with_voxel_size(self):
        """Test shrinkwrap using explicit voxel size for resolution"""
        stage = self._open_stage("simpleCube.usda")
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 1)

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 0
        args[_KEY_VOXEL_SIZE] = 0.5
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        self._assert_valid_shrinkwrap_mesh(stage, "/World/Cube_shrinkwrap")

    async def test_shrinkwrap_both_dim_and_voxel(self):
        """Both dim and voxelSize set should succeed, with dim acting as a cap"""
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 64
        args[_KEY_VOXEL_SIZE] = 0.5
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        self._assert_valid_shrinkwrap_mesh(stage, "/World/Cube_shrinkwrap")

    async def test_shrinkwrap_with_paths(self):
        """Test shrinkwrap with specific prim paths"""
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args[_KEY_PATHS] = ["/World/Cube"]
        args[_KEY_DIM] = 32
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        self._assert_valid_shrinkwrap_mesh(stage, "/World/Cube_shrinkwrap")

        source_prim = stage.GetPrimAtPath("/World/Cube")
        self.assertTrue(source_prim.IsValid(), "Original source mesh should still exist")

    async def test_shrinkwrap_extract_lod_pyramid(self):
        """Test shrinkwrap with LOD pyramid extraction enabled.

        Verifies that at least the base shrinkwrap mesh is created, and any
        additional LOD levels have monotonically decreasing vertex counts.
        """
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 32
        args[_KEY_EXTRACT_LOD_PYRAMID] = True
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        source_path = "/World/Cube"
        lod_meshes = []

        lod0_path = f"{source_path}_shrinkwrap"
        lod0 = stage.GetPrimAtPath(lod0_path)
        if lod0.IsValid() and lod0.IsA(UsdGeom.Mesh):
            lod_meshes.append(lod0)

        lod_idx = 1
        while True:
            lod_path = f"{source_path}_shrinkwrap_lod{lod_idx}"
            lod_prim = stage.GetPrimAtPath(lod_path)
            if not lod_prim.IsValid():
                break
            if lod_prim.IsA(UsdGeom.Mesh):
                lod_meshes.append(lod_prim)
            lod_idx += 1

        self.assertGreaterEqual(len(lod_meshes), 1, "Expected at least one shrinkwrap LOD mesh")

        prev_vertex_count = None
        for i, prim in enumerate(lod_meshes):
            mesh = UsdGeom.Mesh(prim)
            points = mesh.GetPointsAttr().Get()
            vertex_count = len(points) if points else 0
            self.assertGreater(vertex_count, 0, f"LOD {i} has no vertices")

            if prev_vertex_count is not None:
                self.assertLessEqual(
                    vertex_count,
                    prev_vertex_count,
                    f"LOD {i} has more vertices ({vertex_count}) than LOD {i-1} ({prev_vertex_count})",
                )
            prev_vertex_count = vertex_count

    async def test_shrinkwrap_adaptivity(self):
        """Test shrinkwrap with adaptive meshing threshold"""
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 32
        args[_KEY_ADAPTIVITY] = 0.5
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        mesh = self._assert_valid_shrinkwrap_mesh(stage, "/World/Cube_shrinkwrap")
        adaptive_faces = len(mesh.GetFaceVertexCountsAttr().Get())

        baseline_args = DEFAULT_ARGS.copy()
        baseline_args[_KEY_DIM] = 32
        baseline_args[_KEY_ADAPTIVITY] = 0.0
        stage_baseline = self._open_stage("simpleCube.usda")
        baseline_success, _baseline_result = self._execute_command(baseline_args)
        self.assertTrue(baseline_success, "Baseline shrinkwrap operation failed")
        baseline_mesh = self._assert_valid_shrinkwrap_mesh(stage_baseline, "/World/Cube_shrinkwrap")
        baseline_faces = len(baseline_mesh.GetFaceVertexCountsAttr().Get())
        self.assertLessEqual(
            adaptive_faces,
            baseline_faces,
            f"Adaptive mesh ({adaptive_faces} faces) should have at most as many faces "
            f"as non-adaptive mesh ({baseline_faces} faces)",
        )

    async def test_shrinkwrap_erode_threshold(self):
        """Test shrinkwrap with custom level set controls"""
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 32
        args[_KEY_ERODE] = 4.0
        args[_KEY_THRESHOLD] = 1.0
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        self._assert_valid_shrinkwrap_mesh(stage, "/World/Cube_shrinkwrap")

    async def test_shrinkwrap_teapot_lod_pyramid(self):
        """Test shrinkwrap on teapot model generating a full LOD pyramid.

        Verifies that:
        - Multiple LOD meshes are created (original is preserved,
          finest gets a _shrinkwrap sibling, coarser levels get
          _shrinkwrap_lodN sibling prims).
        - Each successive LOD has fewer or equal vertices.
        """
        stage = self._open_stage("teapot.usdc")
        self.assertIsNotNone(stage)

        # The teapot mesh lives at /root/imagetostl_mesh0/imagetostl_mesh0
        source_path = "/root/imagetostl_mesh0/imagetostl_mesh0"
        source_prim = stage.GetPrimAtPath(source_path)
        self.assertTrue(source_prim.IsValid(), f"Source prim not found at {source_path}")

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 256
        args[_KEY_ERODE] = 8.0
        args[_KEY_THRESHOLD] = 0.0
        args[_KEY_EXTRACT_LOD_PYRAMID] = True
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        # Collect all LOD meshes: LOD 0 is a _shrinkwrap sibling, coarser levels
        # are _shrinkwrap_lodN siblings (original mesh is preserved).
        parent_path = "/root/imagetostl_mesh0"
        parent_prim = stage.GetPrimAtPath(parent_path)
        self.assertTrue(parent_prim.IsValid())

        lod_meshes = []
        # LOD 0 is the shrinkwrap output (original is preserved)
        lod0_path = f"{source_path}_shrinkwrap"
        lod0 = stage.GetPrimAtPath(lod0_path)
        if lod0.IsValid() and lod0.IsA(UsdGeom.Mesh):
            lod_meshes.append(lod0)

        # LOD 1, 2, ... are sibling prims with _shrinkwrap_lodN suffix
        lod_idx = 1
        while True:
            lod_path = f"{source_path}_shrinkwrap_lod{lod_idx}"
            lod_prim = stage.GetPrimAtPath(lod_path)
            if not lod_prim.IsValid():
                break
            if lod_prim.IsA(UsdGeom.Mesh):
                lod_meshes.append(lod_prim)
            lod_idx += 1

        # We expect more than 1 LOD level
        self.assertGreater(len(lod_meshes), 1, f"Expected multiple LOD meshes, got {len(lod_meshes)}")
        logger.debug("Shrinkwrap teapot LOD test: generated %d LOD levels", len(lod_meshes))

        # Gather vertex counts for each LOD
        prev_vertex_count = None

        for i, prim in enumerate(lod_meshes):
            mesh = UsdGeom.Mesh(prim)
            points = mesh.GetPointsAttr().Get()
            vertex_count = len(points) if points else 0

            self.assertGreater(vertex_count, 0, f"LOD {i} has no vertices")

            # Each coarser LOD should have fewer or equal vertices
            if prev_vertex_count is not None:
                self.assertLessEqual(
                    vertex_count,
                    prev_vertex_count,
                    f"LOD {i} has more vertices ({vertex_count}) " f"than LOD {i-1} ({prev_vertex_count})",
                )

            prev_vertex_count = vertex_count

    async def test_shrinkwrap_respects_world_transform(self):
        """Test that shrinkwrap produces output at the correct world-space position
        when the source mesh has transforms offset from the origin.

        Loads simpleCube.usda and applies both an ancestral translate on /World
        and a local translate on /World/Cube. The local translate is critical:
        because the output prim is a sibling under /World it inherits the
        ancestral transform automatically, so only the local translate actually
        exercises the C++ world-space transform handling.
        """
        stage = self._open_stage("simpleCube.usda")

        world_xform = UsdGeom.Xformable(stage.GetPrimAtPath("/World"))
        world_xform.AddTranslateOp().Set(Gf.Vec3d(500, 500, 500))

        cube_prim = stage.GetPrimAtPath("/World/Cube")
        cube_prim.GetAttribute("xformOp:translate").Set(Gf.Vec3d(100, 0, 0))

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 32
        args[_KEY_PATHS] = ["/World/Cube"]
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation failed")

        sw_prim = stage.GetPrimAtPath("/World/Cube_shrinkwrap")
        self.assertTrue(sw_prim.IsValid(), "Shrinkwrap output prim not found")
        self.assertTrue(sw_prim.IsA(UsdGeom.Mesh))

        bbox_cache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), [UsdGeom.Tokens.default_])
        center = bbox_cache.ComputeWorldBound(sw_prim).ComputeCentroid()

        expected_center = Gf.Vec3d(600, 500, 500)
        tolerance = 50.0
        self.assertTrue(
            Gf.IsClose(center, expected_center, tolerance),
            f"Shrinkwrap world-space center {center} not near expected "
            f"{expected_center} (tolerance {tolerance}). "
            f"Shrinkwrap may not be accounting for world-space transforms.",
        )

    async def test_shrinkwrap_copies_source_xform_ops(self):
        """Per Mesh outputs, LODs included, copy the source's xform ops, pivot included, and line up with it."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.Xform.Define(stage, "/World").AddTranslateOp().Set(Gf.Vec3d(100, 0, 0))
        source = UsdGeom.Xformable(_define_cube(stage, "/World/Cube"))
        source.AddTranslateOp().Set(Gf.Vec3d(5, 2, 0))
        source.AddTranslateOp(opSuffix="pivot").Set(Gf.Vec3d(1, 1, 0))
        source.AddRotateXYZOp().Set(Gf.Vec3f(0, 0, 30))
        source.AddScaleOp().Set(Gf.Vec3f(1, 2, 1))
        source.AddTranslateOp(opSuffix="pivot", isInverseOp=True)

        self._shrinkwrap_cube(stage, lod_pyramid=True)

        source_ops = [(op.GetOpName(), op.Get()) for op in source.GetOrderedXformOps()]
        source_to_world = UsdGeom.XformCache().GetLocalToWorldTransform(source.GetPrim())
        outputs = [p for p in stage.GetPrimAtPath("/World").GetChildren() if p.GetName().startswith("Cube_shrinkwrap")]
        self.assertGreater(len(outputs), 1, "Expected LOD outputs")
        for output in outputs:
            self._assert_valid_shrinkwrap_mesh(stage, output.GetPath())
            output_ops = UsdGeom.Xformable(output).GetOrderedXformOps()
            self.assertEqual([(op.GetOpName(), op.Get()) for op in output_ops], source_ops)
            self.assertEqual(UsdGeom.XformCache().GetLocalToWorldTransform(output), source_to_world)
        self._assert_aligned(stage, "/World/Cube_shrinkwrap", "/World/Cube")

    async def test_shrinkwrap_output_follows_animated_source_xform(self):
        """Time-sampled xform ops are copied too, so the output tracks its source."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetStartTimeCode(1)
        stage.SetEndTimeCode(3)
        translate = UsdGeom.Xformable(_define_cube(stage, "/World/Cube")).AddTranslateOp()
        translate.Set(Gf.Vec3d(-4, 0, 0), 1)
        translate.Set(Gf.Vec3d(6, 0, 0), 3)

        self._shrinkwrap_cube(stage)

        for time in (1.0, 3.0):
            self._assert_aligned(stage, "/World/Cube_shrinkwrap", "/World/Cube", time)

    async def test_shrinkwrap_without_usable_source_xform_authors_no_ops(self):
        """A source with no xform ops, or with a singular or near-singular transform, gets an output without any."""
        for name, scale in (
            ("no ops", None),
            ("singular", Gf.Vec3d(1, 0, 1)),
            ("near singular", Gf.Vec3d(1, 1e-100, 1)),
        ):
            with self.subTest(name):
                stage = Usd.Stage.CreateInMemory()
                UsdGeom.Xform.Define(stage, "/World").AddTranslateOp().Set(Gf.Vec3d(100, 0, 0))
                source = UsdGeom.Xformable(_define_cube(stage, "/World/Cube"))
                if scale is not None:
                    source.AddScaleOp(UsdGeom.XformOp.PrecisionDouble).Set(scale)

                self._shrinkwrap_cube(stage)

                output = UsdGeom.Xformable(stage.GetPrimAtPath("/World/Cube_shrinkwrap"))
                self.assertEqual(output.GetOrderedXformOps(), [])
                self._assert_aligned(stage, "/World/Cube_shrinkwrap", "/World/Cube")

    async def test_shrinkwrap_under_singular_parent_resets_xform_stack(self):
        """Under a singular parent, the output keeps its world-space points and resets the xform stack."""
        stage = Usd.Stage.CreateInMemory()
        world = UsdGeom.Xform.Define(stage, "/World")
        world.AddTranslateOp().Set(Gf.Vec3d(100, 0, 0))
        world.AddScaleOp().Set(Gf.Vec3f(1, 0, 1))
        _define_cube(stage, "/World/Cube")

        self._shrinkwrap_cube(stage)

        self.assertTrue(UsdGeom.Xformable(stage.GetPrimAtPath("/World/Cube_shrinkwrap")).GetResetXformStack())
        self._assert_aligned(stage, "/World/Cube_shrinkwrap", "/World/Cube")

    async def test_shrinkwrap_rerun_drops_previous_output_xform_ops(self):
        """A rerun reuses the output prim, so ops copied by an earlier run must not outlive the source's."""
        for name, scale in (("no ops", None), ("singular", Gf.Vec3f(1, 0, 1))):
            with self.subTest(name):
                stage = Usd.Stage.CreateInMemory()
                UsdGeom.Xform.Define(stage, "/World").AddTranslateOp().Set(Gf.Vec3d(100, 0, 0))
                source = UsdGeom.Xformable(_define_cube(stage, "/World/Cube"))
                source.AddTranslateOp().Set(Gf.Vec3d(5, 2, 0))
                self._shrinkwrap_cube(stage)

                source.GetXformOpOrderAttr().Clear()
                if scale is not None:
                    source.AddScaleOp().Set(scale)
                self._shrinkwrap_cube(stage)

                output = UsdGeom.Xformable(stage.GetPrimAtPath("/World/Cube_shrinkwrap"))
                self.assertEqual(output.GetOrderedXformOps(), [])
                self.assertEqual(output.GetPrim().GetAuthoredPropertiesInNamespace("xformOp"), [])
                self._assert_aligned(stage, "/World/Cube_shrinkwrap", "/World/Cube")

    async def test_temporal_combined_samples_midpoint_and_step(self):
        """Temporal mode includes intermediate motion and honors the configured step."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetStartTimeCode(1)
        stage.SetEndTimeCode(3)
        mesh = _define_cube(stage, "/World/Cube")
        translate = UsdGeom.Xformable(mesh).AddTranslateOp()
        translate.Set(Gf.Vec3d(-4, 0, 0), 1)
        translate.Set(Gf.Vec3d(6, 0, 0), 2)
        translate.Set(Gf.Vec3d(-4, 0, 0), 3)

        success, result = self._execute_command(_temporal_args("/SweepAll", ["/World/Cube"]), _get_context(stage))
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])
        self._assert_valid_shrinkwrap_mesh(stage, "/SweepAll")
        all_range = _world_range(stage, "/SweepAll", 1)
        self.assertGreater(all_range.GetMax()[0], 5.0)
        self.assertLess(all_range.GetMin()[0], -3.0)

        success, result = self._execute_command(
            {
                **_temporal_args("/SweepEndpoints", ["/World/Cube"], step=2.0),
                _KEY_CONNECT_TIME_SAMPLES: False,
            },
            _get_context(stage),
        )
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])
        endpoints_range = _world_range(stage, "/SweepEndpoints", 1)
        self.assertLess(endpoints_range.GetMax()[0], 0.0)

    async def test_temporal_combined_connects_adjacent_samples(self):
        """Swept prisms fill motion between sparse, topology-compatible samples."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetStartTimeCode(1)
        stage.SetEndTimeCode(2)
        mesh = _define_cube(stage, "/World/Cube")
        translate = UsdGeom.Xformable(mesh).AddTranslateOp()
        translate.Set(Gf.Vec3d(-8, 0, 0), 1)
        translate.Set(Gf.Vec3d(8, 0, 0), 2)

        sampled_args = _temporal_args("/SampledOnly", ["/World/Cube"], start=1, end=2, step=1)
        sampled_args.update(
            {
                _KEY_CONNECT_TIME_SAMPLES: False,
                _KEY_DIM: 256,
                _KEY_VOXEL_SIZE: 0.2,
                _KEY_ERODE: 8.0,
            }
        )
        success, result = self._execute_command(sampled_args, _get_context(stage))
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])

        swept_args = sampled_args.copy()
        swept_args[_KEY_OUTPUT_PATH] = "/Swept"
        swept_args[_KEY_CONNECT_TIME_SAMPLES] = True
        success, result = self._execute_command(swept_args, _get_context(stage))
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])

        sampled_points = UsdGeom.Mesh(stage.GetPrimAtPath("/SampledOnly")).GetPointsAttr().Get()
        swept_points = UsdGeom.Mesh(stage.GetPrimAtPath("/Swept")).GetPointsAttr().Get()
        sampled_center_points = sum(abs(point[0]) < 2.0 for point in sampled_points)
        swept_center_points = sum(abs(point[0]) < 2.0 for point in swept_points)
        self.assertEqual(sampled_center_points, 0)
        self.assertGreater(swept_center_points, 0)

    async def test_temporal_combined_aggregates_selected_meshes(self):
        """Transform- and point-animated inputs combine while an unselected mesh stays excluded."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetStartTimeCode(1)
        stage.SetEndTimeCode(3)

        transformed = _define_cube(stage, "/World/Transformed")
        translate = UsdGeom.Xformable(transformed).AddTranslateOp()
        translate.Set(Gf.Vec3d(-6, 0, 0), 1)
        translate.Set(Gf.Vec3d(-3, 0, 0), 3)

        points_animated = _define_cube(stage, "/World/PointsAnimated", 5)
        points_animated.GetPointsAttr().Set(_translated_points(5), 1)
        points_animated.GetPointsAttr().Set(_translated_points(9), 2)
        points_animated.GetPointsAttr().Set(_translated_points(5), 3)
        _define_cube(stage, "/World/UnselectedDecoy", 50)

        success, result = self._execute_command(
            _temporal_args("/CombinedSweep", ["/World/Transformed", "/World/PointsAnimated"]),
            _get_context(stage),
        )
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])
        self._assert_valid_shrinkwrap_mesh(stage, "/CombinedSweep")

        output_range = _world_range(stage, "/CombinedSweep", 1)
        self.assertLess(output_range.GetMin()[0], -5.0)
        self.assertGreater(output_range.GetMax()[0], 8.0)
        self.assertLess(output_range.GetMax()[0], 40.0)
        self.assertEqual(len(_get_meshes(stage)), 4)
        self.assertTrue(stage.GetPrimAtPath("/World/Transformed"))
        self.assertTrue(stage.GetPrimAtPath("/World/PointsAnimated"))

    async def test_temporal_combined_output_is_world_static(self):
        """The output has no time samples and resets inheritance beneath an animated parent."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetStartTimeCode(1)
        stage.SetEndTimeCode(3)
        _define_cube(stage, "/Source")
        parent = UsdGeom.Xform.Define(stage, "/AnimatedParent")
        translate = parent.AddTranslateOp()
        translate.Set(Gf.Vec3d(100, 0, 0), 1)
        translate.Set(Gf.Vec3d(200, 0, 0), 3)

        success, result = self._execute_command(
            _temporal_args("/AnimatedParent/Sweep", ["/Source"]), _get_context(stage)
        )
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])
        output = stage.GetPrimAtPath("/AnimatedParent/Sweep")
        self.assertTrue(output)
        self.assertTrue(UsdGeom.Xformable(output).GetResetXformStack())
        for attribute in output.GetAttributes():
            self.assertEqual(attribute.GetTimeSamples(), [], f"{attribute.GetPath()} should be static")

        range_at_1 = _world_range(stage, "/AnimatedParent/Sweep", 1)
        range_at_3 = _world_range(stage, "/AnimatedParent/Sweep", 3)
        self.assertTrue(Gf.IsClose(range_at_1.GetMin(), range_at_3.GetMin(), 1e-4))
        self.assertTrue(Gf.IsClose(range_at_1.GetMax(), range_at_3.GetMax(), 1e-4))
        self.assertLess(range_at_1.GetMax()[0], 10.0)

    async def test_temporal_combined_uses_stage_range_and_includes_end(self):
        """By default the stage's time range is sampled, including a non-divisible final endpoint."""
        stage = Usd.Stage.CreateInMemory()
        stage.SetStartTimeCode(2)
        stage.SetEndTimeCode(5)
        mesh = _define_cube(stage, "/Cube")
        translate = UsdGeom.Xformable(mesh).AddTranslateOp()
        translate.Set(Gf.Vec3d(0, 0, 0), 2)
        translate.Set(Gf.Vec3d(10, 0, 0), 5)

        args = _temporal_args("/StageRangeSweep", ["/Cube"], start=None, end=None, step=2.0)
        success, result = self._execute_command(args, _get_context(stage))
        self.assertTrue(success)
        self.assertTrue(result[0], result[1])
        self.assertGreater(_world_range(stage, "/StageRangeSweep", 2).GetMax()[0], 9.0)

    async def test_temporal_combined_rejects_invalid_arguments(self):
        stage = Usd.Stage.CreateInMemory()
        _define_cube(stage, "/Cube")
        cases = [
            ("missing output", _temporal_args("", ["/Cube"])),
            ("relative output", _temporal_args("Sweep", ["/Cube"])),
            ("overlapping output", _temporal_args("/Cube/Sweep", ["/Cube"])),
            ("reversed range", _temporal_args("/Sweep", ["/Cube"], start=3.0, end=1.0)),
            ("zero step", _temporal_args("/Sweep", ["/Cube"], step=0.0)),
        ]
        lod_args = _temporal_args("/Sweep", ["/Cube"])
        lod_args[_KEY_EXTRACT_LOD_PYRAMID] = True
        cases.append(("LOD extraction", lod_args))

        for name, args in cases:
            with self.subTest(name=name):
                success, result = self._execute_command(args, _get_context(stage))
                self.assertFalse(success)

    async def test_temporal_combined_preserves_existing_output_prim(self):
        stage = Usd.Stage.CreateInMemory()
        _define_cube(stage, "/Cube")
        existing_output = _define_cube(stage, "/ExistingOutput", x=20.0)
        original_points = existing_output.GetPointsAttr().Get()

        args = _temporal_args("/ExistingOutput", ["/Cube"])
        success, result = self._execute_command(args, _get_context(stage))

        self.assertFalse(success)
        preserved_output = UsdGeom.Mesh(stage.GetPrimAtPath("/ExistingOutput"))
        self.assertTrue(preserved_output)
        self.assertEqual(preserved_output.GetPointsAttr().Get(), original_points)

    async def test_shrinkwrap_skips_mesh_when_voxel_too_large(self):
        """Test that shrinkwrap gracefully skips meshes when the voxel size is
        much larger than the mesh bounding box, rather than crashing inside
        OpenVDB (OM-139207).

        simpleCube has a 100-unit bbox.  With voxelSize=100 the effective grid
        dimension drops to ~9, below the narrow-band minimum of 10.  The
        operation should succeed but produce no output mesh.
        """
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args[_KEY_DIM] = 0
        args[_KEY_VOXEL_SIZE] = 100.0
        success, _result = self._execute_command(args)
        self.assertTrue(success, "Shrinkwrap operation should succeed even when skipping meshes")

        sw_prim = stage.GetPrimAtPath("/World/Cube_shrinkwrap")
        self.assertFalse(
            sw_prim.IsValid(),
            "No shrinkwrap output should be created when voxel size is too large for the mesh",
        )
