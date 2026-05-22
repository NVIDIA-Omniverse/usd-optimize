# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation, _get_context

# Constants
MODE_REMOVE = 0
MODE_BLOCK = 1

# Default arguments for the command
DEFAULT_ARGS = {
    "primPaths": [],
    "attributes": [],
    "mode": MODE_REMOVE,
}


def _primvar_has_val(prim, primvar_name):
    """Test if a prim has a primvar with an authored value"""

    primvarsAPI = UsdGeom.PrimvarsAPI(prim)
    primvar = primvarsAPI.GetPrimvar(primvar_name)
    return primvar.HasAuthoredValue()


class Test_Operation_Remove_Unused_UVs(Test_Operation):
    """Test cases for the Remove Unused UVs operation"""

    OPERATION = "removeUnusedUVs"

    async def setUp(self):
        """Common setup"""

        await super().setUp()

        self.stage = self._open_stage("unusedUVs.usda")
        self.context = _get_context(self.stage)
        self.args = DEFAULT_ARGS.copy()

    async def test_default_args(self):
        """Test against various cases with the default arguments"""

        # Assert initial state

        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/Geometry/CubePrototype/Cube"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeWithTexture"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeUnusedUVs"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "testUVs"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialWithAsset"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubePrimvarReader"), "readerTest"))

        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # Assert new state
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeWithTexture"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "testUVs"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialWithAsset"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubePrimvarReader"), "readerTest"))

        # Unused removed
        self.assertFalse(_primvar_has_val(self.stage.GetPrimAtPath("/World/Geometry/CubePrototype/Cube"), "st"))
        self.assertFalse(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "st"))
        self.assertFalse(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeUnusedUVs"), "st"))

    async def test_custom_primvars(self):
        """Test with custom primvar names"""

        # Assert initial state
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/Geometry/CubePrototype/Cube"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeWithTexture"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeUnusedUVs"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "testUVs"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialWithAsset"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubePrimvarReader"), "readerTest"))

        # Add some custom attributes to check, then re-test
        self.args["attributes"] = ["testUVs", "readerTest"]
        self.args["mode"] = MODE_BLOCK

        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # Assert new state
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeWithTexture"), "st"))
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialWithAsset"), "st"))
        # Custom, but used by a primvar reader so we don't expect this to be removed
        self.assertTrue(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubePrimvarReader"), "readerTest"))

        # Original unused removed, plus also the extra unused testUVs
        self.assertFalse(_primvar_has_val(self.stage.GetPrimAtPath("/World/Geometry/CubePrototype/Cube"), "st"))
        self.assertFalse(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "st"))
        self.assertFalse(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeUnusedUVs"), "st"))
        self.assertFalse(_primvar_has_val(self.stage.GetPrimAtPath("/World/CubeMaterialNoUse"), "testUVs"))

    async def test_analysis(self):
        """Test analysis mode"""

        self.context.analysisMode = 1
        self.context.verbose = 1
        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Assert expected results
        self.assertEqual(len(analysis), 4)

        # Assert the expected attributes are present
        self.assertIn("/World/Geometry/CubePrototype/Cube.primvars:st", analysis)
        self.assertIn("/World/Geometry/CubePrototype/Cube.primvars:st:indices", analysis)
        self.assertIn("/World/CubeUnusedUVs.primvars:st", analysis)
        self.assertIn("/World/CubeMaterialNoUse.primvars:st", analysis)

    async def test_analysis_with_paths(self):
        """Test analysis with no results"""

        self.args["paths"] = ["/World/NoPrim"]

        self.context.analysisMode = 1
        self.context.verbose = 1
        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Nothing matched
        self.assertEqual(len(analysis), 0)
