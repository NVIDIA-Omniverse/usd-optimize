# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core import SceneOptimizerCore
from omni.scene.optimizer.core.scripts import standalone
from pxr import Usd

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "removeInterpolated": False,
    "epsilonD": 1e-12,
    "epsilonF": 1e-6,
}

XFORM = "xformOp:transform"
TESTATTR = "testAttr"
SCALE = "xformOp:scale"


def _get_sample_count(prim, attr_name):
    """Return the number of time samples an attribute has"""
    attr = prim.GetAttribute(attr_name)
    return len(attr.GetTimeSamples())


class Test_Operation_OptimizeTimeSamples(Test_Operation):

    OPERATION = "optimizeTimeSamples"

    async def test_simple_duplicates(self):
        """Test simple duplicate removal"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        # Cube with simple linear animation
        primLinear = stage.GetPrimAtPath("/CubeLinear")
        self.assertEqual(_get_sample_count(primLinear, XFORM), 120)

        # Cube that animates linearly, then speeds up
        primLinearStep = stage.GetPrimAtPath("/CubeLinearStep")
        self.assertEqual(_get_sample_count(primLinearStep, XFORM), 120)

        # Cube that animates, then pauses, then continues animating
        primLinearHold = stage.GetPrimAtPath("/CubeLinearHold")
        self.assertEqual(_get_sample_count(primLinearHold, XFORM), 120)

        # Build JSON args and execute command
        json = """[
            {"operation": "optimizeTimeSamples",
            "paths": [],
            "removeInterpolated": false,
            "epsilonD": 1e-12,
            "epsilonF": 1e-6 }]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertTrue(status)

        # The only thing removed should have been the redundant trailing samples
        self.assertEqual(_get_sample_count(primLinear, XFORM), 48)
        self.assertEqual(_get_sample_count(primLinearStep, XFORM), 48)

        # This one should have the trailing samples and also the duplicate held
        # samples in the middle removed
        self.assertEqual(_get_sample_count(primLinearHold, XFORM), 37)

    async def test_redundant_interpolated(self):
        """Test removing redundant samples that can be interpolated"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        # Cube with simple linear animation
        primLinear = stage.GetPrimAtPath("/CubeLinear")
        self.assertEqual(_get_sample_count(primLinear, XFORM), 120)

        # Cube that animates linearly, then speeds up
        primLinearStep = stage.GetPrimAtPath("/CubeLinearStep")
        self.assertEqual(_get_sample_count(primLinearStep, XFORM), 120)

        # Cube that animates, then pauses, then continues animating
        primLinearHold = stage.GetPrimAtPath("/CubeLinearHold")
        self.assertEqual(_get_sample_count(primLinearHold, XFORM), 120)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Basic linear animation - this should only have start/end frame left
        self.assertEqual(_get_sample_count(primLinear, XFORM), 2)

        # Linear animation that changes velocity. This should have start+end
        # and then the point it changes
        self.assertEqual(_get_sample_count(primLinearStep, XFORM), 3)

        # Linear but pauses animation part way through. This should have four;
        # the start/end and then when it stops and then restarts
        self.assertEqual(_get_sample_count(primLinearHold, XFORM), 4)

    async def test_all_duplicates(self):
        """Test prims with multiple time samples of values that never change"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        primStatic = stage.GetPrimAtPath("/CubeStatic")
        primStrings = stage.GetPrimAtPath("/CubeStringsDuplicate")

        # Assert initial expected counts
        self.assertEqual(_get_sample_count(primStatic, XFORM), 2)
        self.assertEqual(_get_sample_count(primStrings, TESTATTR), 6)

        # And no default value
        self.assertIsNone(primStatic.GetAttribute(XFORM).Get())
        self.assertIsNone(primStrings.GetAttribute(TESTATTR).Get())

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # All samples should have been removed, and the single unique
        # value set directly on the attributes
        self.assertEqual(_get_sample_count(primStatic, XFORM), 0)
        self.assertIsNotNone(primStatic.GetAttribute(XFORM).Get())

        self.assertEqual(_get_sample_count(primStrings, TESTATTR), 0)
        self.assertIsNotNone(primStrings.GetAttribute(TESTATTR).Get())
        self.assertEqual(primStrings.GetAttribute(TESTATTR).Get(), "foo")

    async def test_held_types(self):
        """Test attributes that are held / cannot be interpolated"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        primStrings = stage.GetPrimAtPath("/CubeStrings")
        primInts = stage.GetPrimAtPath("/CubeInt")

        # Starts with 6 samples but only three unique values
        self.assertEqual(_get_sample_count(primStrings, TESTATTR), 6)

        # Int starts with 9
        self.assertEqual(_get_sample_count(primInts, TESTATTR), 9)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # After processing should only have three samples
        self.assertEqual(_get_sample_count(primStrings, TESTATTR), 3)

        # And still 9 - int has no duplicates and can't be interpolated
        self.assertEqual(_get_sample_count(primInts, TESTATTR), 9)

        # Extra asserts that they are held to the expected place
        attr = primStrings.GetAttribute(TESTATTR)
        self.assertEqual(attr.Get(1), "foo")
        self.assertEqual(attr.Get(2), "bar")
        self.assertEqual(attr.Get(6), "baz")

    async def test_vec3f_interpolated(self):
        """Test Vec3f types with interpolation"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeVec3f")
        primNonLinear = stage.GetPrimAtPath("/CubeVec3fNonLinear")
        primArray = stage.GetPrimAtPath("/CubeVec3fArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 8)
        self.assertEqual(_get_sample_count(primNonLinear, TESTATTR), 8)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 4)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # The linear vectors should only have start/end samples remaining
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)

        # The non-linear vectors should all still be there
        self.assertEqual(_get_sample_count(primNonLinear, TESTATTR), 8)

        # The arrays should have been reduced to start/end also
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_float_hold_precision(self):
        """Test floats with precision errors"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeFloatHold")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 8)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # Assert that three remain; the first, the held sample
        # with all duplicates removed, and then the last.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 3)

        # Re-open the stage, then test again with a much smaller
        # epsilon
        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeFloatHold")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 8)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["epsilonF"] = 1e-14
        self._execute_command(args)

        # Assert that five remain - this time the precision considers
        # the difference to be relevant
        self.assertEqual(_get_sample_count(prim, TESTATTR), 5)

    async def test_referenced_data(self):
        """Test a prim with time samples that is referenced in"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeReferenced")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, XFORM), 48)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Assert that even though the data was referenced it can still
        # be handled
        self.assertEqual(_get_sample_count(prim, XFORM), 2)

    async def test_single_time_sample(self):
        """Test an attribute with a single time sample"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeSingle")

        # Assert initial state, a single time sample and no default value
        self.assertEqual(_get_sample_count(prim, XFORM), 1)
        self.assertIsNone(prim.GetAttribute(XFORM).Get())

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # After processing, there should be no time samples
        # and we should now have a default xform value
        self.assertEqual(_get_sample_count(prim, XFORM), 0)
        self.assertIsNotNone(prim.GetAttribute(XFORM).Get())

    async def test_duplicates(self):
        """Test an attribute with only duplicates, including precision errors"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeStaticDuplicates")

        # Assert initial state, a single time sample and no default value
        self.assertEqual(_get_sample_count(prim, XFORM), 12)
        self.assertIsNone(prim.GetAttribute(XFORM).Get())

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # After processing, there should be no time samples
        # and we should now have a default xform value
        # All of the equal attributes, accounting for their fake precision
        # errors, should have been considered equal correctly
        self.assertEqual(_get_sample_count(prim, XFORM), 0)
        self.assertIsNotNone(prim.GetAttribute(XFORM).Get())

    async def test_last_value(self):
        """Test for a bug that caused undefined memory to be used for the final value"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeDuplicateScale")

        # Assert initial state, a single time sample and no default value
        self.assertEqual(_get_sample_count(prim, SCALE), 257)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # All time samples removed as the whole lot were duplicates
        self.assertEqual(_get_sample_count(prim, SCALE), 0)

        # The bug would have been here - the time samples were correctly cleared
        # but the value would be garbage. Unfortunately in the unit test (when
        # written) it worked as expected, as it's somewhat intermittent depending
        # on the process/memory usage.
        attr = prim.GetAttribute("xformOp:scale")
        self.assertEqual(attr.Get(), (1, 1, 1))

    async def test_timesample_quats(self):
        """Test quat values"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        primd = stage.GetPrimAtPath("/CubeQuatd")
        self.assertEqual(_get_sample_count(primd, TESTATTR), 5)

        primdArray = stage.GetPrimAtPath("/CubeQuatdArray")
        self.assertEqual(_get_sample_count(primdArray, TESTATTR), 5)

        primf = stage.GetPrimAtPath("/CubeQuatf")
        self.assertEqual(_get_sample_count(primf, TESTATTR), 5)

        primfArray = stage.GetPrimAtPath("/CubeQuatfArray")
        self.assertEqual(_get_sample_count(primfArray, TESTATTR), 5)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args)

        # All duplicates removed
        self.assertEqual(_get_sample_count(primd, TESTATTR), 0)
        self.assertEqual(_get_sample_count(primdArray, TESTATTR), 0)
        self.assertEqual(_get_sample_count(primf, TESTATTR), 0)
        self.assertEqual(_get_sample_count(primfArray, TESTATTR), 0)

        # Re-open
        stage = self._open_stage("optimizeTimeSamples.usda")

        primd = stage.GetPrimAtPath("/CubeQuatd")
        self.assertEqual(_get_sample_count(primd, TESTATTR), 5)

        primdArray = stage.GetPrimAtPath("/CubeQuatdArray")
        self.assertEqual(_get_sample_count(primdArray, TESTATTR), 5)

        primf = stage.GetPrimAtPath("/CubeQuatf")
        self.assertEqual(_get_sample_count(primf, TESTATTR), 5)

        primfArray = stage.GetPrimAtPath("/CubeQuatfArray")
        self.assertEqual(_get_sample_count(primfArray, TESTATTR), 5)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # All duplicates still removed with removeInterpolated enabled
        self.assertEqual(_get_sample_count(primd, TESTATTR), 0)
        self.assertEqual(_get_sample_count(primdArray, TESTATTR), 0)
        self.assertEqual(_get_sample_count(primf, TESTATTR), 0)
        self.assertEqual(_get_sample_count(primfArray, TESTATTR), 0)

    async def test_timesample_halfs(self):
        """Test half values"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        print("Stats Before:")
        context = _get_context(stage)

        success, _, _ = SceneOptimizerCore.getInstance().executeOperation("printStats", context, {})
        self.assertTrue(success)

        prim = stage.GetPrimAtPath("/CubeHalf")
        self.assertEqual(_get_sample_count(prim, TESTATTR), 5)

        primArray = stage.GetPrimAtPath("/CubeHalfArray")
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 5)

        # Custom execution context
        context = _get_context(stage, report=True)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        self._execute_command(args, context)

        # No duplicates
        self.assertEqual(_get_sample_count(prim, TESTATTR), 5)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 5)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Now only two left, middle 3 removed
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_timesample_instance_proxy(self):
        """Test authoring to an instance proxy does not fail"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        # Instance proxy should have 7 time samples
        prim = stage.GetPrimAtPath("/CubeInstanced/CubeInstance")
        self.assertEqual(_get_sample_count(prim, TESTATTR), 7)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["paths"] = ["/CubeInstanced/CubeInstance"]
        self._execute_command(args)

        # Should still have 7 - can't author to it
        prim = stage.GetPrimAtPath("/CubeInstanced/CubeInstance")
        self.assertEqual(_get_sample_count(prim, TESTATTR), 7)

    async def test_vec4f(self):
        """Test Vec4f types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeVec4f")
        primArray = stage.GetPrimAtPath("/CubeVec4fArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 8)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 4)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Only beginning/end should remain - linear samples all removed.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_vec2d(self):
        """Test Vec2d types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeVec2d")
        primArray = stage.GetPrimAtPath("/CubeVec2dArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 8)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 4)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Only beginning/end should remain - linear samples all removed.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 6)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_vec2f(self):
        """Test Vec2f types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeVec2f")
        primArray = stage.GetPrimAtPath("/CubeVec2fArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 7)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 4)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Only beginning/end should remain - linear samples all removed.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_vec3d(self):
        """Test Vec3d types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeVec3d")
        primArray = stage.GetPrimAtPath("/CubeVec3dArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 7)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 4)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Only beginning/end should remain - linear samples all removed.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_vec4d(self):
        """Test Vec4d types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeVec4d")
        primArray = stage.GetPrimAtPath("/CubeVec4dArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 7)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 4)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Only beginning/end should remain - linear samples all removed.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_matrix2d(self):
        """Test Matrix2d types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeMatrix2d")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 6)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Only beginning/end should remain - linear samples all removed.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)

    async def test_matrix3d(self):
        """Test Matrix3d types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeMatrix3d")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 6)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Nothing removed, all required
        self.assertEqual(_get_sample_count(prim, TESTATTR), 6)

    async def test_doubles(self):
        """Test double types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeDoubles")
        primArray = stage.GetPrimAtPath("/CubeDoublesArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 10)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 10)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # Only beginning/end should remain - linear samples all removed.
        self.assertEqual(_get_sample_count(prim, TESTATTR), 2)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 2)

    async def test_floats(self):
        """Test float types"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        prim = stage.GetPrimAtPath("/CubeFloats")
        primArray = stage.GetPrimAtPath("/CubeFloatsArray")

        # Assert initial state
        self.assertEqual(_get_sample_count(prim, TESTATTR), 8)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 8)

        # Get args and execute command
        args = DEFAULT_ARGS.copy()
        args["removeInterpolated"] = True
        self._execute_command(args)

        # A couple of samples removed, most data remains
        self.assertEqual(_get_sample_count(prim, TESTATTR), 6)
        self.assertEqual(_get_sample_count(primArray, TESTATTR), 6)

    async def test_analysis(self):
        """Test analysis mode"""

        stage = self._open_stage("optimizeTimeSamples.usda")

        context = _get_context(stage, analysis=True)

        success, result = self._execute_command(DEFAULT_ARGS, context)

        # Assert analysis exists
        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Assert expected number of attributes with redundant samples
        self.assertEqual(len(analysis), 21)

        # Assert total number of samples/redundant samples
        totalRedundant = 0
        totalCount = 0
        for _, values in analysis.items():
            totalRedundant += values[0]
            totalCount += values[1]

        # Note totalCount is only the total count of samples from attributes
        # with redundant samples, not all attributes (as we don't include those
        # in the analysis)
        self.assertEqual(totalRedundant, 547)
        self.assertEqual(totalCount, 724)

        # Assert a few specific details of the results
        # All 257 redundant
        self.assertIn("/CubeDuplicateScale.xformOp:scale", analysis)
        result = analysis["/CubeDuplicateScale.xformOp:scale"]
        self.assertEqual(result[0], 257)
        self.assertEqual(result[1], 257)

        # 5 of 8 redundant
        self.assertIn("/CubeFloatHold.testAttr", analysis)
        result = analysis["/CubeFloatHold.testAttr"]
        self.assertEqual(result[0], 5)
        self.assertEqual(result[1], 8)

        # 72 of 120 redundant
        self.assertIn("/CubeLinearStep.xformOp:transform", analysis)
        result = analysis["/CubeLinearStep.xformOp:transform"]
        self.assertEqual(result[0], 72)
        self.assertEqual(result[1], 120)

        # 1 of 1 redundant
        self.assertIn("/CubeSingle.xformOp:transform", analysis)
        result = analysis["/CubeSingle.xformOp:transform"]
        self.assertEqual(result[0], 1)
        self.assertEqual(result[1], 1)

        # 6 of 6 redundant
        self.assertIn("/CubeStringsDuplicate.testAttr", analysis)
        result = analysis["/CubeStringsDuplicate.testAttr"]
        self.assertEqual(result[0], 6)
        self.assertEqual(result[1], 6)
