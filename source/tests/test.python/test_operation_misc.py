# SPDX-FileCopyrightText: Copyright (c) 2021-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core import SceneOptimizerCore
from pxr import UsdGeom, UsdShade

from .scripts import commands, standalone
from .test_utils import Test_Operation, _get_context, _get_meshes, _get_test_data_file_path

# OriginalGeometryOption values
ORIGINAL_GEOM_IGNORE = 0
ORIGINAL_GEOM_DELETE = 1

DUPLICATE_METHOD_INSTANCEABLE_REFERENCE = 2


def _tot_vertex_count(prims):
    verts = [len(UsdGeom.Mesh(prim).GetPointsAttr().Get()) for prim in prims]
    return sum(verts)


def _tot_normal_count(prims):
    tot_normals = 0
    for prim in prims:
        mesh = UsdGeom.Mesh(prim)
        primvars_api = UsdGeom.PrimvarsAPI(prim)
        normalPrimvars = primvars_api.GetPrimvar("primvars:normals").GetAttr()
        if normalPrimvars:
            normals = len(normalPrimvars.Get())
        else:  # pragma: no cover
            # This condition not currently used in the existing test data
            normals = len(mesh.GetNormalsAttr().Get())
        tot_normals += normals
    return tot_normals


def _tot_normal_indices_count(prims):
    tot_normals_indices = 0
    for prim in prims:
        primvars_api = UsdGeom.PrimvarsAPI(prim)
        if primvars_api.GetPrimvar("primvars:normals"):
            normalIndicesPrimvars = primvars_api.GetPrimvar("primvars:normals").GetIndicesAttr()
            if normalIndicesPrimvars:
                normals_indices = len(normalIndicesPrimvars.Get())
                tot_normals_indices += normals_indices
    return tot_normals_indices


def _tot_st_count(prims):
    # Return count of STs and ST indices
    tot_st = 0
    tot_st_indices = 0
    for prim in prims:
        primvars_api = UsdGeom.PrimvarsAPI(prim)
        if primvars_api.HasPrimvar("primvars:st"):
            num_st = len(primvars_api.GetPrimvar("primvars:st").GetAttr().Get())
            tot_st += num_st
            stIndicesPrimvars = primvars_api.GetPrimvar("primvars:st").GetIndicesAttr()
            if stIndicesPrimvars:
                num_st_indices = len(stIndicesPrimvars.Get())
                tot_st_indices += num_st_indices

    return tot_st, tot_st_indices


def _tot_color_count(prims):
    # Return count of displayColor and displayColor:indices
    tot_dc = 0
    tot_dc_indices = 0
    for prim in prims:
        primvars_api = UsdGeom.PrimvarsAPI(prim)
        if primvars_api.HasPrimvar("primvars:displayColor"):
            num_dc_indices = len(primvars_api.GetPrimvar("primvars:displayColor").GetIndicesAttr().Get())
            num_dc = len(primvars_api.GetPrimvar("primvars:displayColor").GetAttr().Get())
            tot_dc += num_dc
            tot_dc_indices += num_dc_indices

    return tot_dc, tot_dc_indices


def _get_color(prim, index):
    primvars_api = UsdGeom.PrimvarsAPI(prim)
    colors = primvars_api.GetPrimvar("primvars:displayColor").Get()
    return colors[index]


