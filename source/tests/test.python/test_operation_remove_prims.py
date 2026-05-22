# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "removeInvisible": True,
    "invisibleRemoveMethod": 2,
    "removeOrphanedOvers": True,
    "orphanedOverRemoveMethod": 1,
}


class Test_Operation_RemovePrims(Test_Operation):

    OPERATION = "removePrims"

    async def test_default_remove(self):
        """Test running removePrims with default args"""
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("removePrims.usda")

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check some of the deleted prims
        self.assertFalse(stage.GetPrimAtPath("/World/Scope").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/doubleReference").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/NonConcrete7").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/NonConcrete8").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/Sphere2").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/NonconcretePayload").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/NonconcreteSpecializes").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/NonconcreteVariantGroup").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/NonConcreteNotConnected").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/Looks/material2").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/Looks/material3").IsValid())

        # check some of the prims that should be deactivated
        prim = stage.GetPrimAtPath("/World/Torus")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Xform/Cube")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Xform_02")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        # activate the xform so we can also check the cube under it
        prim.SetActive(True)
        prim = stage.GetPrimAtPath("/World/Xform_02/Cube")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())

        # check prims that should be untouched
        prim = stage.GetPrimAtPath("/World/Sphere")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonConcrete1")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonConcrete3")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/ConcretePayload")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/ConcreteSpecializes")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/ConeClass")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonConcreteInherit")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/ConcreteInherit")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/ConcreteVariantGroup")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Looks/material1")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Looks/material4")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())

    async def test_delete_invisible(self):
        """Test only deleting invisible prims"""
        args = DEFAULT_ARGS.copy()
        args["invisibleRemoveMethod"] = 1
        args["removeOrphanedOvers"] = False
        # Open the stage
        stage = self._open_stage("removePrims.usda")

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check some of the deleted prims
        self.assertFalse(stage.GetPrimAtPath("/World/Torus").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/Xform/Cube").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/Xform_02").IsValid())

        # check prims that should be untouched
        prim = stage.GetPrimAtPath("/World/Sphere")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Scope")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonConcrete7")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())

    async def test_deactivate_orphaned_overs(self):
        """Test only deactivating orphaned overs"""
        args = DEFAULT_ARGS.copy()
        args["removeInvisible"] = False
        args["orphanedOverRemoveMethod"] = 2
        # Open the stage
        stage = self._open_stage("removePrims.usda")

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check some of the prims that should be deactivated
        prim = stage.GetPrimAtPath("/World/Scope")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/doubleReference")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonConcrete7")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonConcrete8")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Sphere2")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonconcretePayload")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonconcreteSpecializes")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonconcreteVariantGroup")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())

        # check prims that should be untouched
        prim = stage.GetPrimAtPath("/World/Torus")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Xform_02")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Xform_02")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Sphere")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())

    async def test_hiding_orphaned_overs(self):
        """Test hiding orphaned overs"""
        args = DEFAULT_ARGS.copy()
        args["removeInvisible"] = False
        args["orphanedOverRemoveMethod"] = 3
        # Open the stage
        stage = self._open_stage("removePrims.usda")

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check some of the prims that should be hidden
        prim = stage.GetPrimAtPath("/World/Scope")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        self.assertEqual(UsdGeom.Imageable(prim).ComputeVisibility(), UsdGeom.Tokens.invisible)
        prim = stage.GetPrimAtPath("/World/doubleReference")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        self.assertEqual(UsdGeom.Imageable(prim).ComputeVisibility(), UsdGeom.Tokens.invisible)
        prim = stage.GetPrimAtPath("/World/NonconcretePayload")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        self.assertEqual(UsdGeom.Imageable(prim).ComputeVisibility(), UsdGeom.Tokens.invisible)

    async def test_set_attribute_orphaned_overs(self):
        """Test setting the hidden attribute on orphaned overs"""
        args = DEFAULT_ARGS.copy()
        args["removeInvisible"] = False
        args["orphanedOverRemoveMethod"] = 4
        # Open the stage
        stage = self._open_stage("removePrims.usda")

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check some of the prims that should have the hidden attribute set on them
        prim = stage.GetPrimAtPath("/World/Scope")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        attr = prim.GetAttribute("hidden")
        self.assertTrue(attr)
        self.assertTrue(attr.Get())
        prim = stage.GetPrimAtPath("/World/doubleReference")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        attr = prim.GetAttribute("hidden")
        self.assertTrue(attr)
        self.assertTrue(attr.Get())

    async def test_explicit_mode(self):
        """Test running removePrims in explicit mode"""
        args = DEFAULT_ARGS.copy()
        args["explicitMode"] = True
        args["explicitInvisiblePaths"] = ["/World/Sphere", "/World/NonConcrete1"]
        args["explicitOrphanedPaths"] = ["/World/ConcretePayload", "/World/ConeClass"]
        # Open the stage
        stage = self._open_stage("removePrims.usda")

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check some of the deleted prims
        self.assertFalse(stage.GetPrimAtPath("/World/ConcretePayload").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/ConeClass").IsValid())

        # check some of the prims that should be deactivated
        prim = stage.GetPrimAtPath("/World/Sphere")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/NonConcrete1")
        self.assertTrue(prim.IsValid())
        self.assertFalse(prim.IsActive())

        # check prims that should be untouched
        prim = stage.GetPrimAtPath("/World/Scope")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())
        prim = stage.GetPrimAtPath("/World/Torus")
        self.assertTrue(prim.IsValid())
        self.assertTrue(prim.IsActive())

    async def test_remove_prims_analysis(self):
        """Test analysis mode"""
        stage = self._open_stage("removePrims.usda")
        context = _get_context(stage, analysis=True)
        success, result = self._execute_command(DEFAULT_ARGS, context)

        # Assert analysis exists
        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # check the invisiblePrims results
        self.assertTrue("invisiblePrims" in analysis)
        invisiblePrims = analysis["invisiblePrims"]
        # check for some expected invisible prims
        self.assertTrue("/World/Torus" in invisiblePrims)
        self.assertTrue("/World/Xform_02" in invisiblePrims)
        self.assertTrue("/World/Xform_02/Cube" in invisiblePrims)

        # check the orphanedOvers results
        self.assertTrue("orphanedOvers" in analysis)
        orphanedOvers = analysis["orphanedOvers"]
        # check for some expected orphaned overs
        self.assertTrue("/World/NonconcreteSpecializes" in orphanedOvers)
        self.assertTrue("/World/NonconcreteVariantGroup" in orphanedOvers)
        self.assertTrue("/World/Looks/material2" in orphanedOvers)
