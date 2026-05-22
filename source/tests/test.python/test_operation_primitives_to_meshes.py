# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_meshes

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "convertSpheres": True,
    "sphereLongitudeDivisions": 32,
    "sphereLatitudeDivisions": 16,
    "convertCylinders": True,
    "cylinderWallDivisions": 32,
    "cylinderLatitudeDivisions": 1,
    "cylinderEndcaps": True,
    "convertCones": True,
    "coneSideDivisions": 64,
    "coneLengthDivisions": 3,
    "coneBases": True,
    "convertCubes": True,
}


def _get_primitive_shapes(stage):
    return [UsdGeom.Gprim(x) for x in stage.TraverseAll() if x.IsA(UsdGeom.Gprim) and not x.IsA(UsdGeom.PointBased)]


def _get_all_gprims(stage):
    return [UsdGeom.Gprim(x) for x in stage.TraverseAll() if x.IsA(UsdGeom.Gprim)]


class Test_Operation_Primitives_to_Meshes(Test_Operation):

    OPERATION = "primitivesToMeshes"

    async def test_primitives_to_meshes(self):
        """Test primitive to mesh conversion"""

        stage = self._open_stage("primitives.usda")

        # Check the primitives in the stage
        before_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(before_prims), 5)
        names = [x.GetPrim().GetName() for x in before_prims]
        self.assertTrue("Sphere" in names)
        self.assertTrue("Cylinder" in names)
        self.assertTrue("Cone" in names)
        self.assertTrue("Cube" in names)
        self.assertTrue("Capsule" in names)

        # Check that there are no meshes in the stage
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 0)

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # capsule should not have been converted
        after_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(after_prims), 1)
        self.assertTrue(after_prims[0])
        self.assertEqual(after_prims[0].GetPrim().GetName(), "Capsule")

        # check generated meshes
        after_primitive_shapes = _get_meshes(stage)
        for mesh in after_primitive_shapes:
            mesh_is_old_prim_with_references = mesh.GetPrim().HasAuthoredReferences() and (
                mesh.GetPrim().GetName() == "Sphere"
                or mesh.GetPrim().GetName() == "Cylinder"
                or mesh.GetPrim().GetName() == "Cone"
                or mesh.GetPrim().GetName() == "Cube"
            )
            mesh_is_new_prim_without_references = not mesh.GetPrim().HasAuthoredReferences() and (
                mesh.GetPrim().GetName() == "sphere_Y_32_16"
                or mesh.GetPrim().GetName() == "cylinder_Y_32_1"
                or mesh.GetPrim().GetName() == "cone_Y_64_3"
                or mesh.GetPrim().GetName() == "cube_Y"
            )
            self.assertTrue(mesh_is_old_prim_with_references or mesh_is_new_prim_without_references)

    async def test_primitives_to_meshes_selections(self):
        """Test primitive to mesh conversion selections"""

        stage = self._open_stage("primitives.usda")

        # Iterate through all combinations of flags
        for mask in range(16):
            # Start with the default arguments
            args = DEFAULT_ARGS.copy()

            # Set test flags based on mask
            args["convertSpheres"] = True if (mask & 1) != 0 else False
            args["convertCylinders"] = True if (mask & 2) != 0 else False
            args["convertCones"] = True if (mask & 4) != 0 else False
            args["convertCubes"] = True if (mask & 8) != 0 else False

            # Execute the command
            success, result = self._execute_command(args)
            self.assertTrue(success)

            # check generated primitives
            after_gprims = _get_all_gprims(stage)
            for gprim in after_gprims:
                if gprim.GetPrim().GetName() == "Sphere":
                    if args["convertSpheres"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Sphere))
                elif gprim.GetPrim().GetName() == "Cylinder":
                    if args["convertCylinders"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cylinder))
                elif gprim.GetPrim().GetName() == "Cone":
                    if args["convertCones"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cone))
                elif gprim.GetPrim().GetName() == "Cube":
                    if args["convertCubes"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cube))
                elif gprim.GetPrim().GetName() == "Capsule":
                    self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Capsule))

            stage.Reload()
