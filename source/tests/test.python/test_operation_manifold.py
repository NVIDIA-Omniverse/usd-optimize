# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_meshes

DEFAULT_ARGS = {
    "paths": [],
}


class Test_Operation_Manifold(Test_Operation):

    OPERATION = "manifoldMeshes"

    async def test_manifold_host(self):
        """Test manifold"""
        stage = self._open_stage("two-cubes-nonmanifold.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 1)

        old_face_counts = []
        old_vertex_counts = []
        for prim in mesh_prims:
            mesh = UsdGeom.Mesh(prim)
            old_face_counts.append(len(mesh.GetFaceVertexCountsAttr().Get()))
            old_vertex_counts.append(len(mesh.GetPointsAttr().Get()))

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Assert that the manifold op has taken place
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            self.assertEqual(old_face_counts[i], len(mesh.GetFaceVertexCountsAttr().Get()))
            self.assertLess(old_vertex_counts[i], len(mesh.GetPointsAttr().Get()))
