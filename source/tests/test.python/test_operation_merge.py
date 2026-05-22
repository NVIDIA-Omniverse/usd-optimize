# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, Vt

from .test_utils import Test_Operation, _get_context, _get_meshes, _get_test_data_file_path

# MergePointOption values
MERGE_POINT_DEFAULT = 0  # Use pseudo root prim
MERGE_POINT_XFORM = 1  # Use the first xformable parent
MERGE_POINT_KINDASSEMBLY = 2  # Use the first parent of kind assembly
MERGE_POINT_KINDGROUP = 3  # Use the first parent of kind group
MERGE_POINT_KINDCOMPONENT = 4  # Use the first parent of kind component
MERGE_POINT_KINDMODEL = 5  # Use the first parent of kind model
MERGE_POINT_KINDSUBCOMPONENT = 6  # Use the first parent of kind subcomponent
MERGE_POINT_ROOTPRIM = 7  # Use root prims
MERGE_POINT_PARENTPRIM = 8  # Use parent prims

# Default arguments for the command
DEFAULT_ARGS = {
    "meshPrimPaths": [],
    "considerMaterials": False,
    "materialAlbedoAsVertexColors": False,
    "originalGeomOption": 1,
    "mergePoint": MERGE_POINT_DEFAULT,
    "rootPath": "",
}


def _find_mesh_by_face_count(prims, value):
    """Helper function to find the first mesh in a list that has a given face count"""
    # WARNING: This yields only the first matching prim, only use this function if you understand all prims in the list
    for prim in prims:
        mesh = UsdGeom.Mesh(prim)
        if mesh:
            face_vertex_counts = mesh.GetFaceVertexCountsAttr().Get()
            if len(face_vertex_counts) == value:
                return prim


def _is_visible(prim):
    """Compute whether or not a prim is visible"""
    imageable = UsdGeom.Imageable(prim)
    return imageable.ComputeVisibility() != UsdGeom.Tokens.invisible


