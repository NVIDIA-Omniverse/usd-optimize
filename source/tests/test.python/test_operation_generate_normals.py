# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_context, _get_meshes

DEFAULT_ARGS = {
    "paths": [],
    "binding": 0,
    "existingNormals": 1,
    "weightMode": 0,
    "sharpnessAngle": 60.0,
    "gpuThreshold": 500000,
}


class Test_Operation_Generate_Normals(Test_Operation):

    OPERATION = "generateNormals"

    async def test_generate_corner_normals(self):
        """Test generate corner normals"""
        stage = self._open_stage("normalsTest.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 2)

        old_name = []
        old_interpolation = []
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                old_name.append("primvars:normals")
                old_interpolation.append(primvar.GetInterpolation())
            elif prim.GetAttibute("normals"):
                old_name.append("normals")
                old_interpolation.append(mesh.GetNormalsInterpolation())
            else:
                old_name.append(None)
                old_interpolation.append(None)

        context = _get_context(stage)

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        expected_interpolation = UsdGeom.Tokens.faceVarying

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                self.assertEqual(old_name[i], "primvars:normals")
                interpolation = primvar.GetInterpolation()
                normals = primvar.Get()
                if primvar.IsIndexed():
                    normals_len = len(primvar.GetIndicesAttr().Get())
                else:
                    normals_len = len(normals)
            elif normals_attr:
                self.assertEqual(old_name[i], "normals")
                interpolation = mesh.GetNormalsInterpolation()
                normals_len = len(normals_attr.Get())
            else:
                self.fail("Expected either normals or primvars:normals")
            self.assertEqual(interpolation, expected_interpolation)
            self.assertEqual(normals_len, len(mesh.GetFaceVertexIndicesAttr().Get()))

    async def test_generate_face_normals(self):
        stage = self._open_stage("normalsTest.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 2)

        old_name = []
        old_interpolation = []
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                old_name.append("primvars:normals")
                old_interpolation.append(primvar.GetInterpolation())
            elif prim.GetAttibute("normals"):
                old_name.append("normals")
                old_interpolation.append(mesh.GetNormalsInterpolation())
            else:
                old_name.append(None)
                old_interpolation.append(None)

        context = _get_context(stage, verbose=False)

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["binding"] = 1
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        expected_interpolation = UsdGeom.Tokens.uniform

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                self.assertEqual(old_name[i], "primvars:normals")
                interpolation = primvar.GetInterpolation()
                normals = primvar.Get()
                if primvar.IsIndexed():
                    normals_len = len(primvar.GetIndicesAttr().Get())
                else:
                    normals_len = len(normals)
            elif normals_attr:
                self.assertEqual(old_name[i], "normals")
                interpolation = mesh.GetNormalsInterpolation()
                normals_len = len(normals_attr.Get())
            else:
                self.fail("Expected either normals or primvars:normals")
            self.assertEqual(interpolation, expected_interpolation)
            self.assertEqual(normals_len, len(mesh.GetFaceVertexCountsAttr().Get()))

    async def test_generate_vertex_normals(self):
        stage = self._open_stage("normals_options.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_name = []
        old_interpolation = []
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                old_name.append("primvars:normals")
                old_interpolation.append(primvar.GetInterpolation())
            elif normals_attr.IsAuthored():
                old_name.append("normals")
                old_interpolation.append(mesh.GetNormalsInterpolation())
            else:
                old_name.append(None)
                old_interpolation.append(None)

        context = _get_context(stage, verbose=False)

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["binding"] = 2
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        expected_interpolation = UsdGeom.Tokens.varying

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                if old_name[i] is not None:
                    self.assertEqual(old_name[i], "primvars:normals")
                interpolation = primvar.GetInterpolation()
                if primvar.IsIndexed():
                    normals_len = len(primvar.GetIndicesAttr().Get())
                else:
                    normals_len = len(primvar.Get())
            elif normals_attr:
                self.assertEqual(old_name[i], "normals")
                interpolation = mesh.GetNormalsInterpolation()
                normals_len = len(normals_attr.Get())
            else:
                self.fail("Expected either normals or primvars:normals")
            self.assertEqual(interpolation, expected_interpolation)
            self.assertEqual(normals_len, len(mesh.GetPointsAttr().Get()))

    async def test_generate_auto_normals_fix_existing(self):
        stage = self._open_stage("normals_options.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_name = []
        old_size = []
        old_interpolation = []
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                old_name.append("primvars:normals")
                interp = primvar.GetInterpolation()
                # The generated vertex normals are always given "varying" interpolation.
                # Save 'vertex' interpolation as 'varying' so that they match when we compare later.
                if interp == UsdGeom.Tokens.vertex:
                    interp = UsdGeom.Tokens.varying
                old_interpolation.append(interp)
                if primvar.IsIndexed():
                    old_size.append(len(primvar.GetIndicesAttr().Get()))
                else:
                    old_size.append(len(primvar.Get()))
            elif normals_attr.IsAuthored():
                old_name.append("normals")
                normals_attr_values = normals_attr.Get()
                interp = mesh.GetNormalsInterpolation()
                if interp == UsdGeom.Tokens.vertex:
                    interp = UsdGeom.Tokens.varying
                old_interpolation.append(interp)
                if normals_attr_values:
                    old_size.append(len(normals_attr_values))
                else:
                    old_size.append(0)
            else:
                old_name.append(None)
                old_interpolation.append(None)
                old_size.append(0)

        context = _get_context(stage, verbose=False)

        expected_interpolation = UsdGeom.Tokens.varying

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["binding"] = 3
        args["existingNormals"] = 0
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                if old_name[i] is not None:
                    self.assertEqual(old_name[i], "primvars:normals")
                interpolation = primvar.GetInterpolation()
                if primvar.IsIndexed():
                    size = len(primvar.GetIndicesAttr().Get())
                else:
                    size = len(primvar.Get())
                if old_name[i] is None:
                    if interpolation == UsdGeom.Tokens.faceVarying:
                        self.assertEqual(size, len(mesh.GetFaceVertexIndicesAttr().Get()))
                    elif interpolation == UsdGeom.Tokens.varying:
                        self.assertEqual(size, len(mesh.GetPointsAttr().Get()))
                    else:
                        self.assertEqual(interpolation, UsdGeom.Tokens.uniform)
                        self.assertEqual(size, len(mesh.GetFaceVertexCountsAttr().Get()))
                else:
                    self.assertEqual(old_size[i], size)
                    self.assertEqual(old_interpolation[i], interpolation)
            elif normals_attr:
                self.assertEqual(old_name[i], "normals")
                normals_attr_values = normals_attr.Get()
                interpolation = mesh.GetNormalsInterpolation()
                size = len(normals_attr_values)
                self.assertEqual(old_size[i], size)
                self.assertEqual(old_interpolation[i], interpolation)
            else:
                self.fail("Expected either normals or primvars:normals")

    async def test_generate_corner_normals_gpu(self):
        stage = self._open_stage("normals_options.usda")
        mesh_prims = _get_meshes(stage)
        self.assertEqual(len(mesh_prims), 10)

        old_name = []
        old_size = []
        old_interpolation = []
        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                old_name.append("primvars:normals")
                old_interpolation.append(primvar.GetInterpolation())
                if primvar.IsIndexed():
                    old_size.append(len(primvar.GetIndicesAttr().Get()))
                else:
                    old_size.append(len(primvar.Get()))
            elif normals_attr.IsAuthored():
                old_name.append("normals")
                normals_attr_values = normals_attr.Get()
                old_interpolation.append(mesh.GetNormalsInterpolation())
                if normals_attr_values:
                    old_size.append(len(normals_attr_values))
                else:
                    old_size.append(0)
            else:
                old_name.append(None)
                old_interpolation.append(None)
                old_size.append(0)

        context = _get_context(stage, verbose=False)

        expected_interpolation = UsdGeom.Tokens.faceVarying

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["binding"] = 0
        args["gpuThreshold"] = 0
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        for i, prim in enumerate(mesh_prims):
            mesh = UsdGeom.Mesh(prim)
            normals_attr = mesh.GetNormalsAttr()
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
            if primvar:
                if old_name[i] is not None:
                    self.assertEqual(old_name[i], "primvars:normals")
                interpolation = primvar.GetInterpolation()
                if primvar.IsIndexed():
                    size = len(primvar.GetIndicesAttr().Get())
                else:
                    size = len(primvar.Get())
                self.assertEqual(size, len(mesh.GetFaceVertexIndicesAttr().Get()))
                self.assertEqual(interpolation, expected_interpolation)
            elif normals_attr:
                self.assertEqual(old_name[i], "normals")
                normals_attr_values = normals_attr.Get()
                interpolation = mesh.GetNormalsInterpolation()
                size = len(normals_attr_values)
                self.assertEqual(size, len(mesh.GetFaceVertexIndicesAttr().Get()))
                self.assertEqual(interpolation, expected_interpolation)
            else:
                self.fail("Expected either normals or primvars:normals")
