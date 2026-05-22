# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import logging

from pxr import Gf, Usd, UsdGeom

from .test_utils import Test_Operation, _get_meshes

logger = logging.getLogger(__name__)

_KEY_PATHS = "paths"
_KEY_DIM = "dim"
_KEY_VOXEL_SIZE = "voxelSize"
_KEY_ERODE = "erode"
_KEY_THRESHOLD = "threshold"
_KEY_ADAPTIVITY = "adaptivity"
_KEY_EXTRACT_LOD_PYRAMID = "extractLodPyramid"

DEFAULT_ARGS = {
    _KEY_PATHS: [],
    _KEY_DIM: 512,
    _KEY_VOXEL_SIZE: 0.0,
    _KEY_ERODE: 8.0,
    _KEY_THRESHOLD: 0.0,
    _KEY_ADAPTIVITY: 0.0,
    _KEY_EXTRACT_LOD_PYRAMID: False,
}


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

        return mesh

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
