# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {}


class Test_Operation_Count_Verts(Test_Operation):

    OPERATION = "countVertices"

    async def test_analysis(self):
        """Test basic analysis"""

        stage = self._open_stage("countVerts.usd")

        # Create analysis context
        context = _get_context(stage, analysis=True)

        args = DEFAULT_ARGS.copy()
        args["high"] = 40000
        args["veryHigh"] = 65000
        args["extreme"] = 150000
        success, result = self._execute_command(args, context=context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Assert expected "high" results
        self.assertTrue("high" in analysis)
        self.assertEqual(len(analysis["high"]), 1)
        self.assertIn("/Geometry/MeshHigh/mesh_0", analysis["high"])
        self.assertEqual(analysis["high"]["/Geometry/MeshHigh/mesh_0"], 46184)

        # Assert veryHigh
        self.assertTrue("veryHigh" in analysis)
        self.assertEqual(len(analysis["veryHigh"]), 1)
        self.assertIn("/Geometry/MeshVeryHigh/mesh_0", analysis["veryHigh"])
        self.assertEqual(analysis["veryHigh"]["/Geometry/MeshVeryHigh/mesh_0"], 69216)

        # Assert extreme
        self.assertTrue("extreme" in analysis)
        self.assertEqual(len(analysis["extreme"]), 1)
        self.assertIn("/Geometry/MeshExtreme/mesh_0", analysis["extreme"])
        self.assertEqual(analysis["extreme"]["/Geometry/MeshExtreme/mesh_0"], 184736)

        # Test again, but set the limits above what the meshes are to verify
        # there are no results
        args["high"] = 5000000
        args["veryHigh"] = 5000001
        args["extreme"] = 5000002

        success, result = self._execute_command(args, context=context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Assert the entries exist, but are all empty
        self.assertTrue("high" in analysis)
        self.assertEqual(len(analysis["high"]), 0)
        self.assertTrue("veryHigh" in analysis)
        self.assertEqual(len(analysis["veryHigh"]), 0)
        self.assertTrue("extreme" in analysis)
        self.assertEqual(len(analysis["extreme"]), 0)

    async def test_bad_thresholds(self):
        """Test with bad threshold values"""

        stage = self._open_stage("countVerts.usd")

        # Create analysis context
        context = _get_context(stage, analysis=True)

        # Set invalid thresholds
        args = DEFAULT_ARGS.copy()
        args["high"] = 100
        args["veryHigh"] = 50
        args["extreme"] = 10
        success, result = self._execute_command(args, context=context)

        # Command should succeed, but SO result should be false
        self.assertTrue(success)
        self.assertFalse(result[0])
