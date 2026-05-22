# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Sdf, Usd, UsdGeom

from .test_utils import Test_Operation, _get_context


class Test_Operation_Delete_Prims(Test_Operation):

    OPERATION = "deletePrims"

    def _defined_in_two_layers(self, edit_target):
        # define paths
        defaultPath: Sdf.Path = "/World"
        deletePath: Sdf.Path = "/World/Target"

        # open in memory sub layer
        subLayer1: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        subStage1: Usd.Stage = Usd.Stage.Open(subLayer1)
        subStage1.DefinePrim(defaultPath)
        subStage1.DefinePrim(deletePath)

        # open in memory sub layer
        subLayer2: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        subStage2: Usd.Stage = Usd.Stage.Open(subLayer2)
        subStage2.DefinePrim(defaultPath)
        subStage2.DefinePrim(deletePath)

        # open in memory root layer
        layer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        stage: Usd.Stage = Usd.Stage.Open(layer)
        stage.DefinePrim(defaultPath)
        layer.subLayerPaths.append(subLayer1.identifier)
        layer.subLayerPaths.append(subLayer2.identifier)

        # explicitly set edit target to specified layer
        if edit_target == 0:
            target: Usd.EditTarget = Usd.EditTarget(layer)
            stage.SetEditTarget(target)
        elif edit_target == 1:
            target: Usd.EditTarget = Usd.EditTarget(subLayer1)
            stage.SetEditTarget(target)
        elif edit_target == 2:
            target: Usd.EditTarget = Usd.EditTarget(subLayer2)
            stage.SetEditTarget(target)

        # Use the operation to delete the prim in the in active stage
        args: dict = {"primPaths": [deletePath]}
        context: ExecutionContext = _get_context(stage)
        self._execute_command(args, context)

        # set edit target back
        restore: Usd.EditTarget = Usd.EditTarget(layer)
        stage.SetEditTarget(restore)

        # verify results based on edit target
        if edit_target == 0:
            # verify root layer is deactivated and an over
            primSpec: Sdf.PrimSpec = layer.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertFalse(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierOver)

            # verify sub layer 1 is active and a def
            primSpec = subLayer1.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierDef)

            # verify sub layer 2 is active and a def
            primSpec = subLayer2.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierDef)

        if edit_target == 1:
            # verify spec is invalid at root
            primSpec: Sdf.PrimSpec = layer.GetPrimAtPath(deletePath)
            self.assertFalse(primSpec)

            # verify spec was removed in sub layer 1
            primSpec = subLayer1.GetPrimAtPath(deletePath)
            self.assertFalse(primSpec)

            # verify sub layer 2 is active and a def
            primSpec = subLayer2.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierDef)

        if edit_target == 2:
            # verify spec is invalid at root
            primSpec: Sdf.PrimSpec = layer.GetPrimAtPath(deletePath)
            self.assertFalse(primSpec)

            # verify sub layer 1 is active and def
            primSpec = subLayer1.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierDef)

            # verify spec was removed in sub layer 2
            primSpec = subLayer2.GetPrimAtPath(deletePath)
            self.assertFalse(primSpec)

    def _three_layer_stage_test(self, edit_target):
        # define paths
        defaultPath: Sdf.Path = "/World"
        deletePath: Sdf.Path = "/World/Sphere"

        # open in memory sub sub layer
        weakestLayer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        weakestStage: Usd.Stage = Usd.Stage.Open(weakestLayer)
        weakestStage.DefinePrim(defaultPath)
        sphere: UsdGeom.Sphere = UsdGeom.Sphere.Define(weakestStage, deletePath)

        # open in memory layer for sub layer
        weakLayer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        weakStage: Usd.Stage = Usd.Stage.Open(weakLayer)
        weakStage.DefinePrim(defaultPath)

        # insert sub sub layer
        weakLayer.subLayerPaths.append(weakestLayer.identifier)

        # author over on sphere in middle layer
        spherePrim = weakStage.GetPrimAtPath(deletePath)
        sphereGeom: UsdGeom.Sphere = UsdGeom.Sphere(spherePrim)
        sphereGeom.GetRadiusAttr().Set(20)

        # open in memory layer for root
        strongLayer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        strongStage: Usd.Stage = Usd.Stage.Open(strongLayer)
        strongStage.DefinePrim(defaultPath)

        # insert sub layer
        strongLayer.subLayerPaths.append(weakLayer.identifier)

        # explicitly set edit target to the specified layer
        if edit_target == 0:
            target: Usd.EditTarget = Usd.EditTarget(strongLayer)
            strongStage.SetEditTarget(target)
        elif edit_target == 1:
            target: Usd.EditTarget = Usd.EditTarget(weakLayer)
            strongStage.SetEditTarget(target)
        elif edit_target == 2:
            target: Usd.EditTarget = Usd.EditTarget(weakestLayer)
            strongStage.SetEditTarget(target)

        # Use the operation to delete the prim in the in active stage
        args: dict = {"primPaths": [deletePath]}
        context: ExecutionContext = _get_context(strongStage, verbose=False)
        self._execute_command(args, context)

        # set edit target back
        restore: Usd.EditTarget = Usd.EditTarget(strongLayer)
        strongStage.SetEditTarget(restore)

        # verify results based on edit target
        if edit_target == 0:
            # verify strong layer is deactivated and an over
            primSpec: Sdf.PrimSpec = strongLayer.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertFalse(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierOver)

            # verify middle layer is active and an over
            primSpec = weakLayer.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierOver)

            # verify the weakest layer is active and a def
            primSpec = weakestLayer.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierDef)

        elif edit_target == 1:
            # verify strong layer has no spec for sphere
            primSpec: Sdf.PrimSpec = strongLayer.GetPrimAtPath(deletePath)
            self.assertFalse(primSpec)

            # verify middle layer is deactivated and an over
            primSpec = weakLayer.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertFalse(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierOver)

            # verify the weakest layer is active and a def
            primSpec = weakestLayer.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierDef)

        elif edit_target == 2:
            # verify strong layer has no spec for sphere
            primSpec: Sdf.PrimSpec = strongLayer.GetPrimAtPath(deletePath)
            self.assertFalse(primSpec)

            # verify middle layer is active and an over
            primSpec = weakLayer.GetPrimAtPath(deletePath)
            self.assertTrue(primSpec)
            self.assertTrue(primSpec.active)
            self.assertEqual(primSpec.specifier, Sdf.SpecifierOver)

            # verify the weakest layer prim spec is deleted
            primSpec = weakestLayer.GetPrimAtPath(deletePath)
            self.assertFalse(primSpec)

    async def test_default_prim(self):

        # Create a test stage
        layer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        stage: Usd.Stage = Usd.Stage.Open(layer)

        # Define the default prim
        path: Sdf.Path = "/Test"
        prim: Usd.Prim = stage.DefinePrim(path)
        stage.SetDefaultPrim(prim)

        # Use the operation to delete the Prim
        args: dict = {"primPaths": [path]}
        context: ExecutionContext = _get_context(stage, verbose=False)
        self._execute_command(args, context)

        # The prim should no longer exist, the default prim should no longer be set and the layer should be empty
        self.assertFalse(stage.GetPrimAtPath(path))
        self.assertFalse(layer.HasDefaultPrim())
        self.assertTrue(layer.empty)

    async def test_defined_in_layer(self):

        # define paths
        defaultPath: Sdf.Path = "/World"
        deletePath: Sdf.Path = "/World/Target"

        # open in memory layer
        layer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        stage: Usd.Stage = Usd.Stage.Open(layer)
        stage.DefinePrim(defaultPath)
        stage.DefinePrim(deletePath)

        # explicitly set edit target to layer
        target: Usd.EditTarget = Usd.EditTarget(layer)
        stage.SetEditTarget(target)

        # Use the operation to delete the prim in the stage
        args: dict = {"primPaths": [deletePath]}
        context: ExecutionContext = _get_context(stage, verbose=False)
        self._execute_command(args, context)

        # the prim should be invalid and the root prim should have no children
        deleted: Usd.Prim = stage.GetPrimAtPath(deletePath)
        self.assertFalse(deleted)
        root: UsdPrim = stage.GetPrimAtPath(defaultPath)
        children = root.GetAllChildren()
        self.assertEqual(len(children), 0)

    async def test_defined_in_weak_layer(self):

        # define paths
        defaultPath: Sdf.Path = "/World"
        deletePath: Sdf.Path = "/World/Target"

        # open in memory sub layer
        subLayer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        subStage: Usd.Stage = Usd.Stage.Open(subLayer)
        subStage.DefinePrim(defaultPath)
        subStage.DefinePrim(deletePath)

        # open in memory layer for active stage
        activeLayer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        activeStage: Usd.Stage = Usd.Stage.Open(activeLayer)

        activeDefault: UsdPrim = activeStage.DefinePrim(defaultPath)
        activeStage.SetDefaultPrim(activeDefault)

        # insert sublayer
        activeLayer.subLayerPaths.append(subLayer.identifier)

        # explicitly set edit target to active layer
        target: Usd.EditTarget = Usd.EditTarget(activeLayer)
        activeStage.SetEditTarget(target)

        # Use the operation to delete the prim in the in active stage
        args: dict = {"primPaths": [deletePath]}
        context: ExecutionContext = _get_context(activeStage, verbose=False)
        self._execute_command(args, context)

        # the prim should be valid and deactivated in the in active stage
        deactivated: Usd.Prim = activeStage.GetPrimAtPath(deletePath)
        self.assertTrue(deactivated)
        self.assertFalse(deactivated.IsActive())

        # prim spec specifier in the active layer (edit target) should be an over
        primSpec: Sdf.PrimSpec = activeLayer.GetPrimAtPath(deletePath)
        self.assertEqual(primSpec.specifier, Sdf.SpecifierOver)

        # verify sub layer has not been changed
        active: Sdf.PrimSpec = subLayer.GetPrimAtPath(deletePath)
        self.assertTrue(active)
        self.assertTrue(active.active)
        self.assertEqual(active.specifier, Sdf.SpecifierDef)

    async def test_defined_in_strong_layer(self):

        # define paths
        defaultPath: Sdf.Path = "/World"
        deletePath: Sdf.Path = "/World/Target"

        # open in memory sub layer
        subLayer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        subStage: Usd.Stage = Usd.Stage.Open(subLayer)
        subDefault: Usd.Prim = subStage.DefinePrim(defaultPath)
        subStage.SetDefaultPrim(subDefault)

        # open in memory layer for active stage
        activeLayer: Sdf.Layer = Sdf.Layer.CreateAnonymous()
        activeStage: Usd.Stage = Usd.Stage.Open(activeLayer)

        activeDefault: UsdPrim = activeStage.DefinePrim(defaultPath)
        activeStage.SetDefaultPrim(activeDefault)
        activeStage.DefinePrim(deletePath)

        # insert sublayer
        activeLayer.subLayerPaths.append(subLayer.identifier)

        # explicitly set edit target to the sub layer
        target: Usd.EditTarget = Usd.EditTarget(subLayer)
        activeStage.SetEditTarget(target)

        # Use the operation to delete the prim in the in active stage
        args: dict = {"primPaths": [deletePath]}
        context: ExecutionContext = _get_context(activeStage, verbose=False)
        self._execute_command(args, context)

        # set edit target back
        target = Usd.EditTarget(activeLayer)
        activeStage.SetEditTarget(target)

        # prim spec in the strong layer should be active and specifier should still be a def
        primSpec: Sdf.PrimSpec = activeLayer.GetPrimAtPath(deletePath)
        self.assertTrue(primSpec)
        self.assertTrue(primSpec.active)
        self.assertEqual(primSpec.specifier, Sdf.SpecifierDef)

        # verify sub layer (the edit target) prim spec is deactivated and set to over
        deactivated: Sdf.PrimSpec = subLayer.GetPrimAtPath(deletePath)
        self.assertTrue(deactivated)
        self.assertFalse(deactivated.active)
        self.assertEqual(deactivated.specifier, Sdf.SpecifierOver)

        # over in weak layer results in a deactivated prim after composition
        composedPrim: Usd.Prim = activeStage.GetPrimAtPath(deletePath)
        self.assertTrue(composedPrim)
        self.assertFalse(composedPrim.IsActive())

    async def test_over_in_sublayer(self):

        # test with strong layer as edit target
        self._three_layer_stage_test(0)

        # test with weaker layer as edit target
        self._three_layer_stage_test(1)

        # test with weakest layer as edit target
        self._three_layer_stage_test(2)

    async def test_defined_in_two_layers(self):

        # test edit target at root layer
        self._defined_in_two_layers(0)

        # test edit target at sub layer containing definition
        self._defined_in_two_layers(1)

        # test edit target at sub layer containing definition
        self._defined_in_two_layers(2)
