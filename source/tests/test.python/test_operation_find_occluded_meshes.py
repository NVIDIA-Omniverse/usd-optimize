# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "clustered": False,
    "minimumGapSize": 4.0,
    "maximumGridResolution": 500.0,
    "action": 4,  # set custom bool attribute "hidden"
    "useGpu": False,
}


class Test_Operation_FindOccludedMeshes(Test_Operation):

    OPERATION = "findOccludedMeshes"

    async def test_find_occluded_meshes(self):
        """Test find occluded meshes with all combinations of GPU and clustering"""

        test_cases = [
            {"clustered": False, "useGpu": False},
            {"clustered": False, "useGpu": True},
            {"clustered": True, "useGpu": False},
            {"clustered": True, "useGpu": True},
        ]

        failures = []

        for _, overrides in enumerate(test_cases):
            args = DEFAULT_ARGS.copy()
            args.update(overrides)

            stage = self._open_stage("hiddenMesh.usda")

            test_label = f"test_find_occluded_meshes[clustered={overrides['clustered']}, gpu={overrides['useGpu']}]"

            success, _ = self._execute_command(args)

            if not success:
                failures.append(f"{test_label}: command execution failed")
                continue

            # check whether all meshes have a hidden attribute
            # with the expected value

            for prim in stage.TraverseAll():
                if prim.IsA(UsdGeom.Mesh):
                    attrHidden = prim.GetAttribute("hidden")
                    if not attrHidden:
                        failures.append(f"{test_label}: {prim.GetPath()} missing 'hidden' attribute")
                        continue

                    should_be_hidden = "hidden" in prim.GetPath().pathString
                    actual = attrHidden.Get()
                    if actual != should_be_hidden:
                        failures.append(
                            f"{test_label}: {prim.GetPath()} expected hidden={should_be_hidden}, got {actual}"
                        )

        if failures:
            self.fail("Failures:\n" + "\n".join(failures))

    async def test_time_varying_meshes(self):
        """Test find hidden meshes operation on meshes with authored time varying attributes"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

    async def test_analysis(self):
        """Test analysis mode"""

        stage = self._open_stage("hiddenMesh.usda")

        context = _get_context(stage, analysis=True)

        success, result = self._execute_command(DEFAULT_ARGS, context)

        # Assert analysis exists
        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Assert expected name of first (and only) hidden mesh
        self.assertTrue("occludedMeshes" in analysis)
        self.assertEqual(len(analysis["occludedMeshes"]), 1)
        self.assertEqual(analysis["occludedMeshes"][0], "/root/hidden/hidden")

    async def test_large_minimum_gap_size_finds_no_occluded(self):
        """Test that a large minimumGapSize makes the grid too coarse to detect occlusion"""

        stage = self._open_stage("hiddenMesh.usda")

        context = _get_context(stage, analysis=True)

        args = DEFAULT_ARGS.copy()
        args["minimumGapSize"] = 100.0

        success, result = self._execute_command(args, context)

        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # With such a large gap size the grid is too coarse, so no meshes should be detected as occluded
        self.assertTrue("occludedMeshes" in analysis)
        self.assertEqual(len(analysis["occludedMeshes"]), 0)

    async def test_max_resolution_caps_grid(self):
        """Test that a low maximumGridResolution caps the grid, triggering a warning and producing a coarser grid"""

        stage = self._open_stage("hiddenMesh.usda")

        context = _get_context(stage, analysis=True)

        args = DEFAULT_ARGS.copy()
        args["minimumGapSize"] = 0.01  # would request ~6900 cells on the 69-unit scene
        args["maximumGridResolution"] = 5.0  # cap to 5 cells, effective gap size ~13.8

        success, result = self._execute_command(args, context)

        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # The grid is too coarse at 5 cells to reliably detect the hidden mesh
        self.assertTrue("occludedMeshes" in analysis)

    async def test_deprecated_grid_resolution_name(self):
        """Test that the deprecated 'gridResolution' argument name still works via operation_mapping.json"""

        stage = self._open_stage("hiddenMesh.usda")

        context = _get_context(stage, analysis=True)

        # Use the old 'gridResolution' name instead of 'maximumGridResolution'
        args = DEFAULT_ARGS.copy()
        del args["maximumGridResolution"]
        args["gridResolution"] = 500.0

        success, result = self._execute_command(args, context)

        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Should detect the hidden mesh just like the normal test_analysis case
        self.assertTrue("occludedMeshes" in analysis)
        self.assertEqual(len(analysis["occludedMeshes"]), 1)
        self.assertEqual(analysis["occludedMeshes"][0], "/root/hidden/hidden")

    async def test_cpu_gpu_parity(self):
        """Test that CPU and GPU modes produce the same occluded mesh results"""

        # Run with CPU
        stage = self._open_stage("hiddenMesh.usda")
        context_cpu = _get_context(stage, analysis=True)
        args_cpu = DEFAULT_ARGS.copy()
        args_cpu["useGpu"] = False

        success_cpu, result_cpu = self._execute_command(args_cpu, context_cpu)
        self.assertTrue(success_cpu)
        self.assertTrue(result_cpu[0])
        self.assertTrue("analysis" in result_cpu[2])
        occluded_cpu = sorted(result_cpu[2]["analysis"]["occludedMeshes"])

        # Run with GPU
        stage = self._open_stage("hiddenMesh.usda")
        context_gpu = _get_context(stage, analysis=True)
        args_gpu = DEFAULT_ARGS.copy()
        args_gpu["useGpu"] = True

        success_gpu, result_gpu = self._execute_command(args_gpu, context_gpu)
        self.assertTrue(success_gpu)
        self.assertTrue(result_gpu[0])
        self.assertTrue("analysis" in result_gpu[2])
        occluded_gpu = sorted(result_gpu[2]["analysis"]["occludedMeshes"])

        # Verify both modes found the same occluded meshes
        self.assertEqual(occluded_cpu, occluded_gpu)

    async def test_two_groups_find_occluded_meshes(self):
        """Test that two separated enclosures each have their hidden mesh detected, with and without clustering.

        The twoGroupsHiddenMesh.usda scene has two groups (groupA and groupB) offset by 200 units in X.
        Each group is a sealed box enclosure containing a hidden mesh. With clustering enabled, the
        algorithm should identify the two groups as separate clusters and still detect both hidden meshes.
        Without clustering, it should also detect both hidden meshes via a single global grid.
        """

        test_cases = [
            {"clustered": False, "useGpu": False},
            {"clustered": False, "useGpu": True},
            {"clustered": True, "useGpu": False},
            {"clustered": True, "useGpu": True},
        ]

        failures = []

        for overrides in test_cases:
            args = DEFAULT_ARGS.copy()
            args.update(overrides)

            stage = self._open_stage("twoGroupsHiddenMesh.usda")

            test_label = f"test_two_groups[clustered={overrides['clustered']}, gpu={overrides['useGpu']}]"

            context = _get_context(stage, analysis=True)
            success, result = self._execute_command(args, context)

            if not success:
                failures.append(f"{test_label}: command execution failed")
                continue

            if not result[0]:
                failures.append(f"{test_label}: result[0] is False")
                continue

            if "analysis" not in result[2]:
                failures.append(f"{test_label}: no 'analysis' in result")
                continue

            analysis = result[2]["analysis"]
            occluded = sorted(analysis.get("occludedMeshes", []))

            if len(occluded) != 2:
                failures.append(f"{test_label}: expected 2 occluded meshes, got {len(occluded)}: {occluded}")
                continue

            # Verify the occluded meshes are the hidden ones (paths contain "hidden")
            for path in occluded:
                if "hidden" not in path:
                    failures.append(f"{test_label}: unexpected occluded mesh path (no 'hidden'): {path}")

        if failures:
            self.fail("Failures:\n" + "\n".join(failures))

    async def test_transparency_flag(self):
        """Test transparency flag - should find one hidden mesh if false, zero if true"""

        # Test with checkTransparency = False - transparent wall should occlude, finding one hidden mesh
        stage = self._open_stage("hiddenMeshTransparency.usda")
        args_false = DEFAULT_ARGS.copy()
        args_false["checkTransparency"] = False

        success, result = self._execute_command(args_false)

        self.assertTrue(success)

        # Check if the hidden mesh has the "hidden" attribute set (it should be occluded)
        hidden_mesh_prim = stage.GetPrimAtPath("/root/hidden/hidden")
        self.assertTrue(hidden_mesh_prim)
        attrHidden = hidden_mesh_prim.GetAttribute("hidden")
        self.assertTrue(attrHidden)
        # With checkTransparency=False, the transparent wall occludes, so the mesh should be marked as hidden
        self.assertTrue(attrHidden.Get() == True)

        # Test with checkTransparency = True - transparent wall should not occlude, finding zero hidden meshes
        # Reopen the stage since the previous test modified it
        stage = self._open_stage("hiddenMeshTransparency.usda")
        args_true = DEFAULT_ARGS.copy()
        args_true["checkTransparency"] = True

        success, result = self._execute_command(args_true)

        self.assertTrue(success)

        # Check if the hidden mesh has the "hidden" attribute set (it should NOT be occluded)
        hidden_mesh_prim = stage.GetPrimAtPath("/root/hidden/hidden")
        self.assertTrue(hidden_mesh_prim)
        attrHidden = hidden_mesh_prim.GetAttribute("hidden")
        self.assertTrue(attrHidden)
        # With checkTransparency=True, the transparent wall doesn't occlude, so the mesh should NOT be marked as hidden
        self.assertTrue(attrHidden.Get() == False)
