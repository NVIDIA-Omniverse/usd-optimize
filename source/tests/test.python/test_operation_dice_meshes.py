# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_meshes

GRID_TYPE_REGULAR = 0
GRID_TYPE_IRREGULAR = 1

DEFAULT_ARGS = {
    "paths": [],
    "splitDices": False,
    "gridType": GRID_TYPE_REGULAR,
    "gridCellX": 55.0,
    "gridCellY": 55.0,
    "gridCellZ": 55.0,
}


class Test_Operation_Dice_Meshes(Test_Operation):
    """Tests for the diceMeshes operation.

    Covers regular-grid dicing (split/no-split, grid origin sensitivity,
    large-cell suppression), irregular-grid dicing (single and multi-axis
    cut heights), and path filtering.
    """

    OPERATION = "diceMeshes"

    async def test_dice_grid_split(self):
        """
        Tests dicing a mesh in the scene using a grid and then splitting the diced meshes into separate prims.
        """
        sphere_path = "/World/sphere"
        args = DEFAULT_ARGS.copy()
        args["paths"] = [sphere_path]
        args["splitDices"] = True

        stage = self._open_stage("diceSplitPrims.usda")

        self._execute_command(args)

        # the sphere should have been deleted
        sphere_prim = stage.GetPrimAtPath(sphere_path)
        self.assertFalse(sphere_prim.IsValid())

        # the disjoint meshes should have been untouched
        disjoint_prim = stage.GetPrimAtPath("/World/disjoint")
        self.assertTrue(disjoint_prim.IsValid())
        self.assertTrue(disjoint_prim.IsActive())

        # the sphere should be split into 8 new prims
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part_1").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part_2").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part_3").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part_4").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part_5").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part_6").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/sphere_part_7").IsValid())
        self.assertFalse(
            stage.GetPrimAtPath("/World/sphere_part_8").IsValid()
        )  # check there's not more than 8 new prims

    async def test_dice_regular_grid_no_split(self):
        """Tests that dicing with splitDices=False modifies the mesh in place without creating new prims."""
        cube_path = "/World/Cube"
        stage = self._open_stage("simpleCube.usda")

        mesh = UsdGeom.Mesh(stage.GetPrimAtPath(cube_path))
        original_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertEqual(original_face_count, 6)

        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = False
        self._execute_command(args)

        prim = stage.GetPrimAtPath(cube_path)
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())

        mesh = UsdGeom.Mesh(prim)
        new_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertGreater(new_face_count, original_face_count)

    async def test_dice_regular_grid_with_split(self):
        """Tests dicing a simple cube with a regular grid and splitting into separate parts."""
        cube_path = "/World/Cube"
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = True
        self._execute_command(args)

        # The original cube should no longer be valid after dice+split
        prim = stage.GetPrimAtPath(cube_path)
        self.assertFalse(prim.IsValid())

        # Cube spans -50..50; cell size 55 with origin 0 gives one interior cut per axis
        # (at 0), producing 2^3 = 8 parts.
        parts = _get_meshes(stage)
        self.assertEqual(len(parts), 8)

        # Each part should be a valid, active mesh with geometry
        for part in parts:
            self.assertTrue(part.IsActive())
            mesh = UsdGeom.Mesh(part)
            face_counts = mesh.GetFaceVertexCountsAttr().Get()
            self.assertGreater(len(face_counts), 0)

    async def test_dice_specific_paths_only(self):
        """Tests that only the meshes at the specified paths are diced."""
        stage = self._open_stage("simpleFourCubes.usda")
        target_path = "/World/Cube"

        original_face_counts = {}
        for mesh_prim in _get_meshes(stage):
            path = str(mesh_prim.GetPath())
            mesh = UsdGeom.Mesh(mesh_prim)
            original_face_counts[path] = len(mesh.GetFaceVertexCountsAttr().Get())

        args = DEFAULT_ARGS.copy()
        args["paths"] = [target_path]
        args["splitDices"] = False
        self._execute_command(args)

        for mesh_prim in _get_meshes(stage):
            path = str(mesh_prim.GetPath())
            mesh = UsdGeom.Mesh(mesh_prim)
            new_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
            if path == target_path:
                self.assertGreater(new_face_count, original_face_counts[path])
            else:
                self.assertEqual(new_face_count, original_face_counts[path])

    async def test_dice_all_meshes_when_no_paths(self):
        """Tests that all meshes are diced when no paths are specified."""
        stage = self._open_stage("simpleFourCubes.usda")

        original_face_counts = {}
        for mesh_prim in _get_meshes(stage):
            path = str(mesh_prim.GetPath())
            mesh = UsdGeom.Mesh(mesh_prim)
            original_face_counts[path] = len(mesh.GetFaceVertexCountsAttr().Get())

        args = DEFAULT_ARGS.copy()
        args["paths"] = []
        args["splitDices"] = False
        self._execute_command(args)

        for mesh_prim in _get_meshes(stage):
            path = str(mesh_prim.GetPath())
            mesh = UsdGeom.Mesh(mesh_prim)
            new_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
            self.assertGreater(new_face_count, original_face_counts[path])

    async def test_dice_large_cell_no_effect(self):
        """Tests that a grid cell much larger than the mesh produces no dicing."""
        cube_path = "/World/Cube"
        stage = self._open_stage("simpleCube.usda")

        mesh = UsdGeom.Mesh(stage.GetPrimAtPath(cube_path))
        original_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertEqual(original_face_count, 6)

        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = False
        args["gridCellX"] = 10000.0
        args["gridCellY"] = 10000.0
        args["gridCellZ"] = 10000.0
        args["gridOriginX"] = -5000.0
        args["gridOriginY"] = -5000.0
        args["gridOriginZ"] = -5000.0
        self._execute_command(args)

        prim = stage.GetPrimAtPath(cube_path)
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())

        mesh = UsdGeom.Mesh(prim)
        new_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertEqual(new_face_count, original_face_count)

    async def test_dice_grid_origin_affects_result(self):
        """Tests that the grid origin shifts where cuts are placed.

        The cube spans -50..50 on all axes (6 faces).  Cell size is held
        constant at 55 across both scenarios so that any difference in
        face count is attributable solely to the origin shift.

        Origin (0,0,0):   grid lines at ..., -55, 0, 55, ...
          -> x=0 is the only interior cut per axis (1 cut/axis)
        Origin (25,25,25): grid lines at ..., -30, 25, 80, ...
          -> both -30 and 25 are interior cuts per axis (2 cuts/axis)
        """
        cube_path = "/World/Cube"

        # Scenario 1 — default origin (0,0,0), cell size 55
        stage = self._open_stage("simpleCube.usda")
        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = False
        self._execute_command(args)
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath(cube_path))
        face_count_default_origin = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertGreater(face_count_default_origin, 6, "Default origin should produce cuts")

        # Scenario 2 — shifted origin (25,25,25), same cell size 55
        stage = self._open_stage("simpleCube.usda")
        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = False
        args["gridOriginX"] = 25.0
        args["gridOriginY"] = 25.0
        args["gridOriginZ"] = 25.0
        self._execute_command(args)
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath(cube_path))
        face_count_shifted_origin = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertGreater(face_count_shifted_origin, 6, "Shifted origin should also produce cuts")

        self.assertNotEqual(
            face_count_default_origin,
            face_count_shifted_origin,
            "Different origins with the same cell size must produce different face counts",
        )

    async def test_dice_irregular_grid_single_cut(self):
        """Tests dicing with an irregular grid using a single cut height on one axis."""
        cube_path = "/World/Cube"
        stage = self._open_stage("simpleCube.usda")

        mesh = UsdGeom.Mesh(stage.GetPrimAtPath(cube_path))
        original_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertEqual(original_face_count, 6)

        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = False
        args["gridType"] = GRID_TYPE_IRREGULAR
        args["cutHeightsX"] = "0"
        self._execute_command(args)

        prim = stage.GetPrimAtPath(cube_path)
        self.assertTrue(prim.IsValid())

        mesh = UsdGeom.Mesh(prim)
        new_face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertGreater(new_face_count, original_face_count)

    async def test_dice_irregular_grid_with_split(self):
        """Tests that irregular grid dicing with splitDices=True creates separate prims."""
        cube_path = "/World/Cube"
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = True
        args["gridType"] = GRID_TYPE_IRREGULAR
        args["cutHeightsX"] = "0"
        self._execute_command(args)

        # Original cube should no longer be valid
        prim = stage.GetPrimAtPath(cube_path)
        self.assertFalse(prim.IsValid())

        # Cutting at x=0 splits the cube into 2 halves
        parts = _get_meshes(stage)
        self.assertEqual(len(parts), 2)

        # Verify the X extents of each half match the expected cut boundary
        expected_x_ranges = [(-50, 0), (0, 50)]
        actual_x_ranges = sorted(
            (float(p.GetAttribute("extent").Get()[0][0]), float(p.GetAttribute("extent").Get()[1][0])) for p in parts
        )
        for actual, expected in zip(actual_x_ranges, expected_x_ranges, strict=True):
            self.assertAlmostEqual(actual[0], expected[0], places=1)
            self.assertAlmostEqual(actual[1], expected[1], places=1)

    async def test_dice_irregular_grid_multiple_cuts(self):
        """Tests dicing with an irregular grid using multiple cut heights on one axis."""
        cube_path = "/World/Cube"

        # Single cut at x=0 as baseline
        stage = self._open_stage("simpleCube.usda")
        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = False
        args["gridType"] = GRID_TYPE_IRREGULAR
        args["cutHeightsX"] = "0"
        self._execute_command(args)
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath(cube_path))
        single_cut_face_count = len(mesh.GetFaceVertexCountsAttr().Get())

        # Three cuts at -25, 0, 25 should produce strictly more faces
        stage = self._open_stage("simpleCube.usda")
        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = False
        args["gridType"] = GRID_TYPE_IRREGULAR
        args["cutHeightsX"] = "-25 0 25"
        self._execute_command(args)
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath(cube_path))
        multi_cut_face_count = len(mesh.GetFaceVertexCountsAttr().Get())

        self.assertGreater(multi_cut_face_count, single_cut_face_count)

    async def test_dice_irregular_grid_multiple_cuts_with_split(self):
        """Tests dicing with multiple irregular cuts and splitting into parts."""
        cube_path = "/World/Cube"
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args["paths"] = [cube_path]
        args["splitDices"] = True
        args["gridType"] = GRID_TYPE_IRREGULAR
        args["cutHeightsX"] = "-25 0 25"
        self._execute_command(args)

        # Original cube should no longer be valid
        prim = stage.GetPrimAtPath(cube_path)
        self.assertFalse(prim.IsValid())

        # 3 cuts at -25, 0, 25 on a cube from -50 to 50 creates 4 slices
        parts = _get_meshes(stage)
        self.assertEqual(len(parts), 4)

        # Verify the X extents of each slice match the expected cut boundaries
        expected_x_ranges = [(-50, -25), (-25, 0), (0, 25), (25, 50)]
        actual_x_ranges = sorted(
            (float(p.GetAttribute("extent").Get()[0][0]), float(p.GetAttribute("extent").Get()[1][0])) for p in parts
        )
        for actual, expected in zip(actual_x_ranges, expected_x_ranges, strict=True):
            self.assertAlmostEqual(actual[0], expected[0], places=1)
            self.assertAlmostEqual(actual[1], expected[1], places=1)