class Test_Operation_Merge(Test_Operation):

    OPERATION = "merge"

    async def test_merge_doubleSided(self):
        """Test merge functions handling of meshes with different values for 'doubleSided' attribute"""
        # The input scene contains 3 meshes that we will try to merge.
        # The first does not define a value for "doubleSided", the second defines "True" and the thrid defines "False"
        # As the default for "doubleSided" is False the first and thrid should merge but the second should not
        stage = self._open_stage("doubleSided_input.usda")
        self._execute_json(stage, "doubleSided_merge.json")

        # Assert that the command ran and there are exactly two meshes afterwards
        after_meshes = _get_meshes(stage)
        self.assertEqual(len(after_meshes), 2)

        # Assert that the single sided and double sided meshes have the correct value for "doubleSided"
        single_sided_prim = stage.GetPrimAtPath("/World/Example/merged")
        double_sided_prim = stage.GetPrimAtPath("/World/Example/merged_1")
        self.assertEqual(UsdGeom.Gprim(single_sided_prim).GetDoubleSidedAttr().Get(), False)
        self.assertEqual(UsdGeom.Gprim(double_sided_prim).GetDoubleSidedAttr().Get(), True)

        # Load the stage that describes our expected result
        file_path = _get_test_data_file_path("doubleSided_output.usda")
        expected_stage = Usd.Stage.Open(file_path)

        # Assert that the correct meshes were merged together by comparing their points to a pre-generated result
        prim_path = "/World/Example/merged"
        returned_prim = stage.GetPrimAtPath(prim_path)
        expected_prim = expected_stage.GetPrimAtPath(prim_path)
        returned_points = UsdGeom.PointBased(returned_prim).GetPointsAttr().Get()
        expected_points = UsdGeom.PointBased(expected_prim).GetPointsAttr().Get()
        self.assertEqual(returned_points, expected_points)
        prim_path = "/World/Example/merged_1"
        returned_prim = stage.GetPrimAtPath(prim_path)
        expected_prim = expected_stage.GetPrimAtPath(prim_path)
        returned_points = UsdGeom.PointBased(returned_prim).GetPointsAttr().Get()
        expected_points = UsdGeom.PointBased(expected_prim).GetPointsAttr().Get()
        self.assertEqual(returned_points, expected_points)

    async def test_merge_unusedPoints(self):
        # Test that merging meshes with unused points offset the faceVertexIndices correctly
        stage = self._open_stage("cubesUnusedPoints.usda")

        before_meshes = _get_meshes(stage)

        # Execute merge command
        self._execute_json(stage, "defaultMergeMeshes.json")

        after_meshes = _get_meshes(stage)
        new_meshes = set(after_meshes) - set(before_meshes)

        # Get the result, and grab all of the points
        mergedCube = list(new_meshes)[0]
        mergedPoints = list(UsdGeom.Mesh(mergedCube).GetPointsAttr().Get())

        # Unused point, intentionally included in the usda
        unusedPoint = (999.0, 999.0, 999.0)

        # Should be carried over to points
        self.assertIn(unusedPoint, mergedPoints)

        # Get the actual used points (as described by the face vertex indices) and verify
        # that this unused point was not incorrectly indexed
        usedPoints = list()
        faceVertexIndices = UsdGeom.Mesh(mergedCube).GetFaceVertexIndicesAttr().Get()
        for fvi in faceVertexIndices:
            usedPoints.append(mergedPoints[fvi])

        self.assertNotIn(unusedPoint, usedPoints)

    async def test_merge_primvarMerge(self):
        """Test various scenarios merging primvar values together"""

        # Load the test scene and then run a bunch of commands. This will perform a
        # bunch of merges on meshes with various combinations of displayColor
        # primvar values/interpolations.
        stage = self._open_stage("primvarMerge.usda")
        self._execute_json(stage, "primvarMerge.json")

        # Check merging two primvars that are constant result in a constant value
        prim = stage.GetPrimAtPath("/World/ConstantToConstant")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "constant")
        self.assertEqual(len(displayColor.Get()), 1)

        # Verify that merging two primvars that are constant with a different value
        # results in uniform output.
        prim = stage.GetPrimAtPath("/World/ConstantToUniform")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "uniform")
        self.assertEqual(len(displayColor.Get()), 2)
        self.assertEqual(len(displayColor.GetIndices()), 8)

        # Verify that two uniform meshes remain uniform
        prim = stage.GetPrimAtPath("/World/UniformToUniform")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "uniform")
        self.assertEqual(len(displayColor.Get()), 3)
        self.assertEqual(len(displayColor.GetIndices()), 8)

        # Verify that mixing varying with something else goes to faceVarying
        prim = stage.GetPrimAtPath("/World/UniformVertexToFaceVarying")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "faceVarying")
        self.assertEqual(len(displayColor.Get()), 3)
        self.assertEqual(len(displayColor.GetIndices()), 32)

        # Verify that all vertex remains vertex
        prim = stage.GetPrimAtPath("/World/VertexToVertex")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "vertex")
        self.assertEqual(len(displayColor.Get()), 3)
        self.assertEqual(len(displayColor.GetIndices()), 18)

        # Verify that all varying remains varying
        prim = stage.GetPrimAtPath("/World/VaryingToVarying")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "varying")
        self.assertEqual(len(displayColor.Get()), 3)
        self.assertEqual(len(displayColor.GetIndices()), 18)

        # Verify a bunch of mixed meshes end up faceVarying
        prim = stage.GetPrimAtPath("/World/MixedToFaceVarying")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "faceVarying")
        self.assertEqual(len(displayColor.Get()), 4)
        self.assertEqual(len(displayColor.GetIndices()), 64)

        # Verify Uniform+FaceVarying works correctly.
        # Tests that adding faceVarying and _then_ uniform doesn't break the interpolation.
        # Also tests issue with incorrectly offsetting into the output indices.
        prim = stage.GetPrimAtPath("/World/UniformOffset")
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertEqual(displayColor.GetInterpolation(), "faceVarying")
        self.assertEqual(len(displayColor.Get()), 4)

        # Explicit value check that the indices match.
        expectedIndices = [
            0,
            1,
            1,
            1,
            0,
            2,
            2,
            2,  # FV indices copied direct
            0,
            3,
            3,
            3,
            0,
            1,
            1,
            1,
            0,
            1,
            1,
            1,
            0,
            2,
            2,
            2,
            0,
            3,
            3,
            3,
            0,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            2,
            2,
            2,
            2,  # Uniform indices expanded
            3,
            3,
            3,
            3,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            1,
            2,
            2,
            2,
            2,
            3,
            3,
            3,
            3,
            1,
            1,
            1,
            1,
        ]

        self.assertEqual(list(displayColor.GetIndices()), expectedIndices)

        # Verify that meshes with no colors are bucketed separately and have no display color.

        # Execute a merge operation with a mix of prims with and without display color.
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = [
            "/World/NoColorMesh",
            "/World/BlockedColorMesh",
            "/World/ConstantMesh",
            "/World/UniformMesh",
        ]
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        output_paths = result[2]

        # There should be 2 merged mesh prims.
        prims = [stage.GetPrimAtPath(x) for x in output_paths]
        self.assertEqual(len(prims), 2)

        # One will have display color authored ...
        prim = stage.GetPrimAtPath(output_paths[0])
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertTrue(displayColor.HasAuthoredValue())

        # ... and the other will not.
        prim = stage.GetPrimAtPath(output_paths[1])
        displayColor = UsdGeom.PrimvarsAPI(prim).GetPrimvar("displayColor")
        self.assertFalse(displayColor.HasAuthoredValue())

    async def test_merge_doNotCastShadows(self):
        """Test merging with the special case primvars:doNotCastShadows attribute"""
        args = DEFAULT_ARGS.copy()

        stage = self._open_stage("mergeDoNotCastShadows.usda")
        success, result = self._execute_command(args)

        # There should be 2 merged mesh prims.
        prims = [stage.GetPrimAtPath(x) for x in result[2]]
        self.assertEqual(len(prims), 2)

        # check the primvars:doNotCastShadows attribute on each merged mesh
        self.assertTrue(stage.GetPrimAtPath("/merged").GetAttribute("primvars:doNotCastShadows").Get())
        self.assertFalse(stage.GetPrimAtPath("/merged_1").GetAttribute("primvars:doNotCastShadows").Get())

    async def test_merge_primvarMergeArbitrary(self):
        """Test merging (or not merging) various custom primvars"""
        args = DEFAULT_ARGS.copy()

        stage = self._open_stage("mergeArbitraryPrimvar.usda")
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)
        self.assertTrue(result[0])

        # There should be 1 merged mesh prim.
        prims = [stage.GetPrimAtPath(x) for x in result[2]]
        self.assertEqual(len(prims), 1)

        # The mesh should have a uniform green/blue/red display color authored on the mesh.
        primvar_api = UsdGeom.PrimvarsAPI(prims[0])

        # check for the primvars that should have been merged
        self.assertTrue(primvar_api.GetPrimvar("mergeInt").HasAuthoredValue())
        self.assertTrue(primvar_api.GetPrimvar("mergeFloat3").HasAuthoredValue())
        self.assertTrue(primvar_api.GetPrimvar("mergeString").HasAuthoredValue())

        # check for the primvars that should have not been merged
        self.assertFalse(primvar_api.GetPrimvar("diffTypes"))
        self.assertFalse(primvar_api.GetPrimvar("onSome"))

    async def test_merge_attrsAsPrimvars(self):
        """Tests treating specific attributes as primvars during merge"""
        args = DEFAULT_ARGS.copy()
        args["treatAsPrimvars"] = ["elementId", "customVec3", "customString"]

        stage = self._open_stage("mergeAttrsAsPrimvars.usda")
        success, result = self._execute_command(args)

        # There should be 2 merged mesh prim.
        prims = [stage.GetPrimAtPath(x) for x in result[2]]
        self.assertEqual(len(prims), 2)

        # check that the first merged mesh has elementId and customString as primvars but not customVec3
        prim = stage.GetPrimAtPath("/merged")
        primvar_api = UsdGeom.PrimvarsAPI(prim)
        self.assertTrue(primvar_api.GetPrimvar("elementId").HasAuthoredValue())
        self.assertTrue(primvar_api.GetPrimvar("customString").HasAuthoredValue())
        self.assertFalse(primvar_api.GetPrimvar("customVec3").HasAuthoredValue())

        # check that the second merged mesh has elementId and customVec3 as primvars but not customString
        prim = stage.GetPrimAtPath("/merged_1")
        primvar_api = UsdGeom.PrimvarsAPI(prim)
        self.assertTrue(primvar_api.GetPrimvar("elementId").HasAuthoredValue())
        self.assertFalse(primvar_api.GetPrimvar("customString").HasAuthoredValue())
        self.assertTrue(primvar_api.GetPrimvar("customVec3").HasAuthoredValue())

        # the unmerged mesh shouldn't have any of the attributes as primvars
        prim = stage.GetPrimAtPath("/World/Cube_05")
        primvar_api = UsdGeom.PrimvarsAPI(prim)
        self.assertFalse(primvar_api.GetPrimvar("elementId").HasAuthoredValue())
        self.assertFalse(primvar_api.GetPrimvar("customString").HasAuthoredValue())
        self.assertFalse(primvar_api.GetPrimvar("customVec3").HasAuthoredValue())

    async def test_merge_inheritedPrimvarMerge(self):
        """Test merging inherited primvar values together"""
        # This example has 6 planes.
        # 2 have an effective blue display color authored on them
        # 2 have an effective green display color authored on their parent
        # 2 have an effective red display color authored on a parent that is above the merge boundary
        red = (1.0, 0.0, 0.0)
        green = (0.0, 1.0, 0.0)
        blue = (0.0, 0.0, 1.0)

        # Execute a merge operation that uses "component" as a merge boundary.
        args = DEFAULT_ARGS.copy()
        args["mergePoint"] = MERGE_POINT_KINDCOMPONENT

        stage = self._open_stage("primvar_inheritance.usda")
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)
        self.assertTrue(result[0])

        # There should be 1 merged mesh prim.
        prims = [stage.GetPrimAtPath(x) for x in result[2]]
        self.assertEqual(len(prims), 1)

        # The mesh should have a uniform green/blue/red display color authored on the mesh.
        prim = prims[0]
        primvar = UsdGeom.PrimvarsAPI(prim).FindPrimvarWithInheritance(UsdGeom.Tokens.primvarsDisplayColor)
        self.assertEqual(primvar.GetInterpolation(), UsdGeom.Tokens.uniform)
        self.assertEqual(primvar.ComputeFlattened(), [green, green, blue, blue, red, red])
        # Assert the source of the primvar
        expected = prim.GetPath()
        returned = primvar.GetAttr().GetPrim().GetPath()
        self.assertEqual(returned, expected)

        # Reopen the stage, block the inherited red primvar and execute again.
        stage = self._open_stage("primvar_inheritance.usda")
        stage.GetPrimAtPath("/World").GetAttribute(UsdGeom.Tokens.primvarsDisplayColor).Block()
        success, result = self._execute_command(args)

        # Now there should be 2 merged mesh prims because the 2 meshes that did not have a display color value (either
        # local or inherited) will have been bucketted separately.
        prims = [stage.GetPrimAtPath(x) for x in result[2]]
        self.assertEqual(len(prims), 2)

        # One mesh should have a uniform green/blue display color authored on the mesh.
        # It can be identified by the fact it has 4 faces.
        prim = _find_mesh_by_face_count(prims, 4)
        self.assertIsNotNone(prim)
        primvar = UsdGeom.PrimvarsAPI(prim).FindPrimvarWithInheritance(UsdGeom.Tokens.primvarsDisplayColor)
        self.assertEqual(primvar.GetInterpolation(), UsdGeom.Tokens.uniform)
        self.assertEqual(primvar.ComputeFlattened(), [green, green, blue, blue])
        # Assert the source of the primvar
        expected = prim.GetPath()
        returned = primvar.GetAttr().GetPrim().GetPath()
        self.assertEqual(returned, expected)

        # One mesh should have no effective display color.
        # It can be identified by the fact it has 2 faces.
        prim = _find_mesh_by_face_count(prims, 2)
        self.assertIsNotNone(prim)
        primvar = UsdGeom.PrimvarsAPI(prim).FindPrimvarWithInheritance(UsdGeom.Tokens.primvarsDisplayColor)
        self.assertFalse(primvar.HasAuthoredValue())

    async def test_merge_displayOpacity(self):
        """Test merging displayOpacity primvar"""
        stage = self._open_stage("primvarOpacity.usda")

        # Assert expected locations of displayOpacity
        self.assertIsNone(stage.GetPrimAtPath("/Cubes").GetAttribute("primvars:displayOpacity").Get())
        self.assertIsNone(stage.GetPrimAtPath("/Cubes/Cube").GetAttribute("primvars:displayOpacity").Get())
        self.assertIsNone(stage.GetPrimAtPath("/Cubes/Cube_01").GetAttribute("primvars:displayOpacity").Get())
        self.assertIsNone(stage.GetPrimAtPath("/Cubes/Cube_02").GetAttribute("primvars:displayOpacity").Get())
        self.assertIsNotNone(stage.GetPrimAtPath("/Planes").GetAttribute("primvars:displayOpacity").Get())
        self.assertIsNotNone(stage.GetPrimAtPath("/Planes/Plane_01").GetAttribute("primvars:displayOpacity").Get())

        # Run merge
        self._execute_json(stage, "primvarOpacity.json")
        prims = _get_meshes(stage)
        self.assertEqual(len(prims), 2)

        # One mesh should have a uniform display opacity authored on the mesh.
        # It can be identified by the fact it has 2 faces.
        prim = _find_mesh_by_face_count(prims, 2)
        self.assertIsNotNone(prim)
        primvar = UsdGeom.PrimvarsAPI(prim).FindPrimvarWithInheritance(UsdGeom.Tokens.primvarsDisplayOpacity)
        self.assertEqual(primvar.GetInterpolation(), UsdGeom.Tokens.uniform)
        flattened = primvar.ComputeFlattened()
        self.assertAlmostEqual(flattened[0], 0.6)
        self.assertAlmostEqual(flattened[1], 0.3)
        # Assert the source of the primvar
        expected = prim.GetPath()
        returned = primvar.GetAttr().GetPrim().GetPath()
        self.assertEqual(returned, expected)

        # The other mesh should have no display opacity
        # It can be identified by the fact it has 18 faces.
        prim = _find_mesh_by_face_count(prims, 18)
        primvar = UsdGeom.PrimvarsAPI(prim).FindPrimvarWithInheritance(UsdGeom.Tokens.primvarsDisplayOpacity)
        self.assertFalse(primvar.HasAuthoredValue())

    async def test_merge_respectSpecifier(self):
        """Test merge where one or more parents of the target has a specifier other than define"""
        stage = self._open_stage("abstractPrims_input.usda")
        self._execute_json(stage, "abstractPrims.json")

        # The merged mesh will have a def specifier, but not be defined because /World/Meshes prim has an over specifier
        overPrim = stage.GetPrimAtPath("/World/Meshes")
        meshPrim = stage.GetPrimAtPath("/World/Meshes/Geo_0/Group_0/MergedMesh")
        self.assertEqual(meshPrim.GetSpecifier(), Sdf.SpecifierDef)
        self.assertEqual(overPrim.GetSpecifier(), Sdf.SpecifierOver)
        self.assertFalse(meshPrim.IsDefined())

        # The merged mesh will have a def specifier, but be abstract because /World/Classes prim has a class specifier
        overPrim = stage.GetPrimAtPath("/World/Classes")
        meshPrim = stage.GetPrimAtPath("/World/Classes/Geo_0/Group_0/MergedMesh")
        self.assertEqual(meshPrim.GetSpecifier(), Sdf.SpecifierDef)
        self.assertEqual(overPrim.GetSpecifier(), Sdf.SpecifierClass)
        self.assertTrue(meshPrim.IsAbstract())

        # The additional group that needs to be created between the last existing prim "/World/Meshes/Geo_0" and the
        # merged mesh path "/World/Meshes/Geo_0/Group_0/MergedMesh" should have "def" specifier and type name "Xform"
        xformPrim = stage.GetPrimAtPath("/World/Meshes/Geo_0/Group_0")
        self.assertEqual(xformPrim.GetSpecifier(), Sdf.SpecifierDef)
        self.assertEqual(xformPrim.GetTypeName(), "Xform")

        xformPrim = stage.GetPrimAtPath("/World/Classes/Geo_0/Group_0")
        self.assertEqual(xformPrim.GetSpecifier(), Sdf.SpecifierDef)
        self.assertEqual(xformPrim.GetTypeName(), "Xform")

    async def test_merge_attributeBucket(self):
        """Test merge buckets subdivisionScheme correctly and sets the attribute on the merged result"""
        stage = self._open_stage("attributeBucket_input.usda")
        self._execute_json(stage, "attributeBucket.json")

        # There should be 4 merged meshes one with each of the subdivisionSchema values. However .. we currently have 5
        # because the fallback value hashes differently to explicit catmulClark despite that being the fallback value
        meshes = [UsdGeom.Mesh(x) for x in stage.GetPseudoRoot().GetChildren() if x.GetTypeName() == "Mesh"]
        self.assertEqual(len(meshes), 5)

        catmullClarkMeshes = [x for x in meshes if x.GetSubdivisionSchemeAttr().Get() == UsdGeom.Tokens.catmullClark]
        noneMeshes = [x for x in meshes if x.GetSubdivisionSchemeAttr().Get() == UsdGeom.Tokens.none]
        bilinearMeshes = [x for x in meshes if x.GetSubdivisionSchemeAttr().Get() == UsdGeom.Tokens.bilinear]
        loopMeshes = [x for x in meshes if x.GetSubdivisionSchemeAttr().Get() == UsdGeom.Tokens.loop]
        self.assertEqual(len(catmullClarkMeshes), 2)
        self.assertEqual(len(noneMeshes), 1)
        self.assertEqual(len(bilinearMeshes), 1)
        self.assertEqual(len(loopMeshes), 1)

    async def test_merge_attributeMatrix(self):
        """Test merge buckets multiple attributes correctly"""
        stage = self._open_stage("attributeMatrix_input.usda")
        self._execute_json(stage, "attributeMatrix.json")

        # The input stage contains 45 meshes with the dot product combination of these attribute values
        #
        #     'doubleSided': ['True', 'False', 'undefined'],
        #     'subdivisionScheme': ['catmullClark', 'none', 'bilinear', 'loop', 'undefined'],
        #     'triangleSundivisionRule': ['catmullClark', 'smooth', 'undefined']
        #
        # These should merge into 16 meshes (2x4x2=16) because 'undefined' values should match the explicit value that
        # are the same as the fallback. However we currently fail to match this way with tokens so we get 30 meshes.
        meshes = [UsdGeom.Mesh(x) for x in stage.GetPseudoRoot().GetChildren() if x.GetTypeName() == "Mesh"]
        self.assertEqual(len(meshes), 30)

    async def test_merge_holeIndices(self):
        """Test that hole indice attributes are merged correctly"""
        stage = self._open_stage("holeIndices.usda")
        self._execute_json(stage, "holeIndices.json")

        # Two meshes with holeIndices authored should have the combined indices on the merged result and the values
        # should have been offest to match the same face on the new mesh
        path = "/World/withHoleIndices"
        attr = UsdGeom.Mesh(stage.GetPrimAtPath(path)).GetHoleIndicesAttr()
        self.assertTrue(attr.HasAuthoredValue())
        self.assertEqual(
            attr.Get(),
            Vt.IntArray(
                (
                    4,
                    13,
                )
            ),
        )

        # Two meshes, one with holeIndices authored and one without should have hole indices authored and the values
        # should have been offest to match the same face on the new mesh. They should be bucketed together.
        path = "/World/mixedHoleIndices"
        attr = UsdGeom.Mesh(stage.GetPrimAtPath(path)).GetHoleIndicesAttr()
        self.assertTrue(attr.HasAuthoredValue())
        self.assertEqual(attr.Get(), Vt.IntArray((13,)))

        # Meshes, without holeIndices authored should not have them authored on the merged mesh
        path = "/World/withoutHoleIndices"
        attr = UsdGeom.Mesh(stage.GetPrimAtPath(path)).GetHoleIndicesAttr()
        self.assertFalse(attr.HasAuthoredValue())

    async def test_merge_inheritedVisibility(self):
        """Test that merging meshes that have inherited visibility buckets correctly and retains visibility"""
        # This case covers;
        # - No authored visibility
        # - Explicit inherited visibility state
        # - Explicit invisible visibility state
        # - Timesampled visibility state
        # - Inherited invisible visibility state from parent
        # - Inherited timesampled visibility state from parent

        stage = self._open_stage("visibility.usda")
        self._execute_json(stage, "visibility.json")

        # The merged meshes should be bucketed into "invisible" and "inherited", which is effectivly "visible".
        # They will be in under the root prim named "World".
        meshes = [UsdGeom.Mesh(x) for x in stage.GetPrimAtPath("/World").GetChildren() if x.IsA(UsdGeom.Mesh)]
        self.assertEqual(len(meshes), 2)

        # There should be one mesh of each visibility state
        visible_meshes = [x for x in meshes if x.GetVisibilityAttr().Get() == UsdGeom.Tokens.inherited]
        invisible_meshes = [x for x in meshes if x.GetVisibilityAttr().Get() == UsdGeom.Tokens.invisible]
        self.assertEqual(len(visible_meshes), 1)
        self.assertEqual(len(invisible_meshes), 1)

        # The input meshes are single faces so the visible mesh should have 1 face and the invisible mesh should have 2
        visible_face_count = len(visible_meshes[0].GetFaceVertexCountsAttr().Get())
        invisible_face_count = len(invisible_meshes[0].GetFaceVertexCountsAttr().Get())
        self.assertEqual(visible_face_count, 4)
        self.assertEqual(invisible_face_count, 2)

        # The meshes with a hidden parent and timesampled visibility sould not be merged.
        # Therefore they will exist at their original path.
        self.assertTrue(stage.GetPrimAtPath("/World/PartA/hiddenParent/mesh"))
        self.assertTrue(stage.GetPrimAtPath("/World/PartA/timesampled"))

        # Because of the timesampled visibility an additional merged meshe shoud be created below "timesampledParent".
        # This should have 2 faces and inherit visibility from the parent.
        prim = stage.GetPrimAtPath("/World/PartA/timesampledParent/merged")
        mesh = UsdGeom.Mesh(prim)
        face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertEqual(mesh.GetVisibilityAttr().Get(), UsdGeom.Tokens.inherited)
        self.assertEqual(face_count, 2)

    async def test_merge_purpose(self):
        """Test that merging meshes that have inherited purpose buckets correctly and retains purpose"""
        stage = self._open_stage("purpose.usda")
        self._execute_json(stage, "purpose.json")

        # The meshes should be split into 4 buckets. Purpose: "default", "render", "proxy", "guide"
        meshes = [UsdGeom.Mesh(x) for x in stage.TraverseAll() if x.IsA(UsdGeom.Mesh)]
        self.assertEqual(len(meshes), 4)

        # There should be one mesh of each purpose value
        default_meshes = [x for x in meshes if x.GetPurposeAttr().Get() == UsdGeom.Tokens.default_]
        render_meshes = [x for x in meshes if x.GetPurposeAttr().Get() == UsdGeom.Tokens.render]
        proxy_meshes = [x for x in meshes if x.GetPurposeAttr().Get() == UsdGeom.Tokens.proxy]
        guide_meshes = [x for x in meshes if x.GetPurposeAttr().Get() == UsdGeom.Tokens.guide]
        self.assertEqual(len(default_meshes), 1)
        self.assertEqual(len(render_meshes), 1)
        self.assertEqual(len(proxy_meshes), 1)
        self.assertEqual(len(guide_meshes), 1)

        # The input meshes are single faces so the merged meshes for explict purposes should have 2 faces
        # but default will have 4 as it will include meshes with no defined purpose
        default_face_count = len(default_meshes[0].GetFaceVertexCountsAttr().Get())
        render_face_count = len(render_meshes[0].GetFaceVertexCountsAttr().Get())
        proxy_face_count = len(proxy_meshes[0].GetFaceVertexCountsAttr().Get())
        guide_face_count = len(guide_meshes[0].GetFaceVertexCountsAttr().Get())
        self.assertEqual(default_face_count, 4)
        self.assertEqual(render_face_count, 2)
        self.assertEqual(proxy_face_count, 2)
        self.assertEqual(guide_face_count, 2)

    async def test_merge_authoredAttributes(self):
        """Test that merging meshes that have different combinations of normals and st bucket correctly"""
        stage = self._open_stage("authoredAttributes.usda")
        self._execute_json(stage, "authoredAttributes.json")

        # The meshes should be split into 3 buckets. "normals", "st", "normals+st"
        prims = [x for x in stage.TraverseAll() if x.IsA(UsdGeom.Mesh)]
        self.assertEqual(len(prims), 3)

        # There should be one mesh of each purpose value
        normals_prims = [x for x in prims if x.HasAttribute("primvars:normals") and not x.HasAttribute("primvars:st")]
        st_prims = [x for x in prims if x.HasAttribute("primvars:st") and not x.HasAttribute("primvars:normals")]
        both_prims = [x for x in prims if x.HasAttribute("primvars:normals") and x.HasAttribute("primvars:st")]
        self.assertEqual(len(normals_prims), 1)
        self.assertEqual(len(st_prims), 1)
        self.assertEqual(len(both_prims), 1)

    async def test_merge_materialAlbedo(self):
        """Test that material albedo is correctly extracted during merge"""
        stage = self._open_stage("materialAlbedo.usda")
        self._execute_json(stage, "materialAlbedo.json")

        prim = stage.GetPrimAtPath("/World/Geometry/MergedMesh")
        mesh = UsdGeom.Mesh(prim)
        self.assertTrue(prim)
        self.assertTrue(mesh)

        # The display color attribute should be authored with values of red, green, blue, 0.2 grey
        displayColor = mesh.GetDisplayColorAttr().Get()
        self.assertEqual(len(displayColor), 4)
        self.assertEqual(displayColor[0], (1.0, 0.0, 0.0))
        self.assertEqual(displayColor[1], (0.0, 1.0, 0.0))
        self.assertEqual(displayColor[2], (0.0, 0.0, 1.0))
        self.assertEqual(displayColor[3], (0.2, 0.2, 0.2))

    async def test_merge_geomsubsetMaterialBinding(self):
        """Test that merging meshes that have materials bound to geom subsets is supported"""
        stage = self._open_stage("geomSubset_materialBinding.usda")
        self._execute_json(stage, "geomSubset_materialBinding.json")

        # Given a mesh with a bound material and no subsets the merged result should not have subsets regardless of the
        # value of "considerMaterials"
        # considerMaterials=True
        primPath = "/NoSubsets_ConsiderMaterialTrue"
        prim = stage.GetPrimAtPath(primPath)
        materialBindSubsets = UsdShade.MaterialBindingAPI(prim).GetMaterialBindSubsets()
        self.assertEqual(len(materialBindSubsets), 0)
        # considerMaterials=False
        primPath = "/NoSubsets_ConsiderMaterialFalse"
        prim = stage.GetPrimAtPath(primPath)
        materialBindSubsets = UsdShade.MaterialBindingAPI(prim).GetMaterialBindSubsets()
        self.assertEqual(len(materialBindSubsets), 0)

        # Given a mix of meshes with different numbers of subsets and a mix of non-overlapping and partition family type
        # we should get a single mesh if we have "considerMaterials=False" and that should have a subset for each
        # material bound to the input meshes but no material bound to the prim itself
        primPath = "/Everything_ConsiderMaterialFalse"
        prim = stage.GetPrimAtPath(primPath)
        materialBindSubsets = UsdShade.MaterialBindingAPI(prim).GetMaterialBindSubsets()
        self.assertEqual(len(materialBindSubsets), 3)
        imageable = UsdGeom.Imageable(prim)
        result, reason = UsdGeom.Subset.ValidateFamily(imageable, UsdGeom.Tokens.face, UsdShade.Tokens.materialBind)
        self.assertTrue(result)

        # Given the same inputs as above but with "considerMaterials=True" we get 2 output meshes because they are
        # bucketed based on the presence or absence of a material bound to the prim itself. This should result in the
        # meshes that are a partition in one bucket and those that are a non overlapping family in the other
        primPath = "/merged"
        prim = stage.GetPrimAtPath(primPath)
        materialBindSubsets = UsdShade.MaterialBindingAPI(prim).GetMaterialBindSubsets()
        self.assertEqual(len(materialBindSubsets), 2)
        imageable = UsdGeom.Imageable(prim)
        result, reason = UsdGeom.Subset.ValidateFamily(imageable, UsdGeom.Tokens.face, UsdShade.Tokens.materialBind)
        self.assertTrue(result)

        primPath = "/merged_1"
        prim = stage.GetPrimAtPath(primPath)
        materialBindSubsets = UsdShade.MaterialBindingAPI(prim).GetMaterialBindSubsets()
        self.assertEqual(len(materialBindSubsets), 3)
        imageable = UsdGeom.Imageable(prim)
        result, reason = UsdGeom.Subset.ValidateFamily(imageable, UsdGeom.Tokens.face, UsdShade.Tokens.materialBind)
        self.assertTrue(result)

    async def test_merge_geomsubsetCollision(self):
        """Test an edge case with material naming that can cause a naming collision when merging multiple meshes with
        geom material subsets"""
        merge_args = DEFAULT_ARGS.copy()
        stage = self._open_stage("mergeGeomSubsetCollision.usda")
        self._execute_command(merge_args)

        # get the merged prim and verify it has 3 subsets
        primPath = "/merged"
        prim = stage.GetPrimAtPath(primPath)
        materialBindSubsets = UsdShade.MaterialBindingAPI(prim).GetMaterialBindSubsets()
        self.assertEqual(len(materialBindSubsets), 3)

        # check the expected subset names
        subset_names = [subset.GetPath() for subset in materialBindSubsets]
        self.assertIn("/merged/Mat", subset_names)
        self.assertIn("/merged/Mat_1", subset_names)
        # This could have been named "Mat_1" but then a collision would have occurred with the existing "Mat_1"
        self.assertIn("/merged/Mat_1_1", subset_names)

    async def test_merge_deletedPrims(self):
        """Test that merging parent child mesh prims in different buckets does not crash"""
        stage = self._open_stage("deletedPrims.usda")
        self._execute_json(stage, "deletedPrims.json")

    async def test_merge_multipleBucketRootPath(self):
        """Test merge function create uniques prims for each bucket if rootPath is supplied"""
        # The input scene produces two buckets
        stage = self._open_stage("doubleSided_input.usda")
        self._execute_json(stage, "multipleBucketRootPath.json")

        # There should be a mesh with the same path as rootPath "/World/Example/MultipleBucket"
        self.assertTrue(stage.GetPrimAtPath("/World/Example/SingleBucket"))
        # There should be 2 meshes produced for the rootPath "/World/Example/MultipleBucket" with suffixes
        self.assertTrue(stage.GetPrimAtPath("/World/Example/MultipleBucket"))
        self.assertTrue(stage.GetPrimAtPath("/World/Example/MultipleBucket_1"))

    async def test_merge_uniqueOutputMeshes(self):
        """Test that running merge multiple times doesn't overwrite existing merged meshes"""

        stage = self._open_stage("cubesUnusedPoints.usda")
        self._execute_json(stage, "uniqueMeshes.json")

        # The JSON file does two merges, we should have two unique merged meshes
        self.assertTrue(stage.GetPrimAtPath("/merged").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/merged_1").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/merged_2").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/merged_3").IsValid())

        # Rerun the commands, we should now have four unique merged meshes
        self._execute_json(stage, "uniqueMeshes.json")

        self.assertTrue(stage.GetPrimAtPath("/merged").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/merged_1").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/merged_2").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/merged_3").IsValid())

    async def test_merge_maxDataVolume(self):
        """Test that merge will not allow meshes greater that the max data volume to be created"""
        # Merge 1600 meshes that have 10000 vertices each
        stage = self._open_stage("maxDataVolume.usdc")
        self._execute_json(stage, "maxDataVolume.json")

        # Assert that 2 merged meshes are generated.
        prim_0 = stage.GetPrimAtPath("/World/Geometry/Merged")
        prim_1 = stage.GetPrimAtPath("/World/Geometry/Merged_1")
        self.assertTrue(prim_0)
        self.assertTrue(prim_1)

        # Neither should have a vertex count greater than 10 million.
        points_0 = UsdGeom.Mesh(prim_0).GetPointsAttr().Get()
        points_1 = UsdGeom.Mesh(prim_1).GetPointsAttr().Get()
        self.assertEqual(len(points_0), 10000000)
        self.assertEqual(len(points_1), 6000000)

    async def test_merge_pointInstancer(self):
        """Test that unguided traversal for merge retains the structure of point instancers"""
        # A point instancer with two prototype hierarchies and some extra meshes in the world
        stage = self._open_stage("pointInstancer.usda")
        self._execute_json(stage, "pointInstancer.json")

        # Assert that there are merged meshes below the point instancer "/World/Scatter".
        prim_0 = stage.GetPrimAtPath("/World/Scatter/Prototypes/PlaneCluster/Mesh")
        prim_1 = stage.GetPrimAtPath("/World/Scatter/Prototypes/CubeCluster/Mesh")
        prim_2 = stage.GetPrimAtPath("/Mesh")
        self.assertTrue(prim_0)
        self.assertTrue(prim_1)
        self.assertTrue(prim_2)

        # Asset the face counts to ensure the merge results dont change.
        points_0 = UsdGeom.Mesh(prim_0).GetPointsAttr().Get()
        points_1 = UsdGeom.Mesh(prim_1).GetPointsAttr().Get()
        points_2 = UsdGeom.Mesh(prim_2).GetPointsAttr().Get()
        self.assertEqual(len(points_0), 168)
        self.assertEqual(len(points_1), 336)
        self.assertEqual(len(points_2), 16)

    async def test_merge_pointInstancerMeshPrototype(self):
        """Test merging a point instancer with a prototype that is a mesh"""

        stage = self._open_stage("pointInstancerMeshPrototype.usda")
        self._execute_json(stage, "pointInstancer.json")

        # Assert the mesh still exists
        prim_0 = stage.GetPrimAtPath("/PointInstancer/Mesh")
        self.assertTrue(prim_0)

        points_0 = UsdGeom.Mesh(prim_0).GetPointsAttr().Get()
        self.assertEqual(len(points_0), 8)

        # Get the point instancer
        prim_1 = stage.GetPrimAtPath("/PointInstancer")
        self.assertTrue(prim_1)
        pointInstancer = UsdGeom.PointInstancer(prim_1)
        self.assertTrue(pointInstancer)

        # Assert there are two prototypes, one that exists and one that doesn't
        prototypes = pointInstancer.GetPrototypesRel().GetTargets()
        self.assertEqual(len(prototypes), 2)
        self.assertIn("/PointInstancer/Mesh", prototypes)
        self.assertIn("/PointInstancer/MeshDoesntExist", prototypes)

        # Double-check the dangling prototype prim doesn't exist
        # Previously this would crash in the merge code.
        prim_2 = stage.GetPrimAtPath("/PointInstancer/MeshDoesntExist")
        self.assertFalse(prim_2)

    async def test_merge_skipTransforms(self):
        """Test the case we can skip applying transforms to points"""

        stage = self._open_stage("transformSkip.usda")
        self._execute_json(stage, "transformSkip.json")

        # Test that merging meshes that don't have an xform to somewhere else
        # that doesn't have an xform works (skips applying anything)
        skipped = stage.GetPrimAtPath("/World/Skipped")
        points = list(skipped.GetAttribute("points").Get())
        expectedSkipped = [
            Gf.Vec3f(50.0, -50.0, -50.0),
            Gf.Vec3f(150.0, -50.0, -50.0),
            Gf.Vec3f(50.0, -50.0, 50.0),
            Gf.Vec3f(150.0, -50.0, 50.0),
            Gf.Vec3f(50.0, 50.0, -50.0),
            Gf.Vec3f(150.0, 50.0, -50.0),
            Gf.Vec3f(150.0, 50.0, 50.0),
            Gf.Vec3f(50.0, 50.0, 50.0),
            Gf.Vec3f(-50.0, 50.0, -50.0),
            Gf.Vec3f(50.0, 50.0, -50.0),
            Gf.Vec3f(-50.0, 50.0, 50.0),
            Gf.Vec3f(50.0, 50.0, 50.0),
            Gf.Vec3f(-50.0, 150.0, -50.0),
            Gf.Vec3f(50.0, 150.0, -50.0),
            Gf.Vec3f(50.0, 150.0, 50.0),
            Gf.Vec3f(-50.0, 150.0, 50.0),
            Gf.Vec3f(-50.0, -50.0, 50.0),
            Gf.Vec3f(50.0, -50.0, 50.0),
            Gf.Vec3f(-50.0, -50.0, 150.0),
            Gf.Vec3f(50.0, -50.0, 150.0),
            Gf.Vec3f(-50.0, 50.0, 50.0),
            Gf.Vec3f(50.0, 50.0, 50.0),
            Gf.Vec3f(50.0, 50.0, 150.0),
            Gf.Vec3f(-50.0, 50.0, 150.0),
        ]
        self.assertEqual(points, expectedSkipped)

        # Test that merging meshes onto an xform that has a different transform
        # applies the expected transformation
        applied = stage.GetPrimAtPath("/World/Xform/Mesh")
        points = list(applied.GetAttribute("points").Get())
        expectedApplied = [
            Gf.Vec3f(-250.0, -350.0, -350.0),
            Gf.Vec3f(-150.0, -350.0, -350.0),
            Gf.Vec3f(-250.0, -350.0, -250.0),
            Gf.Vec3f(-150.0, -350.0, -250.0),
            Gf.Vec3f(-250.0, -250.0, -350.0),
            Gf.Vec3f(-150.0, -250.0, -350.0),
            Gf.Vec3f(-150.0, -250.0, -250.0),
            Gf.Vec3f(-250.0, -250.0, -250.0),
            Gf.Vec3f(-350.0, -250.0, -350.0),
            Gf.Vec3f(-250.0, -250.0, -350.0),
            Gf.Vec3f(-350.0, -250.0, -250.0),
            Gf.Vec3f(-250.0, -250.0, -250.0),
            Gf.Vec3f(-350.0, -150.0, -350.0),
            Gf.Vec3f(-250.0, -150.0, -350.0),
            Gf.Vec3f(-250.0, -150.0, -250.0),
            Gf.Vec3f(-350.0, -150.0, -250.0),
            Gf.Vec3f(-350.0, -350.0, -250.0),
            Gf.Vec3f(-250.0, -350.0, -250.0),
            Gf.Vec3f(-350.0, -350.0, -150.0),
            Gf.Vec3f(-250.0, -350.0, -150.0),
            Gf.Vec3f(-350.0, -250.0, -250.0),
            Gf.Vec3f(-250.0, -250.0, -250.0),
            Gf.Vec3f(-250.0, -250.0, -150.0),
            Gf.Vec3f(-350.0, -250.0, -150.0),
        ]
        self.assertEqual(points, expectedApplied)

    async def test_merge_skipTimesampled(self):
        """Test the handling of timesampled attributes in merge"""
        # Merge 4 meshes where 2 have timesamples for the points attribute and delete the merged meshes
        # One of the animated meshes also has default (static) point value, but the other does not
        stage = self._open_stage("timesampledAttributes.usda")
        self._execute_json(stage, "timesampledAttributes.json")

        # assert that both the animated meshes were not deleted
        prim = stage.GetPrimAtPath("/World/Geo/AnimatedMesh01")
        self.assertTrue(prim.IsValid())
        prim = stage.GetPrimAtPath("/World/Geo/AnimatedMesh02")
        self.assertTrue(prim.IsValid())

        # assert that the merged mesh has the correct number of faces
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/World/Geo/MergedMesh"))
        face_count = len(mesh.GetFaceVertexCountsAttr().Get())
        self.assertEqual(face_count, 18)

    async def test_merge_animated(self):
        """Test the handling of timesampled xformable prims in merge"""
        stage = self._open_stage("animatedXform.usda")
        self._execute_json(stage, "defaultMergeMeshes.json")

        # assert that the mesh with an animated xfrom was not merged
        prim = stage.GetPrimAtPath("/World/Geometry/Mesh03")
        self.assertTrue(prim.IsValid())
        # assert that there is a merged mesh below the animated xform
        prim = stage.GetPrimAtPath("/World/Geometry/Group01/merged")
        self.assertTrue(prim.IsValid())
        # assert that there is a merged mesh under root
        prim = stage.GetPrimAtPath("/merged")
        self.assertTrue(prim.IsValid())
        # assert that there are no meshes below the group that had an unused timesampled xformOp
        children = stage.GetPrimAtPath("/World/Geometry/Group02").GetChildren()
        self.assertEqual(len(children), 0)

    async def test_merge_resetXformStack(self):
        """Test the handling of xformable prims that reset the xform stack"""
        # Given a stage where "/World/Geometry/Xform" resets the xform stack we expect Meshes below that prim to be
        # merged into a new Mesh named "/World/Geometry/Xform/merged". If this merge boundary was not identified then
        # the meshes would be merged into "/World/merged". This would result in the Mesh inheriting the animated
        # transform from "/World" which it does not currently inherit. For the same reason we do not merge Mesh prims
        # that reset the Xfrom stack. So "/World/Geometry/Mesh05" will not be merged.
        expected_path = "/World/Geometry/Xform/merged"

        # Setup merge arguments so a the merge boundary would ideally be /World
        merge_args = DEFAULT_ARGS.copy()
        merge_args["mergePoint"] = MERGE_POINT_ROOTPRIM

        # Load the stage and assert that the expected paths do not exist
        stage = self._open_stage("xformHierarchy.usda")
        self.assertFalse(stage.GetPrimAtPath("/World/Geometry/Xform/merged"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged"))

        # Execute the Merge operation and assert that there are new Meshes at the expected paths
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/Geometry/Xform/merged"))
        self.assertTrue(stage.GetPrimAtPath("/World/merged"))
        self.assertTrue(stage.GetPrimAtPath("/World/Geometry/Mesh05"))

    async def test_merge_timeSampledDefault(self):
        """Test merging underneath an xform with a default value and timesamples"""

        stage = self._open_stage("timeSampledDefaultTransform.usda")
        self._execute_json(stage, "timeSampledDefaultTransform.json")

        # Get prim. It's a simple cube with a known/simple set of points we can validate.
        prim = stage.GetPrimAtPath("/World/Xform/merged")
        self.assertTrue(prim.IsValid())

        # Get points. Validate they match our expectation.
        # This tests that we (currently) use the same timecode for the various UsdGeomXformCaches
        # in the code.
        points = list(UsdGeom.Mesh(prim).GetPointsAttr().Get())
        expectedPoints = [
            Gf.Vec3f(-50.0, -50.0, -50.0),
            Gf.Vec3f(50.0, -50.0, -50.0),
            Gf.Vec3f(-50.0, -50.0, 50.0),
            Gf.Vec3f(50.0, -50.0, 50.0),
            Gf.Vec3f(-50.0, 50.0, -50.0),
            Gf.Vec3f(50.0, 50.0, -50.0),
            Gf.Vec3f(50.0, 50.0, 50.0),
            Gf.Vec3f(-50.0, 50.0, 50.0),
        ]

        self.assertEqual(points, expectedPoints)

    async def test_merge_deletePrims(self):
        """Test how various prims are handled when deleting after merge"""

        stage = self._open_stage("deleteTest.usda")
        self._execute_json(stage, "deleteTest.json")

        # Standard cubes should be deleted
        cube1 = stage.GetPrimAtPath("/World/Cube1")
        self.assertFalse(cube1.IsValid())

        cube2 = stage.GetPrimAtPath("/World/Cube2")
        self.assertFalse(cube2.IsValid())

        # Sublayered into the stage, should be valid, but not active
        refCube1 = stage.GetPrimAtPath("/World/RefCube1")
        self.assertTrue(refCube1.IsValid())
        self.assertFalse(refCube1.IsActive())

        refCube2 = stage.GetPrimAtPath("/World/RefCube2")
        self.assertTrue(refCube2.IsValid())
        self.assertFalse(refCube2.IsActive())

        # Internal referenced cubes should be valid, but not active
        internalRefCube1 = stage.GetPrimAtPath("/World/XformRef1/geometry_1/MeshGeo1")
        self.assertTrue(internalRefCube1.IsValid())
        self.assertFalse(internalRefCube1.IsActive())

        internalRefCube2 = stage.GetPrimAtPath("/World/XformRef2/geometry_1/MeshGeo2")
        self.assertTrue(internalRefCube2.IsValid())
        self.assertFalse(internalRefCube2.IsActive())

        # Reload and test deactivate
        stage = self._open_stage("deleteTest.usda")
        self._execute_json(stage, "deleteTestDeactivate.json")

        # Standard cubes should be deactivated
        cube1 = stage.GetPrimAtPath("/World/Cube1")
        self.assertTrue(cube1.IsValid())
        self.assertFalse(cube1.IsActive())
        self.assertNotEqual(UsdGeom.Imageable(cube1).GetVisibilityAttr().Get(), "invisible")

        cube2 = stage.GetPrimAtPath("/World/Cube1")
        self.assertTrue(cube2.IsValid())
        self.assertFalse(cube2.IsActive())
        self.assertNotEqual(UsdGeom.Imageable(cube2).GetVisibilityAttr().Get(), "invisible")

        # Sublayered into the stage, should be deactivated
        refCube1 = stage.GetPrimAtPath("/World/RefCube1")
        self.assertTrue(refCube1.IsValid())
        self.assertFalse(refCube1.IsActive())
        self.assertNotEqual(UsdGeom.Imageable(refCube1).GetVisibilityAttr().Get(), "invisible")

        refCube2 = stage.GetPrimAtPath("/World/RefCube2")
        self.assertTrue(refCube2.IsValid())
        self.assertFalse(refCube2.IsActive())
        self.assertNotEqual(UsdGeom.Imageable(refCube2).GetVisibilityAttr().Get(), "invisible")

        # Internal referenced cubes should be valid, but not active
        internalRefCube1 = stage.GetPrimAtPath("/World/XformRef1/geometry_1/MeshGeo1")
        self.assertTrue(internalRefCube1.IsValid())
        self.assertFalse(internalRefCube1.IsActive())

        internalRefCube2 = stage.GetPrimAtPath("/World/XformRef2/geometry_1/MeshGeo2")
        self.assertTrue(internalRefCube2.IsValid())
        self.assertFalse(internalRefCube2.IsActive())

        # Reload and test hiding
        stage = self._open_stage("deleteTest.usda")
        self._execute_json(stage, "deleteTestHide.json")

        # Standard cubes should be hidden
        cube1 = stage.GetPrimAtPath("/World/Cube1")
        self.assertTrue(cube1.IsValid())
        self.assertTrue(cube1.IsActive())
        self.assertEqual(UsdGeom.Imageable(cube1).GetVisibilityAttr().Get(), "invisible")

        cube2 = stage.GetPrimAtPath("/World/Cube1")
        self.assertTrue(cube2.IsValid())
        self.assertTrue(cube2.IsActive())
        self.assertEqual(UsdGeom.Imageable(cube2).GetVisibilityAttr().Get(), "invisible")

        # Sublayered into the stage, should be hidden
        refCube1 = stage.GetPrimAtPath("/World/RefCube1")
        self.assertTrue(refCube1.IsValid())
        self.assertTrue(refCube1.IsActive())
        self.assertEqual(UsdGeom.Imageable(refCube1).GetVisibilityAttr().Get(), "invisible")

        refCube2 = stage.GetPrimAtPath("/World/RefCube2")
        self.assertTrue(refCube2.IsValid())
        self.assertTrue(refCube2.IsActive())
        self.assertEqual(UsdGeom.Imageable(refCube2).GetVisibilityAttr().Get(), "invisible")

        # Internal referenced cubes should be valid, active, hidden
        internalRefCube1 = stage.GetPrimAtPath("/World/XformRef1/geometry_1/MeshGeo1")
        self.assertTrue(internalRefCube1.IsValid())
        self.assertTrue(internalRefCube1.IsActive())
        self.assertEqual(UsdGeom.Imageable(refCube2).GetVisibilityAttr().Get(), "invisible")

        internalRefCube2 = stage.GetPrimAtPath("/World/XformRef2/geometry_1/MeshGeo2")
        self.assertTrue(internalRefCube2.IsValid())
        self.assertTrue(internalRefCube2.IsActive())
        self.assertEqual(UsdGeom.Imageable(refCube2).GetVisibilityAttr().Get(), "invisible")

    async def test_merge_instancedPrims(self):
        """Test that merging instanced prims does not change their hierarchy"""
        stage = self._open_stage("instancedCubes.usda")

        # Validate original geometry is there and the merged data is not
        instancedShape1 = stage.GetPrimAtPath("/World/Thing/Shape1")
        self.assertTrue(instancedShape1.IsValid())
        instancedShape2 = stage.GetPrimAtPath("/World/Thing/Shape2")
        self.assertTrue(instancedShape2.IsValid())
        instancedMerged = stage.GetPrimAtPath("/World/Thing/merged")
        self.assertFalse(instancedMerged.IsValid())

        instance1 = stage.GetPrimAtPath("/World/Thing1")
        self.assertTrue(instance1.IsInstance())
        instanceOfShape1 = stage.GetPrimAtPath("/World/Thing1/Shape1")
        self.assertTrue(instanceOfShape1.IsValid())
        instanceOfShape2 = stage.GetPrimAtPath("/World/Thing2/Shape2")
        self.assertTrue(instanceOfShape2.IsValid())
        instanceOfMerged = stage.GetPrimAtPath("/World/Thing2/merged")
        self.assertFalse(instanceOfMerged.IsValid())

        # Merge
        self._execute_json(stage, "defaultMergeMeshes.json")

        # Validate original geo gone, and the merged data is in the correct place
        instancedShape1 = stage.GetPrimAtPath("/World/Thing/Shape1")
        self.assertFalse(instancedShape1.IsValid())
        instancedShape2 = stage.GetPrimAtPath("/World/Thing/Shape2")
        self.assertFalse(instancedShape2.IsValid())
        instancedMerged = stage.GetPrimAtPath("/World/Thing/merged")
        self.assertTrue(instancedMerged.IsValid())

        instance1 = stage.GetPrimAtPath("/World/Thing1")
        self.assertTrue(instance1.IsInstance())
        instanceOfShape1 = stage.GetPrimAtPath("/World/Thing1/Shape1")
        self.assertFalse(instanceOfShape1.IsValid())
        instanceOfShape2 = stage.GetPrimAtPath("/World/Thing2/Shape2")
        self.assertFalse(instanceOfShape2.IsValid())
        instanceOfMerged = stage.GetPrimAtPath("/World/Thing2/merged")
        self.assertTrue(instanceOfMerged.IsValid())

    async def test_merge_subdiv(self):
        """Test that corner and crease attributes are merged correctly"""
        # Merge 4 cube meshes where;
        # 1. Has no corner or crease attributes.
        # 2. Has crease attributes.
        # 3. Has corner attributes.
        # 4. Has corner and crease attributes.
        stage = self._open_stage("subdiv.usda")
        self._execute_json(stage, "defaultMergeMeshes.json")

        # Check there is a single mesh with the points of all 4 cubes
        mesh = UsdGeom.Mesh.Get(stage, "/merged")
        points = mesh.GetPointsAttr().Get()
        self.assertEqual(len(points), 32)

        # Check the attribute values
        expected = Vt.IntArray([4, 5, 6, 7, 12, 13, 14, 15])
        cornerIndices = mesh.GetCornerIndicesAttr().Get()
        self.assertEqual(cornerIndices, expected)

        expected = Vt.FloatArray([0.2, 0.4, 0.6, 0.8, 0.2, 0.4, 0.6, 0.8])
        cornerSharpnesses = mesh.GetCornerSharpnessesAttr().Get()
        self.assertEqual(cornerSharpnesses, expected)

        expected = Vt.IntArray([5, 5])
        creaseLengths = mesh.GetCreaseLengthsAttr().Get()
        self.assertEqual(creaseLengths, expected)

        expected = Vt.IntArray([0, 1, 3, 2, 0, 16, 17, 19, 18, 16])
        creaseIndices = mesh.GetCreaseIndicesAttr().Get()
        self.assertEqual(creaseIndices, expected)

        expected = Vt.FloatArray([10, 10])
        creaseSharpnesses = mesh.GetCreaseSharpnessesAttr().Get()
        self.assertEqual(creaseSharpnesses, expected)

    async def test_merge_mergePoint(self):
        """Test that the merge point options are correctly handled"""

        # Get a copy of the default arguments for this command
        merge_args = DEFAULT_ARGS.copy()

        # 'meshPrimPaths', 'mergePoint', expected_path
        test_matrix = [
            # Strict kind hierarchy
            ("/World", MERGE_POINT_DEFAULT, ""),
            ("/World", MERGE_POINT_XFORM, "/World/Assembly/Group/Component/Subcomponent/Leaf"),
            ("/World", MERGE_POINT_KINDASSEMBLY, "/World/Assembly"),
            ("/World", MERGE_POINT_KINDGROUP, "/World/Assembly/Group"),
            ("/World", MERGE_POINT_KINDCOMPONENT, "/World/Assembly/Group/Component"),
            ("/World", MERGE_POINT_KINDSUBCOMPONENT, "/World/Assembly/Group/Component/Subcomponent"),
            ("/World", MERGE_POINT_ROOTPRIM, "/World"),
            ("/World", MERGE_POINT_PARENTPRIM, "/World/Assembly/Group/Component/Subcomponent/Leaf/Parent"),
            # Relaxed kind hierarchy
            ("/World_kindNone", MERGE_POINT_DEFAULT, ""),
            ("/World_kindNone", MERGE_POINT_XFORM, "/World_kindNone/Assembly/Group/Component/Subcomponent/Leaf"),
            ("/World_kindNone", MERGE_POINT_KINDASSEMBLY, "/World_kindNone/Assembly"),
            ("/World_kindNone", MERGE_POINT_KINDGROUP, "/World_kindNone/Assembly/Group"),
            ("/World_kindNone", MERGE_POINT_KINDCOMPONENT, "/World_kindNone/Assembly/Group/Component"),
            ("/World_kindNone", MERGE_POINT_KINDSUBCOMPONENT, "/World_kindNone/Assembly/Group/Component/Subcomponent"),
            ("/World_kindNone", MERGE_POINT_ROOTPRIM, "/World_kindNone"),
            (
                "/World_kindNone",
                MERGE_POINT_PARENTPRIM,
                "/World_kindNone/Assembly/Group/Component/Subcomponent/Leaf/Parent",
            ),
            # Sparse kind hierarchy
            ("/World_fallback", MERGE_POINT_DEFAULT, ""),
            ("/World_fallback", MERGE_POINT_XFORM, "/World_fallback/Assembly/Group/Component/Subcomponent/Leaf"),
            ("/World_fallback", MERGE_POINT_KINDASSEMBLY, ""),
            ("/World_fallback", MERGE_POINT_KINDGROUP, "/World_fallback/Assembly/Group"),
            ("/World_fallback", MERGE_POINT_KINDCOMPONENT, "/World_fallback/Assembly/Group"),
            ("/World_fallback", MERGE_POINT_KINDSUBCOMPONENT, "/World_fallback/Assembly/Group"),
            ("/World_fallback", MERGE_POINT_ROOTPRIM, "/World_fallback"),
            (
                "/World_fallback",
                MERGE_POINT_PARENTPRIM,
                "/World_fallback/Assembly/Group/Component/Subcomponent/Leaf/Parent",
            ),
        ]
        msg_pattern = 'Merge Geometry for "{}" with merge point "{}" does not produce expected prim "{}"'

        # Iterate over all the cases in the test matrix asserting that they generate the expected result
        for meshPrimPath, mergePoint, parent_path in test_matrix:

            # Modify the arguments for this test case
            merge_args["meshPrimPaths"] = [meshPrimPath]
            merge_args["rootPath"] = "Merged"
            merge_args["mergePoint"] = mergePoint

            # Build the expected merged mesh path and assertion message for faliures
            expected_path = parent_path + "/Merged"
            msg = msg_pattern.format(meshPrimPath, mergePoint, expected_path)

            # Open a fresh copy of the stage.
            stage = self._open_stage("mergePoint.usda")

            # Assert that the expected prim does not already exist.
            self.assertFalse(stage.GetPrimAtPath(expected_path).IsValid(), msg)

            # execute the operation via commands
            self._execute_command(merge_args)

            # Assert that the expected prim does exist.
            self.assertTrue(stage.GetPrimAtPath(expected_path).IsValid(), msg)

        # When mesh prims are passed in below a merge point that point should still be respected.
        # Get a fresh copy of the default arguments for this command.
        merge_args = DEFAULT_ARGS.copy()
        merge_args["mergePoint"] = MERGE_POINT_KINDCOMPONENT
        merge_args["rootPath"] = "Merged"
        merge_args["meshPrimPaths"] = ["/World/Component/Mesh0", "/World/Component/Mesh1"]

        # Open the stage and execute the operation via commands.
        stage = self._open_stage("simpleComponent.usda")
        self._execute_command(merge_args)

        # Assert that prims now exist below the component rather than at the root.
        self.assertFalse(stage.GetPrimAtPath("/Merged").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/Component/Merged").IsValid())

        # Relative paths with multiple elements should produce output meshes with that path relative to each of the hard
        # points in the stage. In this case the hard points are "/World" and "/World/Component" due to their Kinds.
        merge_args["rootPath"] = "Geometry/Mesh"

        # Open stage and execute the operation via commands
        stage = self._open_stage("simpleComponent.usda")
        self._execute_command(merge_args)

        # Assert that prims now exist at the paths we expect
        self.assertFalse(stage.GetPrimAtPath("/World/Geometry/Mesh").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/Component/Geometry/Mesh").IsValid())

    async def test_merge_relativeOutputName(self):
        """Test using a relative output name on a simple scene doesn't crash"""

        # Open stage
        stage = self._open_stage("primvarMerge.usda")
        self._execute_json(stage, "relativePathCrash.json")

        # Get the merged prim and assert it is valid, and we didn't crash.
        prim = stage.GetPrimAtPath("/TestMesh")
        self.assertTrue(prim.IsValid())

    async def test_merge_outputPivot(self):
        """Test that a logical pivot is set on output meshes"""
        # Get a copy of the default arguments for this command
        merge_args = DEFAULT_ARGS.copy()
        merge_args["mergePoint"] = MERGE_POINT_KINDCOMPONENT

        # execute the operation via commands
        stage = self._open_stage("mergeCenterPivot.usd")
        self._execute_command(merge_args)

        # Assert the expected pivot values
        prim = stage.GetPrimAtPath("/World/Component_01/merged")
        _, _, _, pivot, _ = UsdGeom.XformCommonAPI(prim).GetXformVectors(Usd.TimeCode.Default())
        self.assertEqual(pivot, Gf.Vec3f(0.0, 1.0, 0.0))

        prim = stage.GetPrimAtPath("/World/Component_02/merged")
        _, _, _, pivot, _ = UsdGeom.XformCommonAPI(prim).GetXformVectors(Usd.TimeCode.Default())
        self.assertEqual(pivot, Gf.Vec3f(0.0, 1.0, 0.0))

        prim = stage.GetPrimAtPath("/World/Component_03/merged")
        _, _, _, pivot, _ = UsdGeom.XformCommonAPI(prim).GetXformVectors(Usd.TimeCode.Default())
        # Assert almost equal due to double to float precision differences.
        self.assertAlmostEqual(pivot[0], -1.0, delta=0.000001)
        self.assertAlmostEqual(pivot[1], 1.0, delta=0.000001)
        self.assertAlmostEqual(pivot[2], 0.0, delta=0.000001)

    async def test_merge_singleOutputNames(self):
        """Test that the names of output meshes obey the expected suffixing when a single mesh is produced"""
        # When a single output mesh is generated the name should be "merged" both when no rootPath is specified and
        # when the name "merged" is supplied.

        # Setup merge arguments so a single mesh is produced
        merge_args = DEFAULT_ARGS.copy()
        merge_args["mergePoint"] = MERGE_POINT_ROOTPRIM
        merge_args["originalGeomOption"] = 0
        merge_args["meshPrimPaths"] = ["/World/Example/DefaultSided/mesh", "/World/Example/DoubleSided/mesh"]

        # Load the stage and assert that the expected paths do not exist
        stage = self._open_stage("doubleSided_input.usda")
        self.assertFalse(stage.GetPrimAtPath("/World/merged"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_1"))

        # execute the operation via commands with no defined output.
        merge_args["rootPath"] = ""

        # On the first run a non suffixed prim name should be generated
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged"))

        # When run a second time the desired name is already in use so a numeric suffix will be added
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged_1"))

        # Reload the stage and assert that the expected paths do not exist
        stage = self._open_stage("doubleSided_input.usda")
        self.assertFalse(stage.GetPrimAtPath("/World/merged"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_1"))

        # execute the operation via commands with "merged" as the output name and assert that the expected path exists.
        merge_args["rootPath"] = "merged"

        # On the first run a non suffixed prim name should be generated
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged"))

        # When run a second time the desired name is already in use so a numeric suffix will be added
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged_1"))

    async def test_merge_multipleOutputNames(self):
        """Test that the names of output meshes obey the expected suffixing when multiple meshes are produced"""
        # When multiple output meshs are generated the first name should be "merged" both when no rootPath is specified and
        # when the name "merged" is supplied, the subsequent names will have unique numeric suffixes.

        # Setup merge arguments so a single mesh is produced
        merge_args = DEFAULT_ARGS.copy()
        merge_args["mergePoint"] = MERGE_POINT_ROOTPRIM
        merge_args["originalGeomOption"] = 0
        merge_args["meshPrimPaths"] = [
            "/World/Example/DefaultSided/mesh",
            "/World/Example/DoubleSided/mesh",
            "/World/Example/SingleSided/mesh",
        ]
        merge_args["allowSingleMeshes"] = True

        # Load the stage and assert that the expected paths do not exist
        stage = self._open_stage("doubleSided_input.usda")
        self.assertFalse(stage.GetPrimAtPath("/World/merged"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_1"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_2"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_3"))

        # execute the operation via commands with no defined output.
        merge_args["rootPath"] = ""

        # On the first run a non suffixed prim name should be used for the first mesh but the second will have a suffix
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged"))
        self.assertTrue(stage.GetPrimAtPath("/World/merged_1"))

        # When run a second time the desired names are already in use so a numeric suffix will be added to both
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged_2"))
        self.assertTrue(stage.GetPrimAtPath("/World/merged_3"))

        # Reload the stage and assert that the expected paths do not exist
        stage = self._open_stage("doubleSided_input.usda")
        self.assertFalse(stage.GetPrimAtPath("/World/merged"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_1"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_2"))
        self.assertFalse(stage.GetPrimAtPath("/World/merged_3"))

        # execute the operation via commands with "merged" as the output name and assert that the expected path exists.
        merge_args["rootPath"] = "merged"

        # On the first run a non suffixed prim name should be used for the first mesh but the second will have a suffix
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged"))
        self.assertTrue(stage.GetPrimAtPath("/World/merged_1"))

        # When run a second time the desired names are already in use so a numeric suffix will be added to both
        self._execute_command(merge_args)
        self.assertTrue(stage.GetPrimAtPath("/World/merged_2"))
        self.assertTrue(stage.GetPrimAtPath("/World/merged_3"))

    async def test_merge_meshesWithChildren(self):
        """Test that we skip Meshes with child prims that Merge does not handle"""
        # Setup merge arguments.
        merge_args = DEFAULT_ARGS.copy()
        merge_args["originalGeomOption"] = 1
        merge_args["considerMaterials"] = False
        merge_args["allowSingleMeshes"] = True

        # Load the stage and assert that the expected paths exist before hand.
        stage = self._open_stage("mergeChildPrims.usda")
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh/SubMesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMaterials/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMaterials/Mesh/Material"))
        self.assertTrue(stage.GetPrimAtPath("/childSubsets/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childSubsets/Mesh/green"))
        self.assertTrue(stage.GetPrimAtPath("/childSubsets/Mesh/red"))

        # Execute the operation via commands.
        self._execute_command(merge_args)

        # The Mesh that has child Meshes should still exist as should the child Mesh.
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh/SubMesh"))

        # The Mesh that has child Materials should still exist as should the child Materials.
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh/SubMesh"))

        # The Mesh that has child Subsets should have been Merge as should the Subsets.
        self.assertFalse(stage.GetPrimAtPath("/childSubsets/Mesh"))
        self.assertFalse(stage.GetPrimAtPath("/childSubsets/Mesh/green"))
        self.assertFalse(stage.GetPrimAtPath("/childSubsets/Mesh/red"))

        # Re-Load the stage and assert that the expected paths exist before hand.
        stage = self._open_stage("mergeChildPrims.usda")
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMeshes/Mesh/SubMesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMaterials/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childMaterials/Mesh/Material"))
        self.assertTrue(stage.GetPrimAtPath("/childSubsets/Mesh"))
        self.assertTrue(stage.GetPrimAtPath("/childSubsets/Mesh/green"))
        self.assertTrue(stage.GetPrimAtPath("/childSubsets/Mesh/red"))

        # Deactivate the child prims so that all the meshes will be merged.
        stage.GetPrimAtPath("/childMeshes/Mesh/SubMesh").SetActive(False)
        stage.GetPrimAtPath("/childMaterials/Mesh/Material").SetActive(False)

        # Execute the operation via commands.
        self._execute_command(merge_args)

        # The Meshes and child prims should have been Merged.
        self.assertFalse(stage.GetPrimAtPath("/childMeshes/Mesh"))
        self.assertFalse(stage.GetPrimAtPath("/childMeshes/Mesh/SubMesh"))
        self.assertFalse(stage.GetPrimAtPath("/childMaterials/Mesh"))
        self.assertFalse(stage.GetPrimAtPath("/childMaterials/Mesh/Material"))
        self.assertFalse(stage.GetPrimAtPath("/childSubsets/Mesh"))
        self.assertFalse(stage.GetPrimAtPath("/childSubsets/Mesh/green"))
        self.assertFalse(stage.GetPrimAtPath("/childSubsets/Mesh/red"))

    async def test_merge_singleMesh(self):
        """Test single meshes are not merged (by default)"""

        # First use the default merge args
        merge_args = DEFAULT_ARGS.copy()

        stage = self._open_stage("simpleCube.usda")

        # Assert initial expected state - a single cube, no merged result
        self.assertTrue(stage.GetPrimAtPath("/World/Cube"))
        self.assertFalse(stage.GetPrimAtPath("/merged"))

        # Execute merge
        self._execute_command(merge_args)

        # Assert the state is the same as before - the single cube was not merged
        self.assertTrue(stage.GetPrimAtPath("/World/Cube"))
        self.assertFalse(stage.GetPrimAtPath("/merged"))

        # Enable allow single meshes, then re-test
        merge_args["allowSingleMeshes"] = True

        self._execute_command(merge_args)

        # Assert the cube was now "merged"
        self.assertFalse(stage.GetPrimAtPath("/World/Cube"))
        self.assertTrue(stage.GetPrimAtPath("/merged"))

    async def test_merge_withExecutionContext(self):
        """Test that execution context is respected"""
        # Open a stage without using the omni.usd.context
        file_path = _get_test_data_file_path("doubleSided_output.usda")
        stage = Usd.Stage.Open(file_path)

        # Build an execution context with the custom stage, enable reporting
        context = _get_context(stage, report=True)

        # The report path should be None before execution
        self.assertIsNone(context.reportPath)

        # Execute with default args but custom execution context
        success, result = self._execute_command(DEFAULT_ARGS.copy(), context=context)

        # Assert the operation was successful and we got a result
        self.assertTrue(success)
        self.assertIsNotNone(result)

        # Assert that the report path has now been populated
        self.assertIsNotNone(context.reportPath)

    async def test_merge_invalid_primvars(self):
        """Test merging meshes with invalid primvars"""

        stage = self._open_stage("invalidPrimvars.usda")

        cube1 = stage.GetPrimAtPath("/World/ExtraNormals1")
        cube2 = stage.GetPrimAtPath("/World/ExtraNormals2")

        # Initial data is incorrect for both, they have 24 normals (faceVarying)
        # but no interpolation specified, meaning they are vertex
        normals1 = cube1.GetAttribute("normals").Get()
        self.assertEqual(len(normals1), 24)

        normals2 = cube2.GetAttribute("normals").Get()
        self.assertEqual(len(normals2), 24)

        # Run merge
        merge_args = DEFAULT_ARGS.copy()
        merge_args["meshPrimPaths"] = ["/World/ExtraNormals1", "/World/ExtraNormals2"]

        self._execute_command(merge_args)

        # After merge, we should only have vertex length normals (8x2)
        # as we truncated them to avoid crashing.
        merged = stage.GetPrimAtPath("/merged")
        self.assertTrue(merged)

        papi = UsdGeom.PrimvarsAPI(merged)
        primvar = papi.GetPrimvar("normals")
        normalsMerged = primvar.ComputeFlattened()
        self.assertEqual(len(normalsMerged), 16)

        # Assert that all of the normals values are less than the excess ones,
        # which are all >=100.0 for ease of debugging
        for normal in normalsMerged:
            for n in normal:
                self.assertLess(n, 100.0)

        # Now test truncated normals
        # Initially we should have two meshes that only have three values each for normals
        trunc1 = stage.GetPrimAtPath("/World/TruncatedNormals1")
        trunc2 = stage.GetPrimAtPath("/World/TruncatedNormals2")
        normals1 = trunc1.GetAttribute("normals").Get()
        self.assertEqual(len(normals1), 3)
        normals2 = trunc2.GetAttribute("normals").Get()
        self.assertEqual(len(normals2), 3)

        # Re-run Merge
        merge_args["meshPrimPaths"] = ["/World/TruncatedNormals1", "/World/TruncatedNormals2"]
        self._execute_command(merge_args)

        merged = stage.GetPrimAtPath("/merged_1")
        self.assertTrue(merged)

        # Now we should have 16 normals (expanded to the expected default vertex interpolation)
        # even though there were fewer to begin with.
        # This will pad with default values (0.0), which isn't ideal but ultimately it's
        # mostly about not crashing when invalid data is provided.
        papi = UsdGeom.PrimvarsAPI(merged)
        primvar = papi.GetPrimvar("normals")
        normalsMerged = primvar.ComputeFlattened()
        self.assertEqual(len(normalsMerged), 16)

        # Assert that the expected zero padded values are found
        # Each mesh should have 3 valid normals followed by 5 padded normals,
        # so 3-7 + 11-15 are the padded values.
        for i, normal in enumerate(normalsMerged):
            if 3 <= i <= 7 or 11 <= i <= 15:
                self.assertEqual(normal, Gf.Vec3f(0.0, 0.0, 0.0))
            else:
                self.assertNotEqual(normal, Gf.Vec3f(0.0, 0.0, 0.0))

    async def test_merge_path_expression(self):
        """Test merging prims with path expressions"""

        stage = self._open_stage("mergeRegex.usda")

        # Assert initial state
        self.assertEqual(len(_get_meshes(stage)), 6)

        # Run default merge of all prims
        merge_args = DEFAULT_ARGS.copy()
        merge_args["meshPrimPaths"] = []
        self._execute_command(merge_args)

        # Assert all meshes merged into one
        self.assertEqual(len(_get_meshes(stage)), 1)
        self.assertIsNotNone(stage.GetPrimAtPath("/merged"))

        # ^Cube
        stage = self._open_stage("mergeRegex.usda")
        merge_args = DEFAULT_ARGS.copy()
        merge_args["meshPrimPaths"] = ["//Cube*"]
        self._execute_command(merge_args)

        # One merged mesh and the remaining two that shouldn't match
        self.assertEqual(len(_get_meshes(stage)), 3)
        self.assertIsNotNone(stage.GetPrimAtPath("/World/SomeCube_01"))
        self.assertIsNotNone(stage.GetPrimAtPath("/World/SomeCube_02"))

        # (Some)?Cube.*
        stage = self._open_stage("mergeRegex.usda")
        merge_args = DEFAULT_ARGS.copy()

        merge_args["meshPrimPaths"] = ["//*Cube*"]
        self._execute_command(merge_args)

        # Everything should have merged
        self.assertEqual(len(_get_meshes(stage)), 1)

    async def test_merge_explicit_invisible_prims(self):
        """Test merging an invisible prim hierarchy by explicitly targetting it"""

        stage = self._open_stage("mergeCastle.usda")

        # Assert initial state
        self.assertFalse(_is_visible(stage.GetPrimAtPath("/World/Prototypes")))
        self.assertTrue(_is_visible(stage.GetPrimAtPath("/World/Geometry")))

        # Get all meshes
        # This is the prototypes and the references to them
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 15)

        visible_meshes = list()
        invisible_meshes = list()
        for mesh in meshes:
            if _is_visible(mesh):
                visible_meshes.append(mesh)
            else:
                invisible_meshes.append(mesh)

        # 3 hidden prototypes, the rest are references to them
        self.assertEqual(len(visible_meshes), 12)
        self.assertEqual(len(invisible_meshes), 3)

        # Run a default merge
        merge_args = DEFAULT_ARGS.copy()
        self._execute_command(merge_args)

        # Assert default merge state
        # This should be that the Geometry meshes were merged and deactivated,
        # but nothing in Prototypes was touched.
        self.assertIsNotNone(stage.GetPrimAtPath("/merged"))

        # Assert all meshes under /World/Geometry were merged and now inactive
        it = iter(Usd.PrimRange.AllPrims(stage.GetPrimAtPath("/World/Geometry")))
        for prim in it:
            if prim.IsA(UsdGeom.Mesh):
                self.assertFalse(prim.IsActive())

        # Now re-open stage and configure a merge to explicitly merge /World/Prototypes
        stage = self._open_stage("mergeCastle.usda")

        # Assert some initial Prototypes state
        self.assertEqual(len(stage.GetPrimAtPath("/World/Prototypes/Part1").GetChildren()), 2)
        self.assertEqual(len(stage.GetPrimAtPath("/World/Prototypes/Part2").GetChildren()), 1)

        merge_args = DEFAULT_ARGS.copy()
        merge_args["meshPrimPaths"] = ["/World/Prototypes"]
        merge_args["mergePoint"] = MERGE_POINT_KINDCOMPONENT
        self._execute_command(merge_args)

        # There are now fewer meshes as the Prototypes were merged, and anything
        # referencing them picks up the benefit.
        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 10)

        # Assert new Prototypes state
        self.assertEqual(len(stage.GetPrimAtPath("/World/Prototypes/Part1").GetChildren()), 1)
        self.assertEqual(len(stage.GetPrimAtPath("/World/Prototypes/Part2").GetChildren()), 1)

        # And finally, the meshes under ../Part1 should have been merged
        self.assertIsNotNone(stage.GetPrimAtPath("/World/Prototypes/Part1/merged"))

    async def test_time_varying_meshes(self):
        """Test merge operation on meshes with authored time varying attributes"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

    async def test_parent_xform_expression(self):
        """Test merging with Parent Xform boundary and a subset of meshes"""

        stage = self._open_stage("mergeXform.usda")

        # Assert original state of scene
        self.assertTrue(stage.GetPrimAtPath("/World/Foo/Cube"))
        self.assertTrue(stage.GetPrimAtPath("/World/Foo/Cube_01"))
        self.assertTrue(stage.GetPrimAtPath("/World/Foo/DontMerge"))

        # No merged prim to begin with
        self.assertFalse(stage.GetPrimAtPath("/World/Foo/merged"))

        # Merge the "Cube*" prims
        args = DEFAULT_ARGS.copy()
        args["mergePoint"] = MERGE_POINT_XFORM
        args["meshPrimPaths"] = ["/World/Foo/Cube*"]

        success, result = self._execute_command(args)
        self.assertTrue(success)

        # Assert expected merged state
        # Wasn't merged
        self.assertTrue(stage.GetPrimAtPath("/World/Foo/DontMerge"))

        # Were merged and removed
        self.assertFalse(stage.GetPrimAtPath("/World/Foo/Cube"))
        self.assertFalse(stage.GetPrimAtPath("/World/Foo/Cube_01"))

        # New merged prim exists
        self.assertTrue(stage.GetPrimAtPath("/World/Foo/merged"))

    async def test_merging_to_missing_output_path(self):
        """Test merging meshes to a location that doesn't exist yet"""

        stage = self._open_stage("simpleFourCubesXformed.usda")

        args = DEFAULT_ARGS.copy()
        args["mergePoint"] = MERGE_POINT_ROOTPRIM
        args["rootPath"] = "Geometry/Merged/merged_mesh"
        args["considerMaterials"] = True
        args["allowSingleMeshes"] = True

        success, result = self._execute_command(args)
        self.assertTrue(result[0])

        # Get the merged mesh
        prim = stage.GetPrimAtPath("/World/Geometry/Merged/merged_mesh")
        self.assertTrue(prim)

        # Assert expected number of merged vertices
        points = prim.GetAttribute("points").Get()
        self.assertEqual(len(points), 32)

        # Assert none of the points are (-)inf
        for point in points:
            for i in range(3):
                self.assertNotEqual(abs(point[i]), float("inf"))

        # The original /World had a transform on it, so now assert that
        # we compensated for that and the prim is in the correct place.
        extent = prim.GetAttribute("extent").Get()
        self.assertEqual(extent[0], Gf.Vec3f(-50.0, -50.0, -250.0))
        self.assertEqual(extent[1], Gf.Vec3f(250.0, 50.0, 50.0))

        bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), includedPurposes=[UsdGeom.Tokens.default_])
        worldBBox = bboxCache.ComputeWorldBound(prim)
        alignedRange = worldBBox.ComputeAlignedRange()
        expectedRange = Gf.Range3d(Gf.Vec3d(-350.0, -50.0, -250.0), Gf.Vec3d(-50.0, 50.0, 50.0))
        self.assertEqual(alignedRange, expectedRange)
