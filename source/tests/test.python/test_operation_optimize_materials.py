# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Gf, UsdGeom, UsdShade

from .test_utils import Test_Operation, _get_context

MODE_DEDUPLICATE = 0
MODE_DEDUPLICATE_PRIMVAR = 3

DEFAULT_ARGS = {"materialPrimPaths": [], "optimizeMaterialsMode": MODE_DEDUPLICATE}


def _is_bound_material(stage, prim_path, material_path):
    """Returns true if the prim exists and is bound to a material with the given path"""
    prim = stage.GetPrimAtPath(prim_path)

    if prim:
        materialBindingAPI = UsdShade.MaterialBindingAPI(prim)
        material, _ = materialBindingAPI.ComputeBoundMaterial()

        if material and material.GetPath() == material_path:
            return True

    return False


def _count_materials(stage):
    """Count the number of Materials in a stage"""

    numMaterials = 0
    for prim in stage.Traverse():
        if prim.IsA(UsdShade.Material):
            numMaterials += 1

    return numMaterials


def _count_all_materials(stage):
    """Count the number of Materials in a stage (traverse-all)"""

    numMaterials = 0
    for prim in stage.TraverseAll():
        if prim.IsA(UsdShade.Material):
            numMaterials += 1

    return numMaterials


def _count_material_displayColor_displayOpacity(stage):
    """Count the number of Materials and displayColor, displayOpacity primvars in the stage"""
    # Result counters
    numMaterials = 0
    numDisplayColor = 0
    numDisplayOpacity = 0

    # Iterate all the prims in the stage
    for prim in stage.Traverse():

        # Count materials
        if prim.IsA(UsdShade.Material):
            numMaterials += 1

        if prim.GetTypeName() == "Cube" or prim.GetTypeName() == "Mesh":
            primvarsAPI = UsdGeom.PrimvarsAPI(prim)

            # Count displayColor
            displayColor = primvarsAPI.GetPrimvar("displayColor").Get()
            if displayColor:
                numDisplayColor += 1

            # Count displayOpacity
            displayOpacity = primvarsAPI.GetPrimvar("displayOpacity").Get()
            if displayOpacity:
                numDisplayOpacity += 1

    return (numMaterials, numDisplayColor, numDisplayOpacity)


def _check_primvar(prim, primvar_name, interpolation):
    """Check the interpolation of a primvar matches an expected value"""

    primvarsAPI = UsdGeom.PrimvarsAPI(prim)
    primvar = primvarsAPI.GetPrimvar(primvar_name)

    return primvar.GetInterpolation() == interpolation


