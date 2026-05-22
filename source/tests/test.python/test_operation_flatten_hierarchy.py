# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.scripts import standalone
from pxr import Gf, UsdGeom, UsdShade

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
}


def _get_xforms(stage, name):
    """Return the paths (as strings) of any xforms in the stage
    that contain the specified name.
    """
    xforms = list()

    for prim in stage.Traverse():
        if prim.GetTypeName() == "Xform":
            if name in prim.GetName():
                xforms.append(str(prim.GetPrimPath()))
    return xforms


def _get_expected_path(path):
    """Strip any "/*Remove*/ parts from a path, to get the expected path
    of a prim after processing"""

    parts = path.split("/")
    expected = ""
    for part in parts:
        if part:
            if "Remove" not in part:
                expected += "/" + part
    return expected


def _count_descendants(prim):
    """Count a prim and its descendants"""

    count = 1
    for child in prim.GetAllChildren():
        count += _count_descendants(child)

    return count


def _is_bound_material(stage, prim_path, material_path):
    """Returns true if the prim exists and is bound to a material with the given path"""
    prim = stage.GetPrimAtPath(prim_path)

    if prim:
        materialBindingAPI = UsdShade.MaterialBindingAPI(prim)
        material, _ = materialBindingAPI.ComputeBoundMaterial()

        if material and material.GetPath() == material_path:
            return True

    return False


