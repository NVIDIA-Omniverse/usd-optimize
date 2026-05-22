# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import filecmp
import os
import platform

from pxr import UsdGeom

from .test_utils import Test_Operation, _get_meshes, _get_test_data_file_path

DEFAULT_ARGS = {
    "paths": [],
    "gradation": 0,
    "maxError": 0.1,
    "gpuVertexCountThreshold": 500000,
}


class Test_ZZ_Operation_RemeshMeshes(Test_Operation):

    OPERATION = "remeshMeshes"
    WRITE_TEST_RESULTS = False

    async def test_remesh_cpu(self):
        """Test CPU remesh function"""
        stage = self._open_stage("simpleCube.usda")
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 1)

        mesh = UsdGeom.Mesh(prim=stage.GetPrimAtPath("/World/Cube"))
        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), 6)
        self.assertEqual(len(mesh.GetFaceVertexIndicesAttr().Get()), 24)
        self.assertEqual(len(mesh.GetPointsAttr().Get()), 8)

        args = DEFAULT_ARGS.copy()
        args["gradation"] = 0.1
        success, result = self._execute_command(args)

        self.assertTrue(success)

        # Assert remeshed values
        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), 1024)
        self.assertEqual(len(mesh.GetFaceVertexIndicesAttr().Get()), 3072)
        self.assertEqual(len(mesh.GetPointsAttr().Get()), 514)

    async def test_remesh_gpu(self):
        """Test GPU remesh function"""
        stage = self._open_stage("simpleCube.usda")

        args = DEFAULT_ARGS.copy()
        args["gradation"] = 0.1
        args["gpuVertexCountThreshold"] = 0
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        mesh = UsdGeom.Mesh(prim=stage.GetPrimAtPath("/World/Cube"))

        # Assert remeshed values
        self.assertEqual(len(mesh.GetFaceVertexCountsAttr().Get()), 1024)
        self.assertEqual(len(mesh.GetFaceVertexIndicesAttr().Get()), 3072)
        self.assertEqual(len(mesh.GetPointsAttr().Get()), 514)
