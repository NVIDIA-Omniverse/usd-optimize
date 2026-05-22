# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {"paths": []}


class Test_Command_Rtx_Mesh_Count(Test_Operation):

    OPERATION = "rtxMeshCount"

    async def test_rtx_mesh_count_scene(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains a number of different cases.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(DEFAULT_ARGS, context)

        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 64)
        self.assertEqual(analysis.get("rtxMeshCount"), 40)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 18)

    async def test_rtx_mesh_count_overlapping_paths(self):
        """
        Tests analyzing the RTX mesh count using input paths that are overlapping to ensure we don't double count prims.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World", "/World/Basic"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)

        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 64)
        self.assertEqual(analysis.get("rtxMeshCount"), 40)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 18)

    async def test_rtx_mesh_count_basic(self):
        """
        Tests analyzing the RTX mesh count in a basic scene with a single mesh.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Basic"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        # there are 2 RTX meshes, one visible and one hidden but both count. There is also a deactivated mesh that
        # should not be counted. Only the visible mesh counts towards the acceleration structure count.
        self.assertEqual(analysis.get("rtxAccelStructCount"), 1)
        self.assertEqual(analysis.get("rtxMeshCount"), 2)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 2)

        # get the RTX mesh prims to verify their path
        rtxMeshPrims = analysis.get("rtxMeshPrims")
        self.assertIsNotNone(rtxMeshPrims)
        self.assertEqual(len(rtxMeshPrims), 2)
        self.assertIn("/World/Basic/Cube", rtxMeshPrims)
        self.assertIn("/World/Basic/Sphere", rtxMeshPrims)
        self.assertNotIn("/World/Basic/Plane", rtxMeshPrims)

    async def test_rtx_mesh_count_cameras(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains cameras.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Cameras"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        # cameras no longer count towards the acceleration structure or mesh count
        self.assertEqual(analysis.get("rtxAccelStructCount"), 0)
        self.assertEqual(analysis.get("rtxMeshCount"), 0)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 0)

    async def test_rtx_mesh_count_duplicates(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains duplicate meshes.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Basic", "/World/Duplicates"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 3)
        self.assertEqual(analysis.get("rtxMeshCount"), 4)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 2)

    async def test_rtx_mesh_count_degenerate(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains degenerate meshes.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Basic", "/World/Degen"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        # one degenerate mesh should be ignored - the other has zero extents but valid geometry so should be counted
        self.assertEqual(analysis.get("rtxAccelStructCount"), 2)
        self.assertEqual(analysis.get("rtxMeshCount"), 3)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 3)

    async def test_rtx_mesh_count_shapes(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains shape primitives
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Shapes"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 5)
        self.assertEqual(analysis.get("rtxMeshCount"), 5)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 5)

        # now run again introducing duplicate shapes to insure they are counted correctly
        args["paths"] = ["/World/Shapes", "/World/ShapeDuplicates"]
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 10)
        self.assertEqual(analysis.get("rtxMeshCount"), 10)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 5)

    async def test_rtx_mesh_count_reference(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains meshes that are references.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Basic", "/World/References"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 3)
        self.assertEqual(analysis.get("rtxMeshCount"), 4)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 2)

    async def test_rtx_mesh_count_basic_instances(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains meshes that are basic instances.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/TorusInstance"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 4)
        self.assertEqual(analysis.get("rtxMeshCount"), 1)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 1)

    async def test_rtx_mesh_count_instance_defined_prototype(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains mesh instances that use a prototype that is defined.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/DiskInstance"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        # there should be 2 meshes, one for the prototype and one for the instances, but only 1 unique mesh
        self.assertEqual(analysis.get("rtxAccelStructCount"), 3)
        self.assertEqual(analysis.get("rtxMeshCount"), 2)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 1)

    async def test_rtx_mesh_count_nested_instances(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains nested mesh instances.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/PlaneInstance"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 12)
        self.assertEqual(analysis.get("rtxMeshCount"), 5)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 5)

    async def test_rtx_mesh_count_invisible_instances(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains instances that are all invisible
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/InvisibleInstance"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 2)
        self.assertEqual(analysis.get("rtxMeshCount"), 1)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 1)

    async def test_rtx_mesh_count_point_instancer(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains a point instancer.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/PointInstancer"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 4)
        self.assertEqual(analysis.get("rtxMeshCount"), 1)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 1)

        # now run again on the point instancer with multiple prototypes with some invisible instances
        args["paths"] = ["/World/PointInstancerMultiple"]
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 18)
        self.assertEqual(analysis.get("rtxMeshCount"), 10)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 8)

        # now run again on the point instancer with multiple prototypes with no ids and one prototype unused
        args["paths"] = ["/World/PointInstancerMultiple2"]
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 2)
        self.assertEqual(analysis.get("rtxMeshCount"), 2)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 2)

    async def test_rtx_mesh_count_point_instancer_invisible(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains an invisible point instancer.
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/InvisiblePointInstancer"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        self.assertEqual(analysis.get("rtxAccelStructCount"), 0)
        self.assertEqual(analysis.get("rtxMeshCount"), 1)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 1)

    async def test_rtx_mesh_count_geom_subsets(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains a mesh with geom subsets
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/GeomSubsets"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        # even though there are multiple geom subsets, this is still a single mesh
        self.assertEqual(analysis.get("rtxAccelStructCount"), 1)
        self.assertEqual(analysis.get("rtxMeshCount"), 1)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 1)

    async def test_rtx_mesh_count_curves(self):
        """
        Tests analyzing the RTX mesh count in a scene that contains curves
        """
        stage = self._open_stage("rtxMeshCount.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/World/Curves"]
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)
        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        # 2 acceleration structures for the curves, but no RTX meshes as curves don't count as meshes
        self.assertEqual(analysis.get("rtxAccelStructCount"), 2)
        self.assertEqual(analysis.get("rtxMeshCount"), 0)
        self.assertEqual(analysis.get("rtxUniqueMeshCount"), 0)