class Test(Test_Operation):
    async def setUp(self):
        await super().setUp()
        self._merge_args = {
            "meshPrimPaths": [],
            "considerMaterials": False,
            "materialAlbedoAsVertexColors": False,
            "originalGeomOption": ORIGINAL_GEOM_IGNORE,
            "rootPath": "",
            "allowSingleMeshes": True,
        }

        self._merge_at_leaf_args = {
            "meshPrimPaths": [],
            "considerMaterials": False,
            "materialAlbedoAsVertexColors": False,
            "originalGeomOption": ORIGINAL_GEOM_DELETE,
            "rootPath": "",
            "mergePoint": 1,
        }

        self._pivot_args = {
            "meshPrimPaths": [],
        }

        self._deduplicate_geometry_args = {
            "meshPrimPaths": [],
            "duplicateMethod": DUPLICATE_METHOD_INSTANCEABLE_REFERENCE,
            "considerDeepTransforms": False,
            "tolerance": 0.001,
            "fuzzy": False,
            "useGpu": False,
            "allowScaling": False,
        }

        self._deep_deduplicate_geometry_args = {
            "meshPrimPaths": [],
            "duplicateMethod": DUPLICATE_METHOD_INSTANCEABLE_REFERENCE,
            "considerDeepTransforms": True,
            "tolerance": 0.001,
            "fuzzy": False,
            "useGpu": False,
            "allowScaling": False,
        }

    async def _do_merge(self, stage, args: dict):
        before_meshes = _get_meshes(stage)
        context = _get_context(stage)
        success, _, _ = SceneOptimizerCore.getInstance().executeOperation("merge", context, args)
        self.assertTrue(success)
        after_meshes = _get_meshes(stage)
        new_meshes = set(after_meshes) - set(before_meshes)
        return before_meshes, after_meshes, new_meshes

    async def _do_deduplicate_geometry(self, stage, args: dict):
        before_meshes = _get_meshes(stage)
        context = _get_context(stage)
        SceneOptimizerCore.getInstance().executeOperation("deduplicateGeometry", context, args)
        after_meshes = _get_meshes(stage)
        new_meshes = set(after_meshes) - set(before_meshes)
        return before_meshes, after_meshes, new_meshes

    async def test_Merge_no_considerMaterials(self):
        # Test case is 3 meshes with different materials.
        # This case ignores material diffs and merges all meshes into one.
        stage = self._open_stage("threeLambertShaders.usd")
        before_meshes, after_meshes, new_meshes = await self._do_merge(stage, self._merge_args)

        # Check vertex counts on all meshes
        self.assertEqual(24, _tot_vertex_count(before_meshes))
        self.assertEqual(48, _tot_vertex_count(after_meshes))
        self.assertEqual(24, _tot_vertex_count(new_meshes))

        # Single mesh expected at end
        self.assertEqual(len(new_meshes), 1)

    async def test_Merge_considerMaterials(self):
        # Test case is 3 meshes with different materials.
        # This case considers material diffs so will not merge meshes.
        stage = self._open_stage("threeLambertShaders.usd")
        _merge_args = self._merge_args
        _merge_args["considerMaterials"] = True
        before_meshes, after_meshes, new_meshes = await self._do_merge(stage, self._merge_args)

        # Check vertex counts on all meshes
        self.assertEqual(24, _tot_vertex_count(before_meshes))
        self.assertEqual(48, _tot_vertex_count(after_meshes))
        self.assertEqual(24, _tot_vertex_count(new_meshes))

        # No merging expected, so should see all 3 meshes at end.
        self.assertEqual(len(new_meshes), 3)

    async def test_Merge_no_uvs(self):
        # Test case is 3 meshes with different materials.
        # This case ignores material diffs and merges all meshes into one.
        stage = self._open_stage("meshNoUVs.usd")
        before_meshes, after_meshes, new_meshes = await self._do_merge(stage, self._merge_args)

        # Check vertex counts on all meshes
        self.assertEqual(8, _tot_vertex_count(before_meshes))
        self.assertEqual(16, _tot_vertex_count(after_meshes))
        self.assertEqual(8, _tot_vertex_count(new_meshes))

        # Single mesh expected at end
        self.assertEqual(len(new_meshes), 1)

        # No UVs in input mesh
        num_st, num_st_indices = _tot_st_count(before_meshes)
        self.assertEqual(num_st, 0)
        self.assertEqual(num_st_indices, 0)

        # Verify we do not have empty/invalid UVs added
        num_st, num_st_indices = _tot_st_count(new_meshes)
        self.assertEqual(num_st, 0)
        self.assertEqual(num_st_indices, 0)

    async def test_Merge_faceVarying_uvs(self):
        # Test case is 3 meshes with different materials.
        # This case ignores material diffs and merges all meshes into one.
        stage = self._open_stage("license_plate.usd")
        before_meshes, after_meshes, new_meshes = await self._do_merge(stage, self._merge_args)

        # No UV indices on input mesh
        num_st_before, num_st_indices_before = _tot_st_count(before_meshes)
        self.assertEqual(num_st_before, 1088)
        self.assertEqual(num_st_indices_before, 0)

        # Verify UVs are transferred and we have created per-face vertex UVs on output mesh
        # Should also now be indexed, with unique UV values
        num_st_after, num_st_indices_after = _tot_st_count(new_meshes)
        self.assertEqual(num_st_after, 269)
        self.assertEqual(num_st_indices_after, 1088)

    async def test_Merge_per_face(self):
        # Test case is 2 meshes. One has a single material. The other has 3 materials.
        # Expect merged mesh to have 4 per-face materials.
        stage = self._open_stage("twoPlanesOneHasPerFaceMaterial.usda")
        _merge_args = self._merge_args
        _merge_args["considerMaterials"] = False
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        new_prim = new_meshes.pop()
        self.assertEqual(len(new_prim.GetChildren()), 4)
        self.assertTrue(child.IsA(UsdGeom.Subset) for child in new_prim.GetChildren())

    async def test_Merge_per_face_same_material(self):
        # Test case is 2 meshes with the same material. Merge per face should create a
        # material binding on the mesh rather than a GeomSubset.
        stage = self._open_stage("twoBluePlanes.usd")
        _merge_args = self._merge_args
        _merge_args["considerMaterials"] = False
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        new_prim = new_meshes.pop()
        # No GeomSubset created so should be no children
        self.assertFalse(new_prim.GetChildren())

    async def test_Merge_per_face_missing_material(self):
        # Test case is 2 meshes, one has a single material, one has no
        # material. Even though there is only one material, merge per
        # face should create a GeomSubset for this case so faces
        # without materials are preserved.
        stage = self._open_stage("twoPlanesOneWithoutMaterial.usda")
        _merge_args = self._merge_args
        _merge_args["considerMaterials"] = False
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        new_prim = new_meshes.pop()
        # Expect to see GeomSubset children
        self.assertEqual(len(new_prim.GetChildren()), 1)
        self.assertTrue(child.IsA(UsdGeom.Subset) for child in new_prim.GetChildren())

    async def test_Merge_preserve_st_indices_size(self):
        # Test case is a single mesh with UVs and indices (kit camera)
        # Try to merge. Resulting UV and UV indices size should be unchanged.
        stage = self._open_stage("kit_not_camera.usd")
        before_meshes, _, new_meshes = await self._do_merge(stage, self._merge_args)
        self.assertEqual(len(new_meshes), 1)

        # Verify we have expected per-face vertex UVs on output mesh
        before_num_st, before_num_st_indices = _tot_st_count(before_meshes)
        after_num_st, after_num_st_indices = _tot_st_count(new_meshes)
        self.assertEqual(before_num_st, after_num_st)
        self.assertEqual(before_num_st_indices, after_num_st_indices)

    async def test_Merge_check_camera_ignored(self):
        # Test case is a single mesh belonging to a camera.
        # Try to merge. Since cameras are not merged, should be no new outputs.
        stage = self._open_stage("kit_camera.usd")
        _, _, new_meshes = await self._do_merge(stage, self._merge_args)
        self.assertEqual(len(new_meshes), 0)

    async def test_MergeLevel_invalid_xform_name(self):
        # Test case is a scene with two levels of grouping, then meshes.
        # Give invalid toplevel transform name. Should return no results.
        stage = self._open_stage("fourMeshesAtLevelOne.usd")
        _merge_args = self._merge_args
        _merge_args["meshPrimPaths"] = ["/InvalidName"]
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_IGNORE
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 0)

    async def test_MergeLevel_group_at_level_one(self):
        # Test case is a scene with one level of grouping, at leaf four meshes.
        # Group at level 1 should merge four meshes into one, so one output mesh.
        stage = self._open_stage("fourMeshesAtLevelOne.usd")
        _merge_args = self._merge_at_leaf_args
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 1)

    async def test_MergeLevel_group_at_leaf(self):
        # Test case is a scene with two levels of grouping, then meshes.
        # Group at level 2 should merge each pair into a single mesh, so 2 output meshes
        # Merge at leaf automatically deletes original meshes, so expect 2 total at end
        stage = self._open_stage("twoMeshPairsLevelTwo.usd")
        _merge_args = self._merge_at_leaf_args
        _, after_meshes, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 2)
        # Originals deleted so new count == after count, ie no originals are left
        self.assertEqual(len(after_meshes), 2)

    async def test_MergeAtLeafReference(self):
        # Test case is a file with a single mesh that is referenced.
        # Test that the mesh is found and merged.
        stage = self._open_stage("instancedMesh.usd")
        _merge_args = self._merge_args
        _merge_args["meshPrimPaths"] = ["/Root"]
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_IGNORE
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 1)
        new_prim = new_meshes.pop()
        bound_material, _ = UsdShade.MaterialBindingAPI(new_prim).ComputeBoundMaterial()
        self.assertTrue(bound_material.GetPrim().IsValid())

    async def test_MergeAtLeaf(self):
        # Test case is a scene with a grouped hierarchy of meshes.
        # Test that the meshes under a transform are merged.
        stage = self._open_stage("groupingScene.usd")
        _merge_args = self._merge_at_leaf_args
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 9)  # orig not preserved...

    async def test_MergeReferenceNoMaterials(self):
        # Test case is a file with a single mesh that is referenced.
        # Mesh has no materials. Verify case is handled correctly.
        stage = self._open_stage("meshNoMaterial.usd")
        _merge_args = self._merge_args
        _merge_args["meshPrimPaths"] = ["/World"]
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_IGNORE
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 1)
        new_prim = new_meshes.pop()
        bound_material, _ = UsdShade.MaterialBindingAPI(new_prim).ComputeBoundMaterial()
        self.assertFalse(bound_material.GetPrim().IsValid())

    async def test_MergeReferenceMaterials(self):
        # Test case is a file with a single mesh that is referenced.
        # Test that the material on the mesh is preserved after merge.
        # Using regular merge not merge by level.
        stage = self._open_stage("referencedMesh.usd")
        _merge_args = self._merge_args
        _merge_args["meshPrimPaths"] = ["/Root"]
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_IGNORE
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 1)
        new_prim = new_meshes.pop()
        bound_material, _ = UsdShade.MaterialBindingAPI(new_prim).ComputeBoundMaterial()
        self.assertTrue(bound_material.GetPrim().IsValid())

    async def test_MergeReferenceMaterials_noMeshPrimPaths(self):
        # Test case is same as test_MergeReferenceMaterials except
        # test full DAG including refs is traversed if no meshPrimPaths is defined.
        # Previously references were not traversed if there was no meshPrimPaths.
        stage = self._open_stage("referencedMesh.usd")
        _merge_args = self._merge_args
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_IGNORE
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 1)
        new_prim = new_meshes.pop()
        bound_material, _ = UsdShade.MaterialBindingAPI(new_prim).ComputeBoundMaterial()
        self.assertTrue(bound_material.GetPrim().IsValid())

    async def test_Merge_MaterialNameCollisions(self):
        # Test case is a file with two references meshes. Each has a referenced material.
        # The two referenced materials have the same name.
        # Need to verify they are renamed to avoid collisions.
        stage = self._open_stage("materialNameCollision.usd")
        _merge_args = self._merge_args
        _merge_args["meshPrimPaths"] = ["/Root"]
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_DELETE
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        self.assertEqual(len(new_meshes), 1)
        new_prim = new_meshes.pop()
        mesh = UsdGeom.Mesh(new_prim)
        children = new_prim.GetChildren()
        self.assertEqual(len(children), 2)
        self.assertTrue(child.IsA(UsdGeom.Subset) for child in new_prim.GetChildren())
        for child in children:
            subsets = UsdGeom.Subset.GetGeomSubsets(mesh, elementType=UsdGeom.Tokens.face, familyName="materialBind")
            self.assertEqual(len(subsets), 2)

    async def test_DeduplicateGeometryCycle(self):
        # Test case is a file with three meshes.
        # _deduplicate_geometry generated a cycle 1514->1514
        stage = self._open_stage("cycleSource.usda")
        _args = self._deduplicate_geometry_args
        _, _, new_meshes = await self._do_deduplicate_geometry(stage, _args)
        self.assertEqual(len(new_meshes), 1)

    async def test_DeepDeeduplicateFive(self):
        # Test case is a scene with a 5 meshes, 3 with same verts in local space,
        # and 2 with verts offset in world space.
        # Deduplicate geometry with deep transform should detect all of them.

        # Load scene and deep deduplicate geometry immediately
        stage = self._open_stage("localAndWorldSpaceMeshes.usd")

        print("Stats Before Dedupe:")
        context = _get_context(stage)

        success, _, _ = SceneOptimizerCore.getInstance().executeOperation("printStats", context, {})
        self.assertTrue(success)

        _deep_deduplicate_geometry_args = self._deep_deduplicate_geometry_args
        await self._do_deduplicate_geometry(stage, _deep_deduplicate_geometry_args)

        print("Stats After Dedupe:")
        success, _, _ = SceneOptimizerCore.getInstance().executeOperation("printStats", context, {})
        self.assertTrue(success)

        after_meshes = _get_meshes(stage)
        self.assertEqual(len(after_meshes), 1)

    async def test_JsonParserAllCommands(self):
        # json file has settings for every operation.
        # This primarily tests the jsonReader interface itself (eg arg counts)
        # but also runs every command. Some commands themselves are expected to fail,
        # since no files are being passed in for processing.
        stage = self._open_stage("fourMeshesAtLevelOne.usd")
        filepath = _get_test_data_file_path("all_operations.json")
        status = standalone.execute_commands_from_json(stage, filepath)
        self.assertTrue(status)

    async def test_JsonParseInvalidCommand(self):
        stage = self._open_stage("fourMeshesAtLevelOne.usd")
        json = """[{"foo": "bar"}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertFalse(status)

    async def test_Normals(self):
        # Test case is a file with a scene with two meshes containing primvar:normals.
        # One has normalIndices and is faceVarying, the other does not and is varying.
        stage = self._open_stage("normalsTest.usda")
        _merge_args = self._merge_args
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_DELETE
        numNormalsBefore = _tot_normal_count(_get_meshes(stage))
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        numNormalsAfter = _tot_normal_count(_get_meshes(stage))
        self.assertEqual(len(new_meshes), 1)
        self.assertEqual(numNormalsBefore, numNormalsAfter)
        self.assertEqual(237, numNormalsAfter)
        numNormalsIndicesAfter = _tot_normal_indices_count(_get_meshes(stage))
        self.assertEqual(752, numNormalsIndicesAfter)

    async def test_Colors(self):
        # Test case is a file with a scene with two meshes containing per vertex
        # primvar:displayColor attributes.
        stage = self._open_stage("cpv_two_planes_skewed.usda")
        _merge_args = self._merge_args
        _merge_args["originalGeomOption"] = ORIGINAL_GEOM_DELETE
        numColorsBefore, numColorIndicesBefore = _tot_color_count(_get_meshes(stage))
        _, _, new_meshes = await self._do_merge(stage, _merge_args)
        numColorsAfter, numColorIndicesAfter = _tot_color_count(_get_meshes(stage))
        self.assertEqual(len(new_meshes), 1)
        self.assertEqual(7, numColorsAfter)
        self.assertEqual(numColorIndicesBefore, numColorIndicesAfter)
        self.assertEqual(8, numColorIndicesAfter)
        # Check a specific color to ensure ordering is correct
        new_mesh = new_meshes.pop()
        color = _get_color(new_mesh, 2)
        self.assertEqual(color, (1, 1, 1))

    async def test_pathResolver(self):
        """Test the sdfPathResolver function"""

        stage = self._open_stage("pathResolverTest.usda")

        # Camera prim, this should NEVER be returned, by any query.
        cameraChildPrim = "/World/Camera/Baz"

        # All returnable prims, in the expected reversed order they would be returned
        # This excludes the Camera prim, and any children of the Camera, as well as a
        # root Render prim.
        allPrims = [
            "/World/Bar2",
            "/World/Bar1",
            "/World/Foo2/FooChild2",
            "/World/Foo2",
            "/World/Foo1/FooChild1",
            "/World/Foo1",
            "/World",
        ]

        opts = _get_context(stage)

        # Test empty list results in all paths
        result = commands.IFACE.path_resolver(opts, list(), False)
        self.assertNotIn(cameraChildPrim, result)
        self.assertEqual(result, allPrims)

        # Test explicitly asking for camera child doesn't find it
        result = commands.IFACE.path_resolver(opts, [cameraChildPrim], False)
        self.assertEqual(len(result), 0)

        # Test getting one prim
        result = commands.IFACE.path_resolver(opts, ["/World"], False)
        self.assertEqual(len(result), 1)
        self.assertIn("/World", result)

        # Test getting the same prim recursively
        result = commands.IFACE.path_resolver(opts, ["/World//"], True)
        self.assertEqual(result, allPrims)

        # Test basic regex
        fooPrims = [prim for prim in allPrims if "Foo" in prim]
        result = commands.IFACE.path_resolver(opts, ["//Foo*"], False)
        self.assertEqual(result, fooPrims)

        # Test an invalid expression (mainly testing it doesn't crash!)
        result = commands.IFACE.path_resolver(opts, ["+"], False)
        self.assertEqual(len(result), 0)

        # Test duplicates are correctly not returned when explicitly requesting
        result = commands.IFACE.path_resolver(opts, ["/World/Foo2", "/World/Foo2"], False)
        self.assertEqual(result, ["/World/Foo2"])

        # Test alternation
        result = commands.IFACE.path_resolver(opts, ["//Foo[12]"], False)
        self.assertEqual(result, ["/World/Foo2", "/World/Foo1"])

    async def test_computeExtents(self):
        """Test basic computation of extents"""

        # Load the test stage
        stage = self._open_stage("extentsTest.usda")

        extent = list(stage.GetPrimAtPath("/World/CubeExtent").GetAttribute("extent").Get())
        self.assertEqual(extent, [(-50, -50, -50), (50, 50, 50)])

        noExtent = stage.GetPrimAtPath("/World/CubeNoExtent").GetAttribute("extent").Get()
        self.assertIsNone(noExtent)

        largerExtent = list(stage.GetPrimAtPath("/World/CubeLarger").GetAttribute("extent").Get())
        self.assertEqual(largerExtent, [(-100, -100, -100), (100, 100, 100)])

        incorrectExtent = list(stage.GetPrimAtPath("/World/CubeIncorrectExtent").GetAttribute("extent").Get())
        self.assertEqual(incorrectExtent, [(-75, -75, -75), (75, 75, 75)])

        # Load and execute the test commands
        filepath = _get_test_data_file_path("extentsTest.json")
        status = standalone.execute_commands_from_json(stage, filepath)
        self.assertTrue(status)

        # Cube with correct extents stays the same
        extentAfter = list(stage.GetPrimAtPath("/World/CubeExtent").GetAttribute("extent").Get())
        self.assertEqual(extentAfter, [(-50, -50, -50), (50, 50, 50)])
        self.assertEqual(extent, extentAfter)

        # Cube without extents now has some
        noExtentAfter = list(stage.GetPrimAtPath("/World/CubeNoExtent").GetAttribute("extent").Get())
        self.assertEqual(noExtentAfter, [(-50, -50, -50), (50, 50, 50)])
        self.assertNotEqual(noExtent, noExtentAfter)

        # Still the same
        largerExtentAfter = list(stage.GetPrimAtPath("/World/CubeLarger").GetAttribute("extent").Get())
        self.assertEqual(largerExtentAfter, [(-100, -100, -100), (100, 100, 100)])
        self.assertEqual(largerExtent, largerExtentAfter)

        # Was incorrect, now fixed
        incorrectExtentAfter = list(stage.GetPrimAtPath("/World/CubeIncorrectExtent").GetAttribute("extent").Get())
        self.assertEqual(incorrectExtentAfter, [(-50, -50, -50), (50, 50, 50)])
        self.assertNotEqual(incorrectExtent, incorrectExtentAfter)

        # Finally test that the merged result also has an extent authored
        merged = stage.GetPrimAtPath("/World/Merged")
        mergedExtent = list(merged.GetAttribute("extent").Get())
        self.assertEqual(mergedExtent, [(-350, -100, -200), (50, 200, 200)])

        # Re-open stage
        stage = self._open_stage("extentsTest.usda")

        # Test running via command
        args = {"meshPrimPaths": []}
        context = _get_context(stage)
        SceneOptimizerCore.getInstance().executeOperation("computeExtents", context, args)

        noExtentAfter = list(stage.GetPrimAtPath("/World/CubeNoExtent").GetAttribute("extent").Get())
        self.assertEqual(noExtentAfter, [(-50, -50, -50), (50, 50, 50)])
        self.assertNotEqual(noExtent, noExtentAfter)

        # Disabled: currently do not support undo with plugins
        # Test undo restores value
        # cmd.undo()
        # noExtentAfter = stage.GetPrimAtPath("/World/CubeNoExtent").GetAttribute("extent").Get()
        # self.assertIsNone(noExtentAfter)

    async def test_computeExtentTimeSamples(self):
        """Test extent computation with time samples"""

        stage = self._open_stage("extentsTestTimeSamples.usda")

        # Prior to running the opration check for a mesh that has invalid time samples for an
        # existing extent attribute
        deformingIncorrectExtent = stage.GetPrimAtPath("/World/DeformingIncorrectExtent")
        deformingIncorrectTimeSamples = list(deformingIncorrectExtent.GetAttribute("extent").GetTimeSamples())
        self.assertEqual(deformingIncorrectTimeSamples, [99.0, 109.0])

        # Compute Extents
        json = """[{"operation": "computeExtents", "meshPrimPaths": []}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertTrue(status)

        # Validate a simple deforming cube with a few frames gets the correct extent values
        # authored
        deformingNoExtent = stage.GetPrimAtPath("/World/DeformingNoExtent")
        deformingNoExtentTimeSamples = list(deformingNoExtent.GetAttribute("extent").GetTimeSamples())
        self.assertEqual(deformingNoExtentTimeSamples, [1.0, 2.0, 3.0])

        extentAt1 = list(deformingNoExtent.GetAttribute("extent").Get(1.0))
        self.assertEqual(extentAt1, [(-1.0, -1.0, -1.0), (1.0, 1.0, 1.0)])

        extentAt2 = list(deformingNoExtent.GetAttribute("extent").Get(2.0))
        self.assertEqual(extentAt2, [(-2.0, -2.0, -2.0), (2.0, 2.0, 2.0)])

        extentAt3 = list(deformingNoExtent.GetAttribute("extent").Get(3.0))
        self.assertEqual(extentAt3, [(-3.0, -3.0, -3.0), (3.0, 3.0, 3.0)])

        # The incorrect extent samples should have been removed and replaced with the correct time samples
        deformingIncorrectExtent = stage.GetPrimAtPath("/World/DeformingIncorrectExtent")
        deformingIncorrectTimeSamples = list(deformingIncorrectExtent.GetAttribute("extent").GetTimeSamples())
        self.assertEqual(deformingIncorrectTimeSamples, [1.0, 2.0])

        # Check deforming points also get time sampled extents
        deformingPoints = stage.GetPrimAtPath("/World/DeformingPoints")
        deformingPointsTimeSamples = list(deformingPoints.GetAttribute("extent").GetTimeSamples())
        self.assertEqual(deformingPointsTimeSamples, [1.0, 2.0, 3.0, 4.0, 5.0])

        extentAt1 = list(deformingPoints.GetAttribute("extent").Get(1.0))
        self.assertEqual(extentAt1, [(-2.0, -2.0, -2.0), (2.0, 2.0, 2.0)])

        extentAt3 = list(deformingPoints.GetAttribute("extent").Get(3.0))
        self.assertEqual(extentAt3, [(-6.0, -6.0, -6.0), (6.0, 6.0, 6.0)])

        extentAt5 = list(deformingPoints.GetAttribute("extent").Get(5.0))
        self.assertEqual(extentAt5, [(-10.0, -10.0, -10.0), (10.0, 10.0, 10.0)])

    async def test_executeJson(self):
        """Test the JSON parser by JSON string"""

        stage = self._open_stage("extentsTestTimeSamples.usda")

        # Assert that invalid JSON fails (not an array, missing [)
        json = """{"operation": "computeExtents"}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertFalse(status)

        # Assert that another invalid string (missing colon after meshPrimPaths) fails
        json = """[{"operation": "computeExtents", "meshPrimPaths" []}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertFalse(status)

        # Assert that random text fails (not a file path, not a JSON string)
        json = """foo123"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertFalse(status)

        # Assert that valid JSON with a valid command succeeds
        json = """[{"operation": "computeExtents", "meshPrimPaths": []}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertTrue(status)

        # Assert executing via JSON file path
        status = standalone.execute_commands_from_json(stage, _get_test_data_file_path("extentsTest.json"))
        self.assertTrue(status)

    async def test_base_command(self):

        # Load a scene, as teardown will close the stage.
        self._open_stage("threeLambertShaders.usd")

        # Assert the base command class has no info
        info = commands._SceneOptimizerOperation.get_operation_info()
        self.assertEqual(len(info), 0)

    async def test_delete_prims(self):
        """Test deleting prims via the plugin interface"""

        # Load the test stage
        stage = self._open_stage("extentsTest.usda")

        self.assertTrue(stage.GetPrimAtPath("/World/CubeNoExtent"))

        context = _get_context(stage)
        args = {"primPaths": ["/World/CubeNoExtent"]}
        success, _, _ = SceneOptimizerCore.getInstance().executeOperation("deletePrims", context, args)
        self.assertTrue(success)

        self.assertFalse(stage.GetPrimAtPath("/World/CubeNoExtent"))
