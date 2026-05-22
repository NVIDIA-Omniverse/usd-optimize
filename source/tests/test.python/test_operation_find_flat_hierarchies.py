# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "maxChildren": 5,
    "considerAllChildren": True,
}


class Test_Command_Find_Flat_Hierarchies(Test_Operation):

    OPERATION = "findFlatHierarchies"

    def _check_flat_hierarchies(self, data, consider_all_children):
        """Check that the expected flat hierarchies are in the data"""
        if consider_all_children:
            self.assertEqual(len(data), 4)
        else:
            self.assertEqual(len(data), 3)
        num = data.get("/World")
        self.assertIsNotNone(num)
        self.assertEqual(num, 10)
        num = data.get("/World/Xform_02")
        self.assertIsNotNone(num)
        self.assertEqual(num, 12)
        num = data.get("/World/Xform_05/Xform_10")
        self.assertIsNotNone(num)
        self.assertEqual(num, 11)
        if consider_all_children:
            num = data.get("/World/Xform_07")
            self.assertIsNotNone(num)
            self.assertEqual(num, 9)
        else:
            self.assertFalse("/World/Xform_07" in data)

    async def test_consider_all_children(self):
        """Check that prims with more than 5 of any state of children are found"""
        stage = self._open_stage("flatHierarchies.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        context = _get_context(stage, report=True)
        success, opResult = self._execute_command(args, context)

        self.assertTrue(success)

        # check the flat hierarchies found are as expected
        self._check_flat_hierarchies(opResult[2], True)

    async def test_dont_consider_all_children(self):
        """Check that prims with more than 5 of any state of children are found"""
        stage = self._open_stage("flatHierarchies.usda")

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["considerAllChildren"] = False
        context = _get_context(stage, report=True)
        success, opResult = self._execute_command(args, context)

        self.assertTrue(success)

        # check the flat hierarchies found are as expected
        self._check_flat_hierarchies(opResult[2], False)

    async def test_analysis(self):
        """Same as the above test but using the analysis context"""
        stage = self._open_stage("flatHierarchies.usda")

        # Execute the command in analysis mode and assert success
        args = DEFAULT_ARGS.copy()
        context = _get_context(stage, analysis=True)
        success, opResult = self._execute_command(args, context)

        self.assertTrue(success)

        # assert the analysis exists
        self.assertTrue("analysis" in opResult[2])
        analysis = opResult[2]["analysis"]

        # check the flat hierarchy results
        self.assertTrue("flatHierarchies" in analysis)
        self._check_flat_hierarchies(analysis["flatHierarchies"], True)
