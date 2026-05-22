# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_context, _get_meshes

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "gpuVertexCountThreshold": 1000000,
}


class Test_Operation_TriangulateMeshes(Test_Operation):

    OPERATION = "triangulateMeshes"

    async def test_triangulate_mesh(self):
        """Test triangulate meshes on a quad mesh"""

        stage = self._open_stage("simpleCube.usda")
        before_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(before_meshes), 1)
        before_mesh = UsdGeom.Mesh(before_meshes[0])
        self.assertTrue(before_mesh)
        before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
        self.assertEqual(len(before_face_sizes), 6)
        for face_size in before_face_sizes:
            self.assertEqual(face_size, 4)

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        after_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(after_meshes), 1)

        # Assert that the quads have been turned into triangles
        after_mesh = UsdGeom.Mesh(after_meshes[0])
        self.assertTrue(after_mesh)
        after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
        self.assertEqual(len(after_face_sizes), 12)
        for face_size in after_face_sizes:
            self.assertEqual(face_size, 3)

    async def test_triangulate_mesh_gpu(self):
        """Test triangulate meshes on a quad mesh with gpu"""
        stage = self._open_stage("simpleCube.usda")
        before_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(before_meshes), 1)
        before_mesh = UsdGeom.Mesh(before_meshes[0])
        self.assertTrue(before_mesh)
        before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
        self.assertEqual(len(before_face_sizes), 6)
        for face_size in before_face_sizes:
            self.assertEqual(face_size, 4)
        args = DEFAULT_ARGS.copy()
        args["gpuVertexCountThreshold"] = 1  # forces the gpu algo to be chosen
        success, result = self._execute_command(args)
        # The operation should execute successfully.
        self.assertTrue(success)
        after_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(after_meshes), 1)
        # Assert that the quads have been turned into triangles
        after_mesh = UsdGeom.Mesh(after_meshes[0])
        self.assertTrue(after_mesh)
        after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
        self.assertEqual(len(after_face_sizes), 12)
        for face_size in after_face_sizes:
            self.assertEqual(face_size, 3)

    async def test_triangulate_all_mesh_prims(self):
        """Test triangulate all mesh prims"""

        stage = self._open_stage("simpleFourCubes.usda")
        before_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(before_meshes), 4)
        for before_mesh in before_meshes:
            self.assertTrue(before_mesh)
            before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
            self.assertEqual(len(before_face_sizes), 6)
            for face_size in before_face_sizes:
                self.assertEqual(face_size, 4)

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        after_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(after_meshes), 4)

        # Assert that the quads have been turned into triangles
        for after_mesh in after_meshes:
            self.assertTrue(after_mesh)
            after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
            self.assertEqual(len(after_face_sizes), 12)
            for face_size in after_face_sizes:
                self.assertEqual(face_size, 3)

    async def test_triangulate_one_mesh_prim(self):
        """Test triangulate specific mesh prims"""

        stage = self._open_stage("simpleFourCubes.usda")
        before_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(before_meshes), 4)
        for before_mesh in before_meshes:
            self.assertTrue(before_mesh)
            before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
            self.assertEqual(len(before_face_sizes), 6)
            for face_size in before_face_sizes:
                self.assertEqual(face_size, 4)

        args = DEFAULT_ARGS.copy()
        args["paths"] = [str(before_meshes[2].GetPath())]
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        after_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
        self.assertEqual(len(after_meshes), 4)

        # Assert that only the specified mesh has been triangulated
        for after_mesh in after_meshes:
            self.assertTrue(after_mesh)
            after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
            if str(after_mesh.GetPath()) == args["paths"][0]:
                self.assertEqual(len(after_face_sizes), 12)
                for face_size in after_face_sizes:
                    self.assertEqual(face_size, 3)
            else:
                self.assertEqual(len(after_face_sizes), 6)
                for face_size in after_face_sizes:
                    self.assertEqual(face_size, 4)

    async def test_time_varying_meshes(self):
        """Test triangulate meshes operation on meshes with authored time varying attributes"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

    async def test_time_varying_meshes_paths(self):
        """Test triangulate meshes operation on meshes with authored time varying attributes using paths"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # settings paths so that _removePrimsWithAuthoredTimeSamples is invoked
        args["paths"] = [
            "/Additional_Assets/sm_warehousecomposition_n32_01/sm_palletcomposition_a52_04/SM_LongBox_A10_58/SM_LongBox_A10_Body_01",
            "/Additional_Assets/sm_warehousecomposition_n32_01/sm_palletcomposition_a52_04/SM_LongBox_A10_58/SM_LongBox_A10_Decal_01",
            "/Additional_Assets/sm_warehousecomposition_n32_01/sm_palletcomposition_a52_04/SM_LongBox_A10_58/SM_LongBox_A10_Scotch_01",
        ]
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)
