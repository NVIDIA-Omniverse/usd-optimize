# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Usd, UsdGeom

from .test_utils import Test_Operation, _get_meshes

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "gpuFaceCountThreshold": 0,
    "vertexTolerance": 0.01,
    "volumeTolerance": 0.01,
    "ignoreNonConstPrimvars": True,
    "ignoreSubsets": True,
    "allowNegativeVolume": True,
    "allowMissingEndcaps": True,
    "fitSphere": True,
    "fitCylinder": True,
    "fitCone": True,
    "fitCube": True,
    "generateMeshes": False,
    "sphereLongitudeDivisions": 32,
    "sphereLatitudeDivisions": 16,
    "cylinderWallDivisions": 32,
    "cylinderLatitudeDivisions": 1,
    "cylinderEndcaps": True,
    "coneSideDivisions": 64,
    "coneLengthDivisions": 3,
    "coneBases": True,
}


def _get_primitive_shapes(stage):
    return [UsdGeom.Gprim(x) for x in stage.TraverseAll() if x.IsA(UsdGeom.Gprim) and not x.IsA(UsdGeom.PointBased)]


def _get_all_gprims(stage):
    return [UsdGeom.Gprim(x) for x in stage.TraverseAll() if x.IsA(UsdGeom.Gprim)]


def _get_meshes_all(stage):
    """Return all meshes in the stage"""
    return [UsdGeom.Mesh(x) for x in stage.Traverse(Usd.PrimIsActive & Usd.PrimIsLoaded) if x.IsA(UsdGeom.Mesh)]


def _get_primitive_shapes_all(stage):
    return [
        UsdGeom.Gprim(x)
        for x in stage.Traverse(Usd.PrimIsActive & Usd.PrimIsLoaded)
        if x.IsA(UsdGeom.Gprim) and not x.IsA(UsdGeom.PointBased)
    ]


