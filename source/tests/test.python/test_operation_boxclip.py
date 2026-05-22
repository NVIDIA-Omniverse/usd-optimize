# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_meshes

# clipping options
DEF_AABB = 0
DEF_PRIM = 1

KEEP_INSIDE = 0
KEEP_OUTSIDE = 1

CLIP_INSIDE_KEEP = 0
CLIP_INSIDE_CUT = 1
CLIP_INSIDE_DISCARD = 2
CLIP_OUTSIDE_KEEP = 3
CLIP_OUTSIDE_DISCARD = 4

DEFAULT_ARGS = {
    "paths": [],
    "clipBoxDef": DEF_PRIM,
    "minX": 0.0,
    "maxX": 0.0,
    "minY": 0.0,
    "maxY": 0.0,
    "minZ": 0.0,
    "maxZ": 0.0,
    "clipBoxPrimPath": "",
    "ignoreClipBoxSide": 0,
    "partialIntersections": 0,
    "keepGeometry": KEEP_INSIDE,
}


class Test_Operation_BoxClip(Test_Operation):

    OPERATION = "boxClip"

    async def test_boxclip_host(self):
        """Test boxClip, all clipped meshes should have some parts left"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.0
        args["maxX"] = 5.0
        args["minY"] = 0.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 8.0
        args["partialIntersections"] = 1  # Cut Mesh
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            self.assertLess(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])

    async def test_boxclip_empty_clipbox(self):
        """Test boxClip with empty clipbox"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Everything should be gone
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            self.assertFalse(mesh)

    async def test_boxclip_large_clipbox(self):
        """Test boxClip with large clipbox"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -10000000.0
        args["maxX"] = 10000000.0
        args["minY"] = -10000000.0
        args["maxY"] = 10000000.0
        args["minZ"] = -10000000.0
        args["maxZ"] = 10000000.0

        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Everything should still remain
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            self.assertTrue(mesh)
            self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])

    async def test_boxclip_some_outside_keep_intersection(self):
        """Test boxClip, only some meshes should remain"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["partialIntersections"] = 1  # Cut Mesh
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/noXform1",
            "/World/noXform2",
            "/World/xform1",
            "/World/xform2",
            "/World/sameXform1",
            "/World/sameXform2",
        ]

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_remaining_prim_paths:
                    self.assertLessEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_remaining_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_some_outside_keep(self):
        """Test boxClip, only some meshes should remain"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["partialIntersections"] = 0  # keep entire prim
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/noXform1",
            "/World/noXform2",
            "/World/xform1",
            "/World/xform2",
            "/World/sameXform1",
            "/World/sameXform2",
        ]

        expected_partially_clipped = [
            "/World/xform1",
            "/World/xform2",
            "/World/sameXform1",
            "/World/sameXform2",
        ]

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_remaining_prim_paths:
                    if prim.GetPath() in expected_partially_clipped:
                        self.assertLessEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                    else:
                        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_remaining_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_some_outside_discard(self):
        """Test boxClip, only some meshes should remain"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["partialIntersections"] = 2  # discard entire prim
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/sameXform1",
            "/World/noXform2",
        ]

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_remaining_prim_paths:
                    self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_remaining_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_clip_prim(self):
        """Test boxClip, clip prim"""
        stage = self._open_stage("boxclipTest.usda")
        mesh_prims = _get_meshes(stage)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_PRIM
        args["clipBoxPrimPath"] = "/World/ClipboxPrim"
        args["partialIntersections"] = 1  # Cut Mesh

        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_clipped_prim_paths = [
            "/World/Cube",
            "/World/Cube_01",
            "/World/Cube_02",
            "/World/Cube_03",
        ]

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_clipped_prim_paths:
                    self.assertLess(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                elif prim.GetPath() == "/World/ClipboxPrim":
                    self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_clipped_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_invalid_clip_prim(self):
        """Test boxClip, invalid clip prim"""
        stage = self._open_stage("boxclipTest.usda")
        mesh_prims = _get_meshes(stage)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_PRIM
        args["clipBoxPrimPath"] = "/World/ClipboxPrimINVALID"

        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
            else:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_empty_clip_prim(self):
        """Test boxClip, clip prim"""
        stage = self._open_stage("boxclipTest.usda")
        mesh_prims = _get_meshes(stage)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_PRIM

        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_clipped_prim_paths = [
            "/World/Cube",
            "/World/Cube_01",
            "/World/Cube_02",
            "/World/Cube_03",
            "/World/Cube_04",
            "/World/Cube_05",
            "/World/Cube_06",
            "/World/Cube_07",
            "/World/Cube_08",
            "/World/Cube_09",
            "/World/Cube_10",
            "/World/Cube_11",
            "/World/ClipboxPrim",
        ]

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_clipped_prim_paths:
                    self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_clipped_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_clip_prim_ignore_side(self):
        """Test boxClip, clip prim while ignoring one side of the clip box"""
        stage = self._open_stage("boxclipTest.usda")
        mesh_prims = _get_meshes(stage)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_PRIM
        args["clipBoxPrimPath"] = "/World/ClipboxPrim"
        args["ignoreClipBoxSide"] = 2  # +X side
        args["partialIntersections"] = 1  # Cut Mesh

        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_clipped_prim_paths = [
            "/World/Cube",
            "/World/Cube_01",
            "/World/Cube_02",
            "/World/Cube_03",
            "/World/Cube_08",
            "/World/Cube_09",
            "/World/ClipboxPrim",
        ]

        # Parts have been clipped away
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_clipped_prim_paths:
                    self.assertLessEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                elif prim.GetPath() == "/World/ClipboxPrim":
                    self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_clipped_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_min_greater_than_max(self):
        """Test boxClip validation that min values must be less than max values"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        # Test case: minX > maxX
        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = 5.0
        args["maxX"] = -5.0
        args["minY"] = 0.0
        args["maxY"] = 1.0
        args["minZ"] = 0.0
        args["maxZ"] = 1.0

        success, result = self._execute_command(args)
        # The operation should fail - first field in result is the status
        self.assertFalse(result[0])
        self.assertIn("Clip min values must be ≤ max values", result[1])

    async def test_boxclip_scene_graph_instances(self):
        """Test boxClip, clip prim with instanced geometry"""
        stage = self._open_stage("boxclipInstanceTest.usda")
        before_mesh_prims = _get_meshes(stage)

        old_face_counts = []
        for prim in before_mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_PRIM
        args["clipBoxPrimPath"] = "/World/ClipBox"
        args["partialIntersections"] = 1  # Cut Mesh

        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_clipped_prim_paths = [
            "/World/Cube/Mesh",
            "/World/Cube_04_inside/Mesh",
            "/World/Cube_01_partial/Mesh",
            "/World/ClipBox",
            "/World/Cube_05_partial",
            "/World/Cube_06_nested_partial/Mesh",
            "/World/Cube_07_partial/Mesh",
            "/World/Cube_09_inside/Mesh",
            "/World/ManyCubes/Xform/Cube",
            "/World/ManyCubes_partial/Cube",
            "/World/ManyCubes_partial/Cube_01",
            "/World/ManyCubes_partial/Xform/Cube",
        ]

        # need to fetch meshes again, due to flattening of the instances
        after_mesh_prims = _get_meshes(stage)

        # Parts have been clipped away
        for i, prim in enumerate(after_mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_clipped_prim_paths:
                    if prim.GetPath() == "/World/ClipBox":
                        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                    elif (
                        prim.GetPath() == "/World/Cube_04_inside/Mesh" or prim.GetPath() == "/World/Cube_09_inside/Mesh"
                    ):
                        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), 6)
                    elif prim not in before_mesh_prims:
                        self.assertLess(
                            len(mesh.GetFaceVertexCountsAttr().Get()), 6, f"{prim.GetPath()} should be clipped"
                        )
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_clipped_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_external_instance(self):
        """Test boxClip, clip prim with external instanced geometry"""
        stage = self._open_stage("boxclipExternalInstanceTest.usda")
        before_mesh_prims = _get_meshes(stage)

        old_face_counts = []
        for prim in before_mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_PRIM
        args["clipBoxPrimPath"] = "/World/ClipBox"
        args["partialIntersections"] = 1  # Cut Mesh

        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        expected_clipped_prim_paths = [
            "/World/ClipBox",
            "/World/Xform/TopScope/Xform/InnerScope/Xform/Xform/Cube",
            "/World/Xform/TopScope/Xform/InnerScope/Xform/Xform/Cube_01",
            "/World/Xform/TopScope/Xform/InnerScope/Xform/Xform/Cube_05",
            "/World/Xform/TopScope/Xform/InnerScope/Xform/Xform/Cube_06",
        ]

        # need to fetch meshes again, due to flattening of the instances
        after_mesh_prims = _get_meshes(stage)

        # Parts have been clipped away
        for i, prim in enumerate(after_mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_clipped_prim_paths:
                    if prim.GetPath() == "/World/ClipBox":
                        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                    elif prim.GetPath() == "/World/Xform/TopScope/Xform/InnerScope/Xform/Xform/Cube_05":
                        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), 6)
                    elif prim not in before_mesh_prims:
                        self.assertLess(
                            len(mesh.GetFaceVertexCountsAttr().Get()), 6, f"{prim.GetPath()} should be clipped"
                        )
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_clipped_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_boxclip_invert_large_clipbox(self):
        """Test boxClip with keepGeometry=Outside and large clipbox - all meshes inside should be removed"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -10000000.0
        args["maxX"] = 10000000.0
        args["minY"] = -10000000.0
        args["maxY"] = 10000000.0
        args["minZ"] = -10000000.0
        args["maxZ"] = 10000000.0
        args["partialIntersections"] = 2  # Discard (Cut Mesh not supported with Outside mode)
        args["keepGeometry"] = KEEP_OUTSIDE

        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Everything should be gone (all meshes were inside the box)
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            self.assertFalse(mesh)

    async def test_boxclip_invert_empty_clipbox(self):
        """Test boxClip with keepGeometry=Outside and empty clipbox - all meshes should remain"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["partialIntersections"] = 2  # Discard (Cut Mesh not supported with Outside mode)
        args["keepGeometry"] = KEEP_OUTSIDE

        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Everything should still remain (empty box means nothing to cull)
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            self.assertTrue(mesh)
            self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])

    async def test_boxclip_invert_some_outside_keep(self):
        """Test boxClip with keepGeometry=Outside and Keep mode - meshes outside box should remain unchanged"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["partialIntersections"] = 0  # keep entire prim
        args["keepGeometry"] = KEEP_OUTSIDE

        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # With keepGeometry=Outside and Keep mode:
        # - Meshes fully outside the box should remain unchanged
        # - Meshes fully inside the box should be removed (emptied)
        # - Meshes partially intersecting should remain unchanged (Keep mode)
        # The parentXform prims are outside (Z translate of 7 puts them beyond clip box Z range)
        # Prims at Z boundary (-1.5 to -0.5 vs clip Z -1.0) are partially intersecting
        expected_remaining_prim_paths = [
            "/World/xform1",  # partially intersecting
            "/World/xform2",  # partially intersecting (Z boundary)
            "/World/parentXform/noXform1",  # fully outside
            "/World/parentXform/noXform2",  # fully outside
            "/World/parentXform/xform1",  # fully outside
            "/World/parentXform/xform2",  # fully outside
            "/World/noXform1",  # partially intersecting (Z boundary)
            "/World/sameXform2",  # partially intersecting (Z boundary)
        ]

        for i, prim in enumerate(mesh_prims):
            prim_path = str(prim.GetPath())
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if prim_path in expected_remaining_prim_paths:
                    # Mesh should remain unchanged (outside or partially intersecting)
                    self.assertEqual(len(face_counts), old_face_counts[i], f"{prim_path} should remain unchanged")
                else:
                    # Fully inside the clip box — should be emptied in Keep Outside + Keep mode
                    self.assertEqual(len(face_counts), 0, f"{prim_path} should be removed (fully inside the clipbox)")

    async def test_boxclip_invert_some_outside_discard(self):
        """Test boxClip with keepGeometry=Outside and Discard mode - meshes touching box should be removed"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["partialIntersections"] = 2  # discard entire prim
        args["keepGeometry"] = KEEP_OUTSIDE

        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Meshes completely outside the box should remain (these are under parentXform which has Z translate of 7)
        # Meshes touching or inside the box should be removed (have 0 face counts)
        expected_remaining_prim_paths = [
            "/World/parentXform/noXform1",
            "/World/parentXform/noXform2",
            "/World/parentXform/xform1",
            "/World/parentXform/xform2",
        ]

        for i, prim in enumerate(mesh_prims):
            prim_path = str(prim.GetPath())
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if prim_path in expected_remaining_prim_paths:
                    # These should remain unchanged (outside the clip box)
                    self.assertEqual(len(face_counts), old_face_counts[i], f"{prim_path} should remain unchanged")
                else:
                    # These should be emptied (inside or touching the clip box)
                    self.assertEqual(
                        len(face_counts), 0, f"{prim_path} should be removed (touches or inside the clipbox)"
                    )

    async def test_boxclip_invert_cut_mesh_falls_back_to_keep(self):
        """Test that keepGeometry=Outside with Cut Mesh falls back to Keep mode"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["partialIntersections"] = 1  # Cut Mesh, should fall back to Keep
        args["keepGeometry"] = KEEP_OUTSIDE

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        # Should behave identically to test_boxclip_invert_some_outside_keep
        expected_remaining_prim_paths = [
            "/World/xform1",
            "/World/xform2",
            "/World/parentXform/noXform1",
            "/World/parentXform/noXform2",
            "/World/parentXform/xform1",
            "/World/parentXform/xform2",
            "/World/noXform1",
            "/World/sameXform2",
        ]

        for i, prim in enumerate(mesh_prims):
            prim_path = str(prim.GetPath())
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if prim_path in expected_remaining_prim_paths:
                    self.assertEqual(len(face_counts), old_face_counts[i], f"{prim_path} should remain unchanged")
                else:
                    self.assertEqual(len(face_counts), 0, f"{prim_path} should be removed (fully inside the clipbox)")

    async def test_boxclip_cube_grid_normal(self):
        """Test boxClip on cube grid scene - normal mode keeps geometry inside the box"""
        stage = self._open_stage("cube_grid_scene_meshes.usdc")
        mesh_prims = _get_meshes(stage)
        # 1000 grid cubes + 1 LargeCube = 1001 meshes
        self.assertEqual(len(mesh_prims), 1001)

        # Count meshes with face data before
        meshes_with_faces_before = 0
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            if len(mesh.GetFaceVertexCountsAttr().Get()) > 0:
                meshes_with_faces_before += 1
        self.assertEqual(meshes_with_faces_before, 1001)

        # Clip box slightly smaller than LargeCube (scale 9.2, 10, 10.8 → bounds
        # ~[-4.6, 4.6] x [-5.0, 5.0] x [-5.4, 5.4]) so LargeCube is clearly
        # partially intersecting and gets discarded.
        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -4.5
        args["maxX"] = 4.5
        args["minY"] = -4.9
        args["maxY"] = 4.9
        args["minZ"] = -5.3
        args["maxZ"] = 5.3
        args["partialIntersections"] = 2  # discard partial intersections
        args["keepGeometry"] = KEEP_INSIDE

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        # Calculate expected results:
        # Grid cubes are 1x1x1 at centers from -9 to 9 with step 2: -9,-7,-5,-3,-1,1,3,5,7,9
        # A cube at position p has bounds [p-0.5, p+0.5]
        #
        # For X (clip: [-4.5, 4.5]):
        #   Fully inside (p-0.5 >= -4.5 AND p+0.5 <= 4.5): -3,-1,1,3 (4 positions)
        # For Y (clip: [-4.9, 4.9]):
        #   Fully inside: -3,-1,1,3 (4 positions)
        # For Z (clip: [-5.3, 5.3]):
        #   Fully inside: -3,-1,1,3 (4 positions)
        #
        # Cubes fully inside clip box = 4 * 4 * 4 = 64 grid cubes
        # LargeCube extends beyond clip box, so it's partially intersecting and discarded.
        # Expected: 64 grid cubes
        expected_remaining = 64

        meshes_with_faces_after = 0
        for prim in mesh_prims:
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if face_counts and len(face_counts) > 0:
                    meshes_with_faces_after += 1

        self.assertEqual(
            meshes_with_faces_after,
            expected_remaining,
            f"Expected {expected_remaining} meshes inside clip box, got {meshes_with_faces_after}",
        )

    async def test_boxclip_cube_grid_inverted(self):
        """Test boxClip on cube grid scene - keepGeometry=Outside keeps geometry outside the box"""
        stage = self._open_stage("cube_grid_scene_meshes.usdc")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 1001)

        # Count meshes with face data before
        meshes_with_faces_before = 0
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            if len(mesh.GetFaceVertexCountsAttr().Get()) > 0:
                meshes_with_faces_before += 1
        self.assertEqual(meshes_with_faces_before, 1001)

        # Same clip box as normal test, but with keepGeometry=Outside
        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -4.5
        args["maxX"] = 4.5
        args["minY"] = -4.9
        args["maxY"] = 4.9
        args["minZ"] = -5.3
        args["maxZ"] = 5.3
        args["partialIntersections"] = 2  # discard partial intersections
        args["keepGeometry"] = KEEP_OUTSIDE

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        # Calculate expected results:
        # Grid cubes are 1x1x1 at centers from -9 to 9 with step 2: -9,-7,-5,-3,-1,1,3,5,7,9
        # A cube is fully outside the clip box if it doesn't intersect in at least one dimension.
        #
        # For X (clip: [-4.5, 4.5]):
        #   Intersects (p+0.5 >= -4.5 AND p-0.5 <= 4.5): -5,-3,-1,1,3,5 (6 positions)
        #   Fully outside X: -9,-7,7,9 (4 positions)
        # For Y (clip: [-4.9, 4.9]):
        #   Intersects: -5,-3,-1,1,3,5 (6 positions)
        # For Z (clip: [-5.3, 5.3]):
        #   Intersects: -5,-3,-1,1,3,5 (6 positions)
        #
        # Cubes that intersect clip box in ALL dimensions = 6 * 6 * 6 = 216
        # Cubes fully outside (don't intersect in at least one dim) = 1000 - 216 = 784
        # LargeCube extends beyond clip box, so it's partially intersecting and discarded.
        # Expected: 784 grid cubes
        expected_remaining = 784

        meshes_with_faces_after = 0
        for prim in mesh_prims:
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if face_counts and len(face_counts) > 0:
                    meshes_with_faces_after += 1

        self.assertEqual(
            meshes_with_faces_after,
            expected_remaining,
            f"Expected {expected_remaining} meshes outside clip box, got {meshes_with_faces_after}",
        )

    # ── clipMode argument tests ──────────────────────────────────────────

    async def test_clipmode_inside_keep(self):
        """clipMode=eInsideKeep: keep geometry inside box, keep partially intersecting prims"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["clipMode"] = CLIP_INSIDE_KEEP

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/noXform1",
            "/World/noXform2",
            "/World/xform1",
            "/World/xform2",
            "/World/sameXform1",
            "/World/sameXform2",
        ]

        expected_partially_clipped = [
            "/World/xform1",
            "/World/xform2",
            "/World/sameXform1",
            "/World/sameXform2",
        ]

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_remaining_prim_paths:
                    if prim.GetPath() in expected_partially_clipped:
                        self.assertLessEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                    else:
                        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_remaining_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_clipmode_inside_cut_mesh(self):
        """clipMode=eInsideCutMesh: keep geometry inside box, cut partially intersecting prims"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["clipMode"] = CLIP_INSIDE_CUT

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/noXform1",
            "/World/noXform2",
            "/World/xform1",
            "/World/xform2",
            "/World/sameXform1",
            "/World/sameXform2",
        ]

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_remaining_prim_paths:
                    self.assertLessEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_remaining_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_clipmode_inside_discard(self):
        """clipMode=eInsideDiscard: keep geometry inside box, discard partially intersecting prims"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["clipMode"] = CLIP_INSIDE_DISCARD

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/sameXform1",
            "/World/noXform2",
        ]

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            if mesh:
                if prim.GetPath() in expected_remaining_prim_paths:
                    self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), old_face_counts[i])
                else:
                    self.fail(f"{prim.GetPath()} should be fully clipped out")
            elif prim.GetPath() in expected_remaining_prim_paths:
                self.fail(f"{prim.GetPath()} should be within the clipbox")

    async def test_clipmode_outside_keep(self):
        """clipMode=eOutsideKeep: keep geometry outside box, keep partially intersecting prims"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["clipMode"] = CLIP_OUTSIDE_KEEP

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/xform1",
            "/World/xform2",
            "/World/parentXform/noXform1",
            "/World/parentXform/noXform2",
            "/World/parentXform/xform1",
            "/World/parentXform/xform2",
            "/World/noXform1",
            "/World/sameXform2",
        ]

        for i, prim in enumerate(mesh_prims):
            prim_path = str(prim.GetPath())
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if prim_path in expected_remaining_prim_paths:
                    self.assertEqual(len(face_counts), old_face_counts[i], f"{prim_path} should remain unchanged")
                else:
                    self.assertEqual(len(face_counts), 0, f"{prim_path} should be removed (fully inside the clipbox)")

    async def test_clipmode_outside_discard(self):
        """clipMode=eOutsideDiscard: keep geometry outside box, discard partially intersecting prims"""
        stage = self._open_stage("xformsSplitSpatial.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_face_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -5.5
        args["maxX"] = 5.5
        args["minY"] = -1.5
        args["maxY"] = 1.0
        args["minZ"] = -1.0
        args["maxZ"] = 5.0
        args["clipMode"] = CLIP_OUTSIDE_DISCARD

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        expected_remaining_prim_paths = [
            "/World/parentXform/noXform1",
            "/World/parentXform/noXform2",
            "/World/parentXform/xform1",
            "/World/parentXform/xform2",
        ]

        for i, prim in enumerate(mesh_prims):
            prim_path = str(prim.GetPath())
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if prim_path in expected_remaining_prim_paths:
                    self.assertEqual(len(face_counts), old_face_counts[i], f"{prim_path} should remain unchanged")
                else:
                    self.assertEqual(
                        len(face_counts), 0, f"{prim_path} should be removed (touches or inside the clipbox)"
                    )

    # ── clipMode / legacy parity tests ───────────────────────────────────

    async def test_clipmode_parity_inside_discard(self):
        """Verify clipMode=eInsideDiscard produces the same result as the equivalent legacy args"""
        stage = self._open_stage("cube_grid_scene_meshes.usdc")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 1001)

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -4.5
        args["maxX"] = 4.5
        args["minY"] = -4.9
        args["maxY"] = 4.9
        args["minZ"] = -5.3
        args["maxZ"] = 5.3
        args["clipMode"] = CLIP_INSIDE_DISCARD

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        # 64 fully-inside grid cubes expected (same as test_boxclip_cube_grid_normal with legacy args)
        meshes_with_faces_after = 0
        for prim in mesh_prims:
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if face_counts and len(face_counts) > 0:
                    meshes_with_faces_after += 1

        self.assertEqual(meshes_with_faces_after, 64)

    async def test_clipmode_parity_outside_discard(self):
        """Verify clipMode=eOutsideDiscard produces the same result as the equivalent legacy args"""
        stage = self._open_stage("cube_grid_scene_meshes.usdc")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 1001)

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -4.5
        args["maxX"] = 4.5
        args["minY"] = -4.9
        args["maxY"] = 4.9
        args["minZ"] = -5.3
        args["maxZ"] = 5.3
        args["clipMode"] = CLIP_OUTSIDE_DISCARD

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        # 784 fully-outside grid cubes expected (same as test_boxclip_cube_grid_inverted with legacy args)
        meshes_with_faces_after = 0
        for prim in mesh_prims:
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if face_counts and len(face_counts) > 0:
                    meshes_with_faces_after += 1

        self.assertEqual(meshes_with_faces_after, 784)

    # ── clipMode overrides legacy args ───────────────────────────────────

    async def test_clipmode_overrides_legacy_args(self):
        """When both clipMode and legacy args are set, clipMode should take precedence"""
        stage = self._open_stage("cube_grid_scene_meshes.usdc")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 1001)

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -4.5
        args["maxX"] = 4.5
        args["minY"] = -4.9
        args["maxY"] = 4.9
        args["minZ"] = -5.3
        args["maxZ"] = 5.3
        # Legacy args say inside+keep (would keep all 1001 since partial prims are kept)
        args["keepGeometry"] = KEEP_INSIDE
        args["partialIntersections"] = 0
        # clipMode says outside+discard (should keep only 784 fully-outside cubes)
        args["clipMode"] = CLIP_OUTSIDE_DISCARD

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        meshes_with_faces_after = 0
        for prim in mesh_prims:
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if face_counts and len(face_counts) > 0:
                    meshes_with_faces_after += 1

        # If clipMode wins: 784 (outside+discard). If legacy wins: many more (inside+keep).
        self.assertEqual(
            meshes_with_faces_after,
            784,
            f"clipMode should override legacy args; expected 784, got {meshes_with_faces_after}",
        )

    async def test_clipmode_legacy_args_honored_when_clipmode_default(self):
        """When clipMode is at its default (eInsideKeep), legacy args should be honored"""
        stage = self._open_stage("cube_grid_scene_meshes.usdc")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 1001)

        args = DEFAULT_ARGS.copy()
        args["clipBoxDef"] = DEF_AABB
        args["minX"] = -4.5
        args["maxX"] = 4.5
        args["minY"] = -4.9
        args["maxY"] = 4.9
        args["minZ"] = -5.3
        args["maxZ"] = 5.3
        # Legacy args: outside + discard
        args["keepGeometry"] = KEEP_OUTSIDE
        args["partialIntersections"] = 2
        # clipMode left at default (eInsideKeep = 0), so legacy should be honored
        args["clipMode"] = CLIP_INSIDE_KEEP

        success, _ = self._execute_command(args)
        self.assertTrue(success)

        meshes_with_faces_after = 0
        for prim in mesh_prims:
            if prim.IsValid():
                mesh = UsdGeom.Mesh(prim)
                face_counts = mesh.GetFaceVertexCountsAttr().Get()
                if face_counts and len(face_counts) > 0:
                    meshes_with_faces_after += 1

        # Legacy outside+discard should yield 784 (the default clipMode guard skips the switch)
        self.assertEqual(
            meshes_with_faces_after,
            784,
            f"Legacy args should be honored when clipMode is default; expected 784, got {meshes_with_faces_after}",
        )