class Test_Operation_Optimize_Materials(Test_Operation):

    OPERATION = "optimizeMaterials"

    async def test_materials_bound_within_scope(self):
        """Instanced meshes should not be bound to materials outside the scope of the reference"""
        # Test case is duplicate materials within two components, one is instanceable and the other is not.
        stage = self._open_stage("materialBind.usd")
        self._execute_json(stage, "materialBind.json")

        # Instance proxies should NOT be bound to the world materials
        self.assertFalse(_is_bound_material(stage, "/World/Instance/Geometry/Mesh_1", "/World/Looks/Red"))
        self.assertFalse(_is_bound_material(stage, "/World/Instance/Geometry/Mesh_2", "/World/Looks/Red"))
        self.assertFalse(_is_bound_material(stage, "/World/Instance/Geometry/Mesh_3", "/World/Looks/Green"))
        self.assertFalse(_is_bound_material(stage, "/World/PayloadInstance/Geometry/Mesh_1", "/World/Looks/Red"))
        self.assertFalse(_is_bound_material(stage, "/World/PayloadInstance/Geometry/Mesh_2", "/World/Looks/Red"))
        self.assertFalse(_is_bound_material(stage, "/World/PayloadInstance/Geometry/Mesh_3", "/World/Looks/Green"))

        # Instance proxies will be bound to materials within the scope of the instance
        self.assertTrue(_is_bound_material(stage, "/World/Instance/Geometry/Mesh_1", "/World/Instance/Looks/Red"))
        self.assertTrue(_is_bound_material(stage, "/World/Instance/Geometry/Mesh_2", "/World/Instance/Looks/Red"))
        self.assertTrue(_is_bound_material(stage, "/World/Instance/Geometry/Mesh_3", "/World/Instance/Looks/Green"))
        self.assertTrue(
            _is_bound_material(stage, "/World/PayloadInstance/Geometry/Mesh_1", "/World/PayloadInstance/Looks/Red")
        )
        self.assertTrue(
            _is_bound_material(stage, "/World/PayloadInstance/Geometry/Mesh_2", "/World/PayloadInstance/Looks/Red")
        )
        self.assertTrue(
            _is_bound_material(stage, "/World/PayloadInstance/Geometry/Mesh_3", "/World/PayloadInstance/Looks/Green")
        )

        # Referenced prims (that are not instanceable) will be bound to the world materials
        self.assertTrue(_is_bound_material(stage, "/World/Component/Geometry/Mesh_1", "/World/Looks/Red"))
        self.assertTrue(_is_bound_material(stage, "/World/Component/Geometry/Mesh_2", "/World/Looks/Red"))
        self.assertTrue(_is_bound_material(stage, "/World/Component/Geometry/Mesh_3", "/World/Looks/Green"))

        # Payload prims (that are not instanceable) will be bound to the world materials
        self.assertTrue(_is_bound_material(stage, "/World/PayloadComponent/Geometry/Mesh_1", "/World/Looks/Red"))
        self.assertTrue(_is_bound_material(stage, "/World/PayloadComponent/Geometry/Mesh_2", "/World/Looks/Red"))
        self.assertTrue(_is_bound_material(stage, "/World/PayloadComponent/Geometry/Mesh_3", "/World/Looks/Green"))

        # The instanced prims will be bound to materials within the scope of the instance
        # So that when instanced they generate instance proxies with materials in scope
        self.assertTrue(
            _is_bound_material(stage, "/World/Meshes/Asset/Geometry/Mesh_1", "/World/Meshes/Asset/Looks/Red")
        )
        self.assertTrue(
            _is_bound_material(stage, "/World/Meshes/Asset/Geometry/Mesh_2", "/World/Meshes/Asset/Looks/Red")
        )
        self.assertTrue(
            _is_bound_material(stage, "/World/Meshes/Asset/Geometry/Mesh_3", "/World/Meshes/Asset/Looks/Green")
        )

    async def test_DeduplicateMaterials(self):
        """Test the Deduplicate Materials command"""
        stage = self._open_stage("deduplicateMaterials.usda")

        # Initially 7 unique materials
        uniqueMaterials = 0
        for prim in stage.TraverseAll():
            if prim.IsA(UsdShade.Material):
                uniqueMaterials += 1

        self.assertEqual(uniqueMaterials, 7)

        # Run the operation
        self._execute_json(stage, "deduplicateMaterials.json")

        # After de-duplication should only be 4
        uniqueMaterials = 0
        for prim in stage.TraverseAll():
            if prim.IsA(UsdShade.Material):
                uniqueMaterials += 1

        self.assertEqual(uniqueMaterials, 4)

    async def test_OptimizeMaterials(self):
        """Test various material optimizations"""
        stage = self._open_stage("optimizeMaterials.usda")

        # Original scene has 6 materials, and no display color/opacity
        num_materials, num_display_color, num_display_opacity = _count_material_displayColor_displayOpacity(stage)
        self.assertEqual(num_materials, 6)
        self.assertEqual(num_display_color, 0)
        self.assertEqual(num_display_opacity, 0)

        # One material has inputs:opacity
        transparentPrim = stage.GetPrimAtPath("/PlaneTransparent/Plane")
        opacityPrimvar = UsdGeom.PrimvarsAPI(transparentPrim).GetPrimvar("displayOpacity")
        self.assertIsNone(opacityPrimvar.Get())

        # Optimize
        self._execute_json(stage, "optimizeMaterials.json")

        # After optimize all materials should be gone, and replaced with colors/opacity.
        num_materials, num_display_color, num_display_opacity = _count_material_displayColor_displayOpacity(stage)
        self.assertEqual(num_materials, 0)
        self.assertEqual(num_display_color, 6)
        self.assertEqual(num_display_opacity, 1)

        # Explicitly check opacity was converted to a primvar with the correct value
        transparentPrim = stage.GetPrimAtPath("/PlaneTransparent/Plane")
        opacityPrimvar = UsdGeom.PrimvarsAPI(transparentPrim).GetPrimvar("displayOpacity")
        opacity = list(opacityPrimvar.Get())
        self.assertEqual(len(opacity), 1)
        self.assertAlmostEqual(opacity[0], 0.6)

    async def test_OptimizeMaterialsInheritedColors(self):
        """Test that when converting to color, child opinions on color are blocked"""

        stage = self._open_stage("optimizeMaterialsInheritedMaterial.usda")

        # The top level xform should not initially have a primvar value
        primvarsAPI = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath("/Cube"))
        displayColorPrimvar = primvarsAPI.GetPrimvar("displayColor")
        self.assertFalse(displayColorPrimvar.HasAuthoredValue())

        # The initial child mesh should have a primvar value
        primvarsAPI = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath("/Cube/CubeShape"))
        displayColorPrimvar = primvarsAPI.GetPrimvar("displayColor")
        self.assertTrue(displayColorPrimvar.HasAuthoredValue())
        color = displayColorPrimvar.Get()
        self.assertEqual(list(color), [(0, 0, 1)])

        # Convert materials
        self._execute_json(stage, "optimizeMaterialsInheritedMaterial.json")

        # After convert the top level xform should have a primvar
        primvarsAPI = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath("/Cube"))
        xformPrimvar = primvarsAPI.GetPrimvar("displayColor")
        self.assertTrue(xformPrimvar.HasAuthoredValue())
        xformColor = xformPrimvar.Get()
        self.assertEqual(list(xformColor), [(1, 1, 0)])

        # The primvar on the prim should have been blocked
        primvarsAPI = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath("/Cube/CubeShape"))
        displayColorPrimvar = primvarsAPI.GetPrimvar("displayColor")
        self.assertFalse(displayColorPrimvar.HasAuthoredValue())

        # Find the inherited primvar from the prim
        inheritedPrimvar = primvarsAPI.FindPrimvarWithInheritance("displayColor")
        color = inheritedPrimvar.Get()

        # Assert the expected value
        self.assertEqual(list(color), [(1, 1, 0)])

        # Assert it matches the xform value it inherited from
        self.assertEqual(color, xformColor)

        # Also assert the primvar came from where we expected it to
        self.assertEqual(inheritedPrimvar.GetAttr().GetPrim(), xformPrimvar.GetAttr().GetPrim())

    async def test_OptimizeMaterialsRemoveUnbound(self):
        """Test the "Remove Unbound" operation mode"""

        stage = self._open_stage("optimizeMaterialsUnbound.usda")

        materialsCount = _count_materials(stage)

        # Verify initial material count.
        # This is actually 2x the materials, due to the referenced copies - doesn't really matter
        # for the purpose of this test.
        self.assertEqual(materialsCount, 12)

        # Explicitly test that the unbound material exists
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialUnused"))

        # Execute
        self._execute_json(stage, "optimizeMaterialsUnbound.json")

        # Count again. One unused material (plus the second referenced copy) should no longer
        # be present.
        materialsCount = _count_materials(stage)

        self.assertEqual(materialsCount, 10)

        # Explicitly test that the unbound material is now gone
        self.assertFalse(stage.GetPrimAtPath("/World/Looks/MaterialUnused"))

    async def test_OptimizeMaterialsReferenceOrder(self):
        """Test that removing duplicates when the source reference material is described after
        the reference in a stage"""

        # Test case is a stage with two prototypes with a copy of the same material and two
        # references to them. The distinction between other tests is that the prototypes (the
        # source materials) are described AFTER the references in the usda. This test ensures
        # that even though a standard traversal would encounter the references first, we still
        # bind to the least-referenced thing. Otherwise we end up deleting the thing that is
        # being referenced!
        stage = self._open_stage("referencedMaterials.usda")

        materialsCount = _count_materials(stage)
        self.assertEqual(materialsCount, 4)

        # Assert initial state
        self.assertTrue(_is_bound_material(stage, "/World/Cube1/Shape", "/World/Cube1/Material3"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube2/Shape", "/World/Cube2/Material"))
        self.assertTrue(_is_bound_material(stage, "/Prototypes/CubeYellow1/Shape", "/Prototypes/CubeYellow1/Material3"))
        self.assertTrue(_is_bound_material(stage, "/Prototypes/CubeYellow2/Shape", "/Prototypes/CubeYellow2/Material"))

        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # After optimize only one unique material should remain, and everything should be bound
        # to the first prototype that is encountered.
        materialsCount = _count_materials(stage)
        self.assertEqual(materialsCount, 1)

        self.assertTrue(_is_bound_material(stage, "/World/Cube1/Shape", "/Prototypes/CubeYellow1/Material3"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube2/Shape", "/Prototypes/CubeYellow1/Material3"))
        self.assertTrue(_is_bound_material(stage, "/Prototypes/CubeYellow1/Shape", "/Prototypes/CubeYellow1/Material3"))
        self.assertTrue(_is_bound_material(stage, "/Prototypes/CubeYellow2/Shape", "/Prototypes/CubeYellow1/Material3"))

    async def test_OptimizeMaterialsPayloads(self):
        """Test that deduplicating materials added via payload works"""

        # This test scene is a stage with four cubes and four copies of a material,
        # which is added via payload.
        stage = self._open_stage("optimizeMaterialsPayload.usda")

        materialsCount = _count_materials(stage)
        self.assertEqual(materialsCount, 4)

        # Custom execution context
        context = _get_context(stage, report=True)

        args = DEFAULT_ARGS.copy()
        self._execute_command(args, context)

        # After optimizing only one material should exist
        materialsCount = _count_materials(stage)
        self.assertEqual(materialsCount, 1)

        self.assertTrue(_is_bound_material(stage, "/World/Cube1", "/World/Looks/Blue1"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube2", "/World/Looks/Blue1"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube3", "/World/Looks/Blue1"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube4", "/World/Looks/Blue1"))

    async def test_OptimizeMaterialsHierarchy(self):
        """Test recursive path expressions when removing duplicate materials"""

        stage = self._open_stage("optimizeMaterialsHierarchy.usda")

        # Assert initial state
        self.assertEqual(_count_materials(stage), 7)

        # First test is deduplicating the entire stage
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)
        self.assertEqual(_count_materials(stage), 3)

        # Re-open stage
        stage = self._open_stage("optimizeMaterialsHierarchy.usda")
        self.assertEqual(_count_materials(stage), 7)

        # Now filter for a subset of materials
        args["materialPrimPaths"] = ["/Materials/Red//"]
        self._execute_command(args)

        # Should be 5: two duplicate "red" materials should have been removed
        self.assertEqual(_count_materials(stage), 5)

        # Repeat with a different path
        args["materialPrimPaths"] = ["/Materials/Blue//"]
        self._execute_command(args)

        # Should be 4: 5 from the previous assert, and then one duplicate blue one removed
        self.assertEqual(_count_materials(stage), 4)

        # Do the final subset
        args["materialPrimPaths"] = ["/Materials/Green//"]
        self._execute_command(args)

        # Now there should be 3, which is all of the duplicates gone
        self.assertEqual(_count_materials(stage), 3)

        # Finally re-open and assert initial state
        stage = self._open_stage("optimizeMaterialsHierarchy.usda")
        self.assertEqual(_count_materials(stage), 7)

        # Test everything under /Materials, which should also give us 3
        args["materialPrimPaths"] = ["/Materials//"]
        self._execute_command(args)
        self.assertEqual(_count_materials(stage), 3)

    async def test_OptimizeMaterialsNodeGraphLoop(self):
        """Test that a NodeGraph that connects to itself does not cause an infinite loop"""

        stage = self._open_stage("optimizeMaterialsNodeGraph.usda")

        # Assert initial state
        self.assertEqual(_count_materials(stage), 2)

        # Deduplicate
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # We should not segfault and end up with a duplicate removed
        self.assertEqual(_count_materials(stage), 1)

    async def test_OptimizeMaterialsConvertToPrimvar(self):
        """Test deduplicating materials with primvar readers"""

        stage = self._open_stage("optimizeMaterialsPrimvars.usda")

        # Assert initial number of materials
        self.assertEqual(_count_all_materials(stage), 15)

        # Check various prims don't have primvars yet
        cube1 = stage.GetPrimAtPath("/World/Cube1/Shape")
        self.assertFalse(cube1.HasAttribute("primvars:diffuse_color_constant"))

        cube2 = stage.GetPrimAtPath("/World/Cube2/Shape")
        self.assertFalse(cube2.HasAttribute("primvars:diffuse_color_constant"))

        cube3 = stage.GetPrimAtPath("/World/Cube3/Shape")
        self.assertFalse(cube3.HasAttribute("primvars:diffuse_color_constant"))

        cube4 = stage.GetPrimAtPath("/World/Cube4/Shape")
        self.assertFalse(cube4.HasAttribute("primvars:diffuse_color_constant"))
        self.assertFalse(cube4.HasAttribute("primvars:opacity_constant"))

        cube5 = stage.GetPrimAtPath("/World/Cube5/Shape")
        self.assertFalse(cube5.HasAttribute("primvars:diffuse_color_constant"))
        self.assertFalse(cube5.HasAttribute("primvars:opacity_constant"))

        inherited = stage.GetPrimAtPath("/World/InheritedCubes")
        self.assertFalse(inherited.HasAttribute("primvars:diffuse_color_constant"))

        # Duplicate materials with existing primvar readers set up
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialReader1"))
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialReader1/PrimvarReader_diffuse"))
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialReader2"))
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialReader2/PrimvarReader_diffuse"))

        cube7 = stage.GetPrimAtPath("/World/Cube7/Shape")
        self.assertTrue(cube7.HasAttribute("primvars:diffuse_color_constant"))
        self.assertFalse(cube7.HasAttribute("primvars:albedo_add"))

        cube8 = stage.GetPrimAtPath("/World/Cube8/Shape")
        self.assertTrue(cube8.HasAttribute("primvars:diffuse_color_constant"))
        self.assertFalse(cube8.HasAttribute("primvars:albedo_add"))

        args = DEFAULT_ARGS.copy()
        args["optimizeMaterialsMode"] = MODE_DEDUPLICATE_PRIMVAR
        self._execute_command(args)

        # Count after deduplication
        self.assertEqual(_count_all_materials(stage), 9)

        # Three basic cubes should now have a primvar
        self.assertTrue(cube1.HasAttribute("primvars:diffuse_color_constant"))
        self.assertTrue(cube2.HasAttribute("primvars:diffuse_color_constant"))
        self.assertTrue(cube3.HasAttribute("primvars:diffuse_color_constant"))

        # And also be bound to the same material
        self.assertTrue(_is_bound_material(stage, "/World/Cube1/Shape", "/World/Looks/MaterialA_pv"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube2/Shape", "/World/Looks/MaterialA_pv"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube3/Shape", "/World/Looks/MaterialA_pv"))

        # Verify the material has a primvar reader
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialA_pv/PrimvarReader_diffuse_color_constant"))

        # Prims with multiple primvars
        self.assertTrue(cube4.HasAttribute("primvars:diffuse_color_constant"))
        self.assertTrue(cube4.HasAttribute("primvars:opacity_constant"))
        self.assertTrue(cube5.HasAttribute("primvars:diffuse_color_constant"))
        self.assertTrue(cube5.HasAttribute("primvars:opacity_constant"))

        self.assertTrue(_is_bound_material(stage, "/World/Cube4/Shape", "/World/Looks/MaterialBlueOpacity_pv"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube5/Shape", "/World/Looks/MaterialBlueOpacity_pv"))

        # Xform with primvars
        self.assertTrue(_is_bound_material(stage, "/World/InheritedCubes", "/World/Looks/MaterialA_pv"))
        self.assertTrue(inherited.HasAttribute("primvars:diffuse_color_constant"))

        # Inherited xform should have a primvar authored with the expected color
        inheritedColor = inherited.GetAttribute("primvars:diffuse_color_constant").Get()
        self.assertEqual(inheritedColor, Gf.Vec3f(0.0, 1.0, 0.0))

        # Both cubes should have the _inherited_ attribute value authored, not the
        # color from the weak material they are directly bound to
        inheritedCube1 = stage.GetPrimAtPath("/World/InheritedCubes/Cube1")
        inheritedColor1 = inheritedCube1.GetAttribute("primvars:diffuse_color_constant").Get()
        self.assertEqual(inheritedColor, inheritedColor1)

        inheritedCube2 = stage.GetPrimAtPath("/World/InheritedCubes/Cube2")
        inheritedColor2 = inheritedCube2.GetAttribute("primvars:diffuse_color_constant").Get()
        self.assertEqual(inheritedColor, inheritedColor2)

        # Material that wasn't deduplicated
        self.assertTrue(_is_bound_material(stage, "/World/Cube6/Shape", "/World/Cube6/MaterialYellowTint"))
        self.assertTrue(stage.GetPrimAtPath("/World/Cube6/MaterialYellowTint"))

        # Prims/Materials with an existing primvar reader
        self.assertTrue(cube7.HasAttribute("primvars:diffuse_color_constant"))
        self.assertTrue(cube7.HasAttribute("primvars:albedo_add"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube7/Shape", "/World/Looks/MaterialReader1_pv"))

        self.assertTrue(cube8.HasAttribute("primvars:diffuse_color_constant"))
        self.assertTrue(cube8.HasAttribute("primvars:albedo_add"))
        self.assertTrue(_is_bound_material(stage, "/World/Cube8/Shape", "/World/Looks/MaterialReader1_pv"))

        # Verify primvar readers. "diffuse" is the original one, we reused it hence it has the original name
        # instead of diffuse_color_constant.
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialReader1_pv/PrimvarReader_diffuse"))
        self.assertTrue(stage.GetPrimAtPath("/World/Looks/MaterialReader1_pv/PrimvarReader_albedo_add"))

        # Some basic instance checks.

        # Different instance boundaries, not deduplicated
        self.assertTrue(
            _is_bound_material(
                stage, "/World/Prototypes/PrototypeCube/Cube", "/World/Prototypes/PrototypeCube/PrototypeMaterial1"
            )
        )
        self.assertTrue(
            _is_bound_material(
                stage, "/World/Prototypes/PrototypeCone/Cone", "/World/Prototypes/PrototypeCone/PrototypeMaterial2"
            )
        )

        # Prototype with multiple materials
        self.assertTrue(
            _is_bound_material(stage, "/World/Prototypes/PrototypeMulti/Cube1", "/World/Looks/MultiMaterial1_pv")
        )
        self.assertTrue(
            _is_bound_material(stage, "/World/Prototypes/PrototypeMulti/Cube2", "/World/Looks/MultiMaterial1_pv")
        )

    async def test_OptimizeMaterialsAnalysis(self):
        """Test analysis mode"""

        stage = self._open_stage("optimizeMaterials.usda")

        context = _get_context(stage, analysis=True)

        success, result = self._execute_command(DEFAULT_ARGS, context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        self.assertTrue("duplicates" in analysis)
        self.assertTrue("duplicatePrimvars" in analysis)

        # Assert two sets of regular duplicates
        dupes = analysis["duplicates"]
        self.assertEqual(len(dupes), 2)

        # Order is non-deterministic across platforms, so identify
        # each group by a known member before asserting.
        if "/Cubes/CubeRed1/Material4" in dupes[0]:
            listA = dupes[0]
            listB = dupes[1]
        else:
            listA = dupes[1]
            listB = dupes[0]

        self.assertIn("/Cubes/CubeRed1/Material4", listA)
        self.assertIn("/Cubes/CubeRed2/Material5", listA)
        self.assertIn("/Cubes/CubeYellow1/Material3", listB)
        self.assertIn("/Cubes/CubeYellow2/Material", listB)

        # One set of materials that can be dedeuplicate with primvar readers
        dupePrimvars = analysis["duplicatePrimvars"]
        self.assertEqual(len(dupePrimvars), 1)

        self.assertIn("/Cubes/CubeYellow1/Material3", dupePrimvars[0])
        self.assertIn("/Cubes/CubeRed1/Material4", dupePrimvars[0])
        self.assertIn("/Cubes/CubeRed2/Material5", dupePrimvars[0])
        self.assertIn("/Cubes/CubeBlue1/Material", dupePrimvars[0])
        self.assertIn("/Cubes/CubeYellow2/Material", dupePrimvars[0])

    async def test_OptimizeMaterialsPrimvarSubsets(self):
        """Test deduplicating w/primvars where subsets are involved"""

        # Open stage
        stage = self._open_stage("optimizeMaterialsSubsets.usda")

        # Initial assert of material count
        self.assertEqual(_count_materials(stage), 6)

        # Execute operation
        args = DEFAULT_ARGS.copy()
        args["optimizeMaterialsMode"] = MODE_DEDUPLICATE_PRIMVAR
        success, result = self._execute_command(args)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # Assert deduplication worked as expected
        self.assertEqual(_count_materials(stage), 2)

        # Test a bunch of subset scenarios.

        # A mesh with subsets that partially cover the faces, and a fallback material
        cubePartialFallback = stage.GetPrimAtPath("/World/CubesPartialFallback")
        self.assertTrue(_check_primvar(cubePartialFallback, "diffuseColor", UsdGeom.Tokens.uniform))
        self.assertTrue(_check_primvar(cubePartialFallback, "metallic", UsdGeom.Tokens.uniform))

        # Assert the expected primvar values for one of the primvars
        primvarsAPI = UsdGeom.PrimvarsAPI(cubePartialFallback)
        metallic = primvarsAPI.GetPrimvar("metallic").Get()
        expected = [0.75] * 6 + [0.5] * 6 + [0.25] * 6
        self.assertEqual(metallic, expected)

        # A mesh with subsets that cover all faces
        cubeCoveredFallback = stage.GetPrimAtPath("/World/CubesCoveredFallback")
        self.assertTrue(_check_primvar(cubeCoveredFallback, "diffuseColor", UsdGeom.Tokens.uniform))
        self.assertTrue(_check_primvar(cubeCoveredFallback, "metallic", UsdGeom.Tokens.uniform))

        # Assert the expected primvar values for one of the primvars
        # Note: this is covered and has a different initial 6 values than the prim above
        primvarsAPI = UsdGeom.PrimvarsAPI(cubeCoveredFallback)
        metallic = primvarsAPI.GetPrimvar("metallic").Get()
        expected = [0.9] * 6 + [0.5] * 6 + [0.25] * 6
        self.assertEqual(metallic, expected)

        # A mesh with diffuseColor shader values that all match, meaning the resulting primvar
        # should be constant
        cubeConstant = stage.GetPrimAtPath("/World/CubesConstant")
        self.assertTrue(_check_primvar(cubeConstant, "diffuseColor", UsdGeom.Tokens.constant))
        self.assertTrue(_check_primvar(cubeConstant, "metallic", UsdGeom.Tokens.uniform))

        # A mesh with no fallback material
        cubeNoFallback = stage.GetPrimAtPath("/World/CubesNoFallback")
        self.assertTrue(_check_primvar(cubeNoFallback, "diffuseColor", UsdGeom.Tokens.uniform))
        self.assertTrue(_check_primvar(cubeNoFallback, "metallic", UsdGeom.Tokens.uniform))

        # Extra asserts that given no fallback material, the expected faces have empty
        # values for those indices
        primvarsAPI = UsdGeom.PrimvarsAPI(cubeNoFallback)
        colors = primvarsAPI.GetPrimvar("diffuseColor").Get()
        for c in colors[0:11]:
            self.assertEqual(c, Gf.Vec3f(0.0, 0.0, 0.0))
        for c in colors[12:17]:
            self.assertNotEqual(c, Gf.Vec3f(0.0, 0.0, 0.0))

        metallic = primvarsAPI.GetPrimvar("metallic").Get()
        for m in metallic[0:11]:
            self.assertEqual(m, 0.0)
        for m in metallic[12:17]:
            self.assertEqual(m, 0.25)

        # A mesh with a subset that has a unique material.
        # We should not end up with a primvar authored on the mesh in this case,
        # as there is nothing to deduplicate/convert to a primvar reader.
        cubeUnique = stage.GetPrimAtPath("/World/CubesUnique")
        primvarsAPI = UsdGeom.PrimvarsAPI(cubeUnique)
        self.assertFalse(primvarsAPI.HasPrimvar("diffuseColor"))

    async def test_OptimizeMaterialsSpecialize(self):
        """Test that specialized materials are not de-duplicated"""

        stage = self._open_stage("optimizeMaterialsSpecialize.usda")

        # Initial assert of material count
        self.assertEqual(_count_materials(stage), 10)

        # Execute operation
        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # Assert deduplication worked as expected
        # One material that was not specialized has been deduplicated
        self.assertEqual(_count_materials(stage), 9)

        # Assert the specialized materials and their targets still exist.
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes1/Looks/PreviewSurface"))
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes1/Cubes/Looks/PreviewSurface_01"))
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes2/Looks/PreviewSurface"))
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes2/Cubes/Looks/PreviewSurface"))

    async def test_OptimizeMaterialsSpecializePrimvars(self):
        """Test that specialized materials are not converted to primvar readers"""

        stage = self._open_stage("optimizeMaterialsSpecialize.usda")

        # Initial assert of material count
        self.assertEqual(_count_materials(stage), 10)

        # Execute operation
        args = DEFAULT_ARGS.copy()
        args["optimizeMaterialsMode"] = MODE_DEDUPLICATE_PRIMVAR

        success, result = self._execute_command(args)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # Nothing has changed - all materials still exist, nothing deduplicated
        self.assertEqual(_count_materials(stage), 10)

        # Assert the specialized materials and their targets still exist.
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes1/Looks/PreviewSurface"))
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes1/Cubes/Looks/PreviewSurface_01"))
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes2/Looks/PreviewSurface"))
        self.assertTrue(stage.GetPrimAtPath("/World/Cubes2/Cubes/Looks/PreviewSurface"))

        # Re-run in analysis mode
        context = _get_context(stage, analysis=True)
        success, result = self._execute_command(DEFAULT_ARGS, context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        self.assertTrue("duplicatePrimvars" in analysis)

        # Assert one set of regular duplicates, with two materials
        dupes = analysis["duplicates"]
        self.assertEqual(len(dupes), 1)
        self.assertEqual(len(dupes[0]), 2)

        # These are flagged as nothing specializes them
        self.assertIn("/World/Cubes1/Looks/PreviewSurface_02", dupes[0])
        self.assertIn("/World/Cubes2/Looks/PreviewSurface_02", dupes[0])

        # Same as above, as they can also be deduplicated with primvars
        dupePrimvars = analysis["duplicatePrimvars"]
        self.assertEqual(len(dupePrimvars), 1)
        self.assertEqual(len(dupePrimvars[0]), 2)

        self.assertIn("/World/Cubes1/Looks/PreviewSurface_02", dupePrimvars[0])
        self.assertIn("/World/Cubes2/Looks/PreviewSurface_02", dupePrimvars[0])