class Test_Operation_PrimitiveFit(Test_Operation):

    OPERATION = "fitPrimitives"

    async def test_primitive_fit(self):
        """Test primitive fit"""

        stage = self._open_stage("shapes_baked.usda")

        # Check the meshes in the stage
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 5)
        names = [x.GetPrim().GetName() for x in before_meshes]
        self.assertTrue("Sphere" in names)
        self.assertTrue("Cylinder" in names)
        self.assertTrue("Cone" in names)
        self.assertTrue("Cube" in names)
        self.assertTrue("Torus" in names)

        # Check that there are no prims in the stage
        before_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(before_prims), 0)

        args = DEFAULT_ARGS.copy()
        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # torus should not have been converted
        after_meshes = _get_meshes(stage)
        self.assertEqual(len(after_meshes), 1)
        after_mesh = UsdGeom.Mesh(after_meshes[0])
        self.assertTrue(after_mesh)
        self.assertEqual(after_mesh.GetPrim().GetName(), "Torus")

        # check generated primitives
        after_primitive_shapes = _get_primitive_shapes(stage)
        for gprim in after_primitive_shapes:
            if gprim.GetPrim().GetName() == "Sphere":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Sphere))
            elif gprim.GetPrim().GetName() == "Cylinder":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cylinder))
            elif gprim.GetPrim().GetName() == "Cone":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cone))
            elif gprim.GetPrim().GetName() == "Cube":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cube))
            else:
                self.fail("UsdGeomGprim found which does not match previous mesh names")

    async def test_primitive_fit_gpu(self):
        """Test primitive fit with gpu"""

        stage = self._open_stage("shapes_baked.usda")

        # Check the meshes in the stage
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 5)
        names = [x.GetPrim().GetName() for x in before_meshes]
        self.assertTrue("Sphere" in names)
        self.assertTrue("Cylinder" in names)
        self.assertTrue("Cone" in names)
        self.assertTrue("Cube" in names)
        self.assertTrue("Torus" in names)

        # Check that there are no prims in the stage
        before_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(before_prims), 0)

        # Execute the fit command
        args = DEFAULT_ARGS.copy()
        args["gpuFaceCountThreshold"] = 1
        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # torus should not have been converted
        after_meshes = _get_meshes(stage)
        self.assertEqual(len(after_meshes), 1)
        after_mesh = UsdGeom.Mesh(after_meshes[0])
        self.assertTrue(after_mesh)
        self.assertEqual(after_mesh.GetPrim().GetName(), "Torus")

        # check generated primitives
        after_primitive_shapes = _get_primitive_shapes(stage)
        for gprim in after_primitive_shapes:
            if gprim.GetPrim().GetName() == "Sphere":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Sphere))
            elif gprim.GetPrim().GetName() == "Cylinder":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cylinder))
            elif gprim.GetPrim().GetName() == "Cone":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cone))
            elif gprim.GetPrim().GetName() == "Cube":
                self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cube))
            else:
                self.fail("UsdGeomGprim found which does not match previous mesh names")

    async def test_primitive_fit_selections(self):
        """Test primitive fit selections"""

        stage = self._open_stage("shapes_baked.usda")

        # Iterate through all combinations of flags
        for mask in range(16):
            # Start with the default arguments
            args = DEFAULT_ARGS.copy()

            # Set test flags based on mask
            args["fitSphere"] = True if (mask & 1) != 0 else False
            args["fitCylinder"] = True if (mask & 2) != 0 else False
            args["fitCone"] = True if (mask & 4) != 0 else False
            args["fitCube"] = True if (mask & 8) != 0 else False

            # Execute the fit command
            success, _ = self._execute_command(args)
            self.assertTrue(success)

            # check generated primitives
            after_gprims = _get_all_gprims(stage)
            for gprim in after_gprims:
                if gprim.GetPrim().GetName() == "Sphere":
                    if args["fitSphere"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Sphere))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                elif gprim.GetPrim().GetName() == "Cylinder":
                    if args["fitCylinder"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cylinder))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                elif gprim.GetPrim().GetName() == "Cone":
                    if args["fitCone"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cone))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                elif gprim.GetPrim().GetName() == "Cube":
                    if args["fitCube"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cube))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                elif gprim.GetPrim().GetName() == "Torus":
                    self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                else:
                    self.fail("UsdGeomGprim found which does not match previous mesh names")

            stage.Reload()

    async def test_primitive_fit_instancing(self):
        """Test primitive fit of instanced meshes"""

        stage = self._open_stage("instancing_meshes.usda")

        # Check the meshes in the stage
        before_meshes = _get_meshes_all(stage)
        self.assertEqual(len(before_meshes), 5)
        defined_count = 0
        for mesh in before_meshes:
            if mesh.GetPrim().IsDefined():
                defined_count += 1
        self.assertEqual(defined_count, 4)

        # Check that there are no prims in the stage
        before_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(before_prims), 0)

        args = DEFAULT_ARGS.copy()
        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Assert there are no meshes
        after_meshes = _get_meshes_all(stage)
        self.assertEqual(len(after_meshes), 0)

        # Check the prims in the stage
        after_prims = _get_primitive_shapes_all(stage)
        self.assertEqual(len(after_prims), 5)
        defined_count = 0
        for gprim in after_prims:
            self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cylinder))
            if gprim.GetPrim().IsDefined():
                defined_count += 1
        self.assertEqual(defined_count, 4)

    async def test_primitive_mesh_regularization(self):
        """Test primitive mesh regularization"""

        stage = self._open_stage("mesh_shapes_textured.usda")

        # Check the meshes in the stage
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 3)
        names = [x.GetPrim().GetName() for x in before_meshes]
        self.assertTrue("Cylinder_per_corner" in names)
        self.assertTrue("Sphere_per_vertex" in names)
        self.assertTrue("Cube_per_face" in names)

        # Check that there are no prims in the stage
        before_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(before_prims), 0)

        args = DEFAULT_ARGS.copy()
        args["generateMeshes"] = True
        success, _ = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # Check the meshes in the stage
        # Check the prims in the stage
        after_meshes = _get_meshes(stage)
        self.assertEqual(len(after_meshes), 6)
        defined_count = 0
        for mesh in after_meshes:
            if mesh.GetPrim().IsDefined():
                defined_count += 1
        self.assertEqual(defined_count, 3)
