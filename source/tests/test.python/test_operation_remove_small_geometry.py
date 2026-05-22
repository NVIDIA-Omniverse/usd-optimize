# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "removeMethod": 1,
    "detectionMethod": 1,
    "threshold": 0.0,
}


class Test_Operation_RemoveSmallGeometry(Test_Operation):

    OPERATION = "removeSmallGeometry"

    async def test_remove_degenerate_meshes(self):
        """Test find and removing degenerate meshes"""
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("smallMeshes.usda")

        # check that the degenerate and small meshes exist
        degenerate = stage.GetPrimAtPath("/World/Degenerate")
        self.assertTrue(degenerate.IsValid())
        small1 = stage.GetPrimAtPath("/World/Small1")
        self.assertTrue(small1.IsValid())
        small2 = stage.GetPrimAtPath("/World/Small2")
        self.assertTrue(small2.IsValid())

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check that the degenerate mesh was removed
        self.assertFalse(degenerate.IsValid())
        # check the small meshes were not removed
        self.assertTrue(small1.IsValid())
        self.assertTrue(small2.IsValid())

    async def test_remove_small_meshes_worldspace(self):
        """Test find and removing small meshes under a threshold world space size"""
        args = DEFAULT_ARGS.copy()
        args["detectionMethod"] = 0
        args["threshold"] = 5.0
        # Open the stage
        stage = self._open_stage("smallMeshes.usda")

        # check that the degenerate and small meshes exist
        degenerate = stage.GetPrimAtPath("/World/Degenerate")
        self.assertTrue(degenerate.IsValid())
        small1 = stage.GetPrimAtPath("/World/Small1")
        self.assertTrue(small1.IsValid())
        small2 = stage.GetPrimAtPath("/World/Small2")
        self.assertTrue(small2.IsValid())

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check that the degenerate and 2 small mesh were removed
        self.assertFalse(degenerate.IsValid())
        self.assertFalse(small1.IsValid())
        self.assertFalse(small2.IsValid())

    async def test_remove_small_meshes_percent(self):
        """Test find and removing small meshes under a threshold percentage"""
        args = DEFAULT_ARGS.copy()
        args["threshold"] = 2.0
        # Open the stage
        stage = self._open_stage("smallMeshes.usda")

        # check that the degenerate and small meshes exist
        degenerate = stage.GetPrimAtPath("/World/Degenerate")
        self.assertTrue(degenerate.IsValid())
        small1 = stage.GetPrimAtPath("/World/Small1")
        self.assertTrue(small1.IsValid())
        small2 = stage.GetPrimAtPath("/World/Small2")
        self.assertTrue(small2.IsValid())

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check that the degenerate and second small mesh was removed
        self.assertFalse(degenerate.IsValid())
        self.assertFalse(small2.IsValid())
        # check the first small mesh was not removed
        self.assertTrue(small1.IsValid())

    async def test_deactivate_degenerate_meshes(self):
        """Test using the deactivate method to remove degenerate meshes"""
        args = DEFAULT_ARGS.copy()
        args["removeMethod"] = 2
        # Open the stage
        stage = self._open_stage("smallMeshes.usda")

        # check that the degenerate mesh is active
        degenerate = stage.GetPrimAtPath("/World/Degenerate")
        self.assertTrue(degenerate.IsActive())

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check that the degenerate mesh is now invactive
        self.assertFalse(degenerate.IsActive())

    async def test_hide_degenerate_meshes(self):
        """ "Test using the hide method to remove degenerate meshes"""
        args = DEFAULT_ARGS.copy()
        args["removeMethod"] = 3
        # Open the stage
        stage = self._open_stage("smallMeshes.usda")

        # check that the degenerate mesh is visible
        degenerate = stage.GetPrimAtPath("/World/Degenerate")
        imageable = UsdGeom.Imageable(degenerate)

        self.assertNotEqual(imageable.ComputeVisibility(), UsdGeom.Tokens.invisible)

        # run command
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # check that the degenerate mesh is now hidden
        self.assertEqual(imageable.ComputeVisibility(), UsdGeom.Tokens.invisible)

    async def test_small_mesh_analysis(self):
        """Test running analysis mode to find small geometry"""
        args = DEFAULT_ARGS.copy()
        args["threshold"] = 5.0
        # Open the stage
        stage = self._open_stage("smallMeshes.usda")
        context = _get_context(stage, analysis=True)

        # run command
        success, result = self._execute_command(args, context)
        self.assertTrue(success)

        # Assert analysis exists
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # check the small geometry results
        self.assertTrue("smallGeometry" in analysis)
        small_geometry = analysis["smallGeometry"]
        self.assertTrue(len(small_geometry) == 3)
        self.assertTrue("/World/Degenerate" in small_geometry)
        self.assertTrue("/World/Small1" in small_geometry)
        self.assertTrue("/World/Small2" in small_geometry)