class Test_Operation_Flatten_Hierarchy(Test_Operation):

    OPERATION = "flattenHierarchy"

    async def test_flatten(self):
        """Test flattening a stage containing a bunch of different scenarios"""

        args = DEFAULT_ARGS.copy()

        stage = self._open_stage("flattenHierarchy.usda")

        # Capture some info before doing anything
        xforms_keep = _get_xforms(stage, "Keep")
        xforms_remove = _get_xforms(stage, "Remove")

        cache = UsdGeom.XformCache()
        cube1_path = "/World/Keep_1/Remove_1/Keep_1_1/Remove_1_1/Keep_1_2/Remove_1_2/Cube"
        cube1_before = cache.GetLocalToWorldTransform(stage.GetPrimAtPath(cube1_path))

        # Assert this prim does NOT reset the xform stack (it's child does)
        no_reset_prim = stage.GetPrimAtPath("/World/Keep_1/Remove_1/Keep_1_1")
        self.assertFalse(UsdGeom.Xformable(no_reset_prim).GetResetXformStack())
        reset_prim = stage.GetPrimAtPath("/World/Keep_1/Remove_1/Keep_1_1/Remove_1_1")
        self.assertTrue(UsdGeom.Xformable(reset_prim).GetResetXformStack())

        self.assertGreater(len(xforms_keep), 0)
        self.assertGreater(len(xforms_remove), 0)

        self._execute_command(args)

        xforms_keep_after = _get_xforms(stage, "Keep")
        xforms_remove_after = _get_xforms(stage, "Remove")

        # Get the keep_after ones. For each original Keep,
        # work out the expected name by stripping any "Remove" parts
        # then verify those xforms exist
        #
        # This is done to make it easier to debug specifically which
        # path doesn't exist if something goes wrong
        for xform in xforms_keep:
            expected = _get_expected_path(xform)
            self.assertIn(expected, xforms_keep_after)

        # All prims with "Keep" in their name should still be there
        self.assertEqual(len(xforms_keep_after), len(xforms_keep))

        # Any prims with "Remove" in their name should be gone
        self.assertEqual(len(xforms_remove_after), 0)

        # Assert world position of a prim that had xforms removed from its hierarchy
        # NB: Reset cache here, to ensure we are not using stale data
        cache.Clear()
        cube1_after = cache.GetLocalToWorldTransform(stage.GetPrimAtPath(_get_expected_path(cube1_path)))
        self.assertEqual(cube1_before, cube1_after)

        # Assert material is still bound
        material_prim = stage.GetPrimAtPath("/World/Bound_Keep/Bound_Keep_1/Cube")
        bindingAPI = UsdShade.MaterialBindingAPI(material_prim)
        bound = bindingAPI.ComputeBoundMaterial()
        material = bound[0]

        # Assert the new look path
        self.assertEqual(material.GetPath(), "/World/Looks/Looks_Keep_1/Look_1")

        # Assert visibility of a prim that should remain hidden
        # Visibility was hidden in one of the descendant prims that was removed - it should
        # have been set on this prim.
        hidden_prim = stage.GetPrimAtPath("/Keep_Vis")
        imageable = UsdGeom.Imageable(hidden_prim)
        self.assertEqual(imageable.ComputeVisibility(), UsdGeom.Tokens.invisible)

        # Assert an external reference is retained, and has children
        ext_ref_prim = stage.GetPrimAtPath("/Keep_Ref")
        self.assertTrue(ext_ref_prim)
        children = ext_ref_prim.GetAllChildren()
        self.assertEqual(len(children), 1)

        # Assert resetXformStack is respected
        reset_prim = stage.GetPrimAtPath("/World/Keep_1/Keep_1_1")
        self.assertTrue(UsdGeom.Xformable(reset_prim).GetResetXformStack())

        # Assert a prim that is the target of a (non-material-bind) relationship is retained
        prim = stage.GetPrimAtPath("/Keep_Rel/Keep_Rel_1/Keep_Rel_2/Keep_Rel_3")
        self.assertTrue(prim)

    async def test_flatten_path(self):
        """Test flattening a subset of the stage"""

        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Keep_1"]

        stage = self._open_stage("flattenHierarchy.usda")

        # Capture some info before doing anything
        xforms_keep = _get_xforms(stage, "Keep")
        xforms_remove = _get_xforms(stage, "Remove")

        self.assertGreater(len(xforms_keep), 0)
        self.assertGreater(len(xforms_remove), 0)

        # Build an execution context with the custom stage, enable verbose
        context = _get_context(stage)

        self._execute_command(args, context=context)

        # Expected number of xforms that will be removed
        removed = 6

        xforms_keep_after = _get_xforms(stage, "Keep")
        xforms_remove_after = _get_xforms(stage, "Remove")

        # Keep is the same regardless
        self.assertEqual(len(xforms_keep), len(xforms_keep_after))

        # After + expected removed should match the original
        self.assertEqual(len(xforms_remove), len(xforms_remove_after) + removed)

    async def test_flattening_nothing(self):
        """Test flattening something that doesn't exist"""

        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Foo"]

        stage = self._open_stage("flattenHierarchy.usda")

        # Capture some info before doing anything
        xforms_keep = _get_xforms(stage, "Keep")
        xforms_remove = _get_xforms(stage, "Remove")

        self.assertGreater(len(xforms_keep), 0)
        self.assertGreater(len(xforms_remove), 0)

        success, result = self._execute_command(args)
        self.assertTrue(result[0])

        xforms_keep_after = _get_xforms(stage, "Keep")
        xforms_remove_after = _get_xforms(stage, "Remove")

        # Nothing happened
        self.assertEqual(len(xforms_keep), len(xforms_keep_after))
        self.assertEqual(len(xforms_remove), len(xforms_remove_after))

    async def test_flattening_inside_external_ref(self):
        """Test trying to flatten within an external reference"""

        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/Keep_Ref/Keep_Ref_1"]

        stage = self._open_stage("flattenHierarchy.usda")

        # Capture some info before doing anything
        xforms_keep = _get_xforms(stage, "Keep")
        xforms_remove = _get_xforms(stage, "Remove")

        self.assertGreater(len(xforms_keep), 0)
        self.assertGreater(len(xforms_remove), 0)

        # This will fail!
        success, result = self._execute_command(args)
        self.assertFalse(result[0])

        xforms_keep_after = _get_xforms(stage, "Keep")
        xforms_remove_after = _get_xforms(stage, "Remove")

        # Nothing happened
        self.assertEqual(len(xforms_keep), len(xforms_keep_after))
        self.assertEqual(len(xforms_remove), len(xforms_remove_after))

    async def test_flattening_internal_ref(self):
        """Test trying to flatten within an external reference"""

        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/InternalRef_Keep"]

        stage = self._open_stage("flattenHierarchy.usda")

        # Capture some info before doing anything
        xforms_keep = _get_xforms(stage, "Keep")
        xforms_remove = _get_xforms(stage, "Remove")

        self.assertGreater(len(xforms_keep), 0)
        self.assertGreater(len(xforms_remove), 0)

        success, result = self._execute_command(args)
        self.assertTrue(result[0])

        xforms_keep_after = _get_xforms(stage, "Keep")
        xforms_remove_after = _get_xforms(stage, "Remove")

        # Nothing happened
        self.assertEqual(len(xforms_keep), len(xforms_keep_after))
        self.assertEqual(len(xforms_remove), len(xforms_remove_after))

    async def test_flattening_identity_only(self):
        """Test flattening only identity xforms"""

        args = DEFAULT_ARGS.copy()

        stage = self._open_stage("flattenHierarchy.usda")
        args["paths"] = ["/World/Keep_Ident"]

        # Starts with 7 prims
        descendants = _count_descendants(stage.GetPrimAtPath("/World/Keep_Ident"))
        self.assertEqual(descendants, 7)

        success, result = self._execute_command(args)
        self.assertTrue(result[0])

        # Everything removed except the top xform and the cube
        descendants = _count_descendants(stage.GetPrimAtPath("/World/Keep_Ident"))
        self.assertEqual(descendants, 2)

        # Open stage again, then run with identity-only
        stage = self._open_stage("flattenHierarchy.usda")
        args["identity"] = True

        success, result = self._execute_command(args)
        self.assertTrue(result[0])

        # Only some of te descendants removed, as some had xform data
        descendants = _count_descendants(stage.GetPrimAtPath("/World/Keep_Ident"))
        self.assertEqual(descendants, 4)

        # These have "Remove" in the name, for the main unit test.
        # But we expect with identity=true, these prims still exist as they have a useful local xform.
        self.assertTrue(stage.GetPrimAtPath("/World/Keep_Ident/Remove_Ident_2"))
        self.assertTrue(stage.GetPrimAtPath("/World/Keep_Ident/Remove_Ident_2/Remove_Ident_5"))

    async def test_flattening_name_clash(self):
        """Test flattening where a redundant xform has the same name as a child
        that should be reparented in its place"""

        args = DEFAULT_ARGS.copy()

        stage = self._open_stage("flattenHierarchy.usda")

        # Assert the original hierarchy
        self.assertTrue(stage.GetPrimAtPath("/Dup"))
        self.assertTrue(stage.GetPrimAtPath("/Dup/Dup_1"))
        self.assertTrue(stage.GetPrimAtPath("/Dup/Dup_1/Dup_1"))
        self.assertTrue(stage.GetPrimAtPath("/Dup/Dup_1/Dup_1/Dup_2"))
        self.assertTrue(stage.GetPrimAtPath("/Dup/Dup_1/Dup_1/Dup_2/Dup_1"))

        success, result = self._execute_command(args)
        self.assertTrue(result[0])

        # Assert new hierarchy
        self.assertTrue(stage.GetPrimAtPath("/Dup"))
        self.assertTrue(stage.GetPrimAtPath("/Dup/Dup_1"))
        self.assertTrue(stage.GetPrimAtPath("/Dup/Dup_1/Dup_1_1"))

        # This was the original name. Since the child took it, the
        # child ended up with a new name, so this prim won't exist
        # any more.
        self.assertFalse(stage.GetPrimAtPath("/Dup/Dup_1/Dup_1"))

    async def test_flattening_existing_xformop(self):
        """Test flattening when there is an existing :flattenHierarchyOp xform op"""

        args = DEFAULT_ARGS.copy()
        stage = self._open_stage("flattenHierarchyOp.usda")

        sphere = stage.GetPrimAtPath("/World/Group/Group_01/Group/Sphere")
        self.assertTrue(sphere)
        torus = stage.GetPrimAtPath("/World/Group/Group_01/Group/Torus")
        self.assertTrue(torus)

        xformCache = UsdGeom.XformCache()
        sphere_world_before = xformCache.GetLocalToWorldTransform(sphere)
        torus_world_before = xformCache.GetLocalToWorldTransform(torus)

        success, result = self._execute_command(args)
        self.assertTrue(result[0])

        # Get the prims from their new paths
        sphere = stage.GetPrimAtPath("/World/Sphere")
        self.assertTrue(sphere)
        torus = stage.GetPrimAtPath("/World/Torus")
        self.assertTrue(torus)

        # Reset xform cache, get "after" values
        xformCache = UsdGeom.XformCache()
        sphere_world_after = xformCache.GetLocalToWorldTransform(sphere)
        torus_world_after = xformCache.GetLocalToWorldTransform(torus)

        # World position should match
        self.assertTrue(Gf.IsClose(sphere_world_before, sphere_world_after, 1e-12))
        self.assertTrue(Gf.IsClose(torus_world_before, torus_world_after, 1e-12))

    async def test_time_varying_meshes(self):
        """Test flatten hierarchy operation on meshes with authored time varying attributes"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

    async def test_flattening_materials(self):
        """Test flattening hierarchies containing materials"""

        stage = self._open_stage("flattenMaterials.usda")

        # Execute command
        context = _get_context(stage)

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # Assert the various expected results.
        # These test that the bound prims AND the materials are in their expected place.

        # Assert material with internal shader network connections
        self.assertTrue(
            _is_bound_material(stage, "/World/Xform/XformReader/Cube", "/World/Xform/XformReader/PrimvarMaterial")
        )

        # Assert that an internal connection between shader/primvar reader has been remapped
        shader = stage.GetPrimAtPath("/World/Xform/XformReader/PrimvarMaterial/PrimvarShader")
        connectable = UsdShade.ConnectableAPI(shader)
        inputs = connectable.GetInputs()
        self.assertEqual(len(inputs), 1)
        connected_paths = inputs[0].GetRawConnectedSourcePaths()
        self.assertEqual(len(connected_paths), 1)
        self.assertEqual(
            connected_paths[0], "/World/Xform/XformReader/PrimvarMaterial/usdprimvarreader1.outputs:result"
        )

        # Assert GeomSubsets are rebound
        self.assertTrue(
            _is_bound_material(stage, "/World/Xform/XformSubsets/Cube/Red", "/World/Xform/XformSubsets/Red")
        )
        self.assertTrue(
            _is_bound_material(stage, "/World/Xform/XformSubsets/Cube/Green", "/World/Xform/XformSubsets/Green")
        )
        self.assertTrue(
            _is_bound_material(stage, "/World/Xform/XformSubsets/Cube/Blue", "/World/Xform/XformSubsets/Blue")
        )

        # Assert basic xform/material are moved
        self.assertTrue(_is_bound_material(stage, "/World/Xform_01/Cube", "/World/Xform_01/Pink"))

        # Assert xforms with direct bindings are not removed (bindings to outside the xform hierarchy)
        self.assertTrue(_is_bound_material(stage, "/World/XformBindings/Xform", "/World/Looks/Material1"))
        self.assertTrue(_is_bound_material(stage, "/World/XformBindings/Xform/Xform", "/World/Looks/Material2"))

        # Assert xforms with direct bindings to materials within the hierarchy are managed
        self.assertTrue(
            _is_bound_material(stage, "/World/XformBindingsInternal/Xform_1", "/World/XformBindingsInternal/Material")
        )
