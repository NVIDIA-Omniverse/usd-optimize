# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.scripts import standalone
from pxr import Usd, UsdGeom

from .test_utils import Test_Operation, _get_context

# Mode Enum
MODE_IGNORE = 0
MODE_INDEX = 1
MODE_INDEX_FORCED = 2
MODE_FLATTEN = 3
MODE_REMOVE = 4

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "primvars": [],
    "mode": MODE_IGNORE,
    "simplify": False,
    "removeIfBound": False,
}


def _count_indexed_primvars(stage):
    """Count the number of indexed primvars on any prim in a stage"""
    count = 0
    for prim in stage.TraverseAll():
        papi = UsdGeom.PrimvarsAPI(prim)
        primvars = papi.GetAuthoredPrimvars()
        for primvar in primvars:
            if primvar.IsIndexed():
                count += 1

    return count


def _count_authored_primvars(stage, prims):
    """Count the number of authored primvars on a list of prims"""

    primvarCount = 0
    for primPath in prims:
        prim = stage.GetPrimAtPath(primPath)
        primvars = UsdGeom.PrimvarsAPI(prim).GetPrimvarsWithAuthoredValues()
        primvarCount += len(primvars)

    return primvarCount


class Test_Operation_OptimizePrimvars(Test_Operation):

    OPERATION = "optimizePrimvars"

    async def test_basic_primvar_flatten(self):
        """Test simple flattening of indexed primvars"""

        stage = self._open_stage("primvarMerge.usda")

        count_before = _count_indexed_primvars(stage)
        self.assertEqual(count_before, 5)

        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_FLATTEN
        self._execute_command(args)

        count_after = _count_indexed_primvars(stage)
        self.assertEqual(count_after, 0)

    async def test_primvar_flatten_instance_proxy(self):
        """Test instance proxies are not authored to"""

        stage = self._open_stage("utilityPrimvar.usda")

        count_before = _count_indexed_primvars(stage)
        self.assertEqual(count_before, 1)

        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_FLATTEN
        self._execute_command(args)

        count_after = _count_indexed_primvars(stage)
        self.assertEqual(count_after, 0)

    async def test_index_colors(self):
        """Test indexing display color primvars"""

        stage = self._open_stage("primvarMerge.usda")

        # Generic indexed primvar count
        count_before = _count_indexed_primvars(stage)
        self.assertEqual(count_before, 5)

        # Explicit checks
        constant_prim = stage.GetPrimAtPath("/World/ConstantRedMesh")
        constant_primvar = UsdGeom.PrimvarsAPI(constant_prim).GetPrimvar("displayColor")
        self.assertFalse(constant_primvar.IsIndexed())

        varying_prim = stage.GetPrimAtPath("/World/VaryingMesh")
        varying_primvar = UsdGeom.PrimvarsAPI(varying_prim).GetPrimvar("displayColor")
        self.assertFalse(varying_primvar.IsIndexed())
        self.assertEqual(len(varying_primvar.Get()), 9)

        context = _get_context(stage)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_INDEX
        self._execute_command(args, context)

        count_after = _count_indexed_primvars(stage)
        self.assertEqual(count_after, 9)

        # Should still be false
        self.assertFalse(constant_primvar.IsIndexed())

        # Now indexed, only 3 unique values, and 9 indices
        self.assertTrue(varying_primvar.IsIndexed())
        self.assertEqual(len(varying_primvar.Get()), 3)
        self.assertEqual(len(varying_primvar.GetIndices()), 9)

    def _checkPrimvar(self, stage, path, primvar_name, interpolation, size, index_size):
        """Check a primvar has the expected interpolation, number of values, and number of indices."""

        prim = stage.GetPrimAtPath(path)
        primvar = UsdGeom.PrimvarsAPI(prim).GetPrimvar(primvar_name)

        self.assertEqual(primvar.GetInterpolation(), interpolation)
        self.assertEqual(len(primvar.Get()), size)

        if index_size > 0:
            self.assertEqual(len(primvar.GetIndices()), index_size)
            self.assertEqual(primvar.IsIndexed(), True)
        else:
            self.assertEqual(primvar.IsIndexed(), False)

    async def test_reduce_primvars(self):
        """Test simplifying primvar functionality"""

        stage = self._open_stage("optimizePrimvars.usda")

        # Assert initial state
        self._checkPrimvar(stage, "/World/FaceVaryingToUniform", "displayColor", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/FaceVaryingToConstant", "displayColor", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/FaceVaryingNoReduce", "displayColor", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(
            stage, "/World/FaceVaryingIndexedToUniform", "displayColor", UsdGeom.Tokens.faceVarying, 3, 16
        )
        self._checkPrimvar(
            stage, "/World/FaceVaryingIndexedToConstant", "displayColor", UsdGeom.Tokens.faceVarying, 1, 16
        )
        self._checkPrimvar(stage, "/World/UniformToConstant", "displayColor", UsdGeom.Tokens.uniform, 4, 0)
        self._checkPrimvar(stage, "/World/UniformNoReduce", "displayColor", UsdGeom.Tokens.uniform, 4, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatindexed", UsdGeom.Tokens.uniform, 1, 4)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatnonindexed", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2indexed", UsdGeom.Tokens.uniform, 1, 4)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2nonindexed", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "elemsize", UsdGeom.Tokens.uniform, 8, 0)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["simplify"] = True
        self._execute_command(args)

        self._checkPrimvar(stage, "/World/FaceVaryingToUniform", "displayColor", UsdGeom.Tokens.uniform, 4, 0)
        self._checkPrimvar(stage, "/World/FaceVaryingToConstant", "displayColor", UsdGeom.Tokens.constant, 1, 0)
        self._checkPrimvar(stage, "/World/FaceVaryingNoReduce", "displayColor", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/FaceVaryingIndexedToUniform", "displayColor", UsdGeom.Tokens.uniform, 3, 4)
        self._checkPrimvar(stage, "/World/FaceVaryingIndexedToConstant", "displayColor", UsdGeom.Tokens.constant, 1, 0)
        self._checkPrimvar(stage, "/World/UniformToConstant", "displayColor", UsdGeom.Tokens.constant, 1, 0)
        self._checkPrimvar(stage, "/World/UniformNoReduce", "displayColor", UsdGeom.Tokens.uniform, 4, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatindexed", UsdGeom.Tokens.constant, 1, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatnonindexed", UsdGeom.Tokens.uniform, 4, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2indexed", UsdGeom.Tokens.constant, 1, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2nonindexed", UsdGeom.Tokens.uniform, 4, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "elemsize", UsdGeom.Tokens.uniform, 8, 0)

    async def test_reduce_and_index(self):
        """Test reducing and indexing a primvar"""

        stage = self._open_stage("optimizePrimvars.usda")

        # Assert initial state
        self._checkPrimvar(stage, "/World/FaceVaryingToUniform", "displayColor", UsdGeom.Tokens.faceVarying, 16, 0)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_INDEX
        args["simplify"] = True
        self._execute_command(args)

        # After, should be 3 unique values and 4 indices
        self._checkPrimvar(stage, "/World/FaceVaryingToUniform", "displayColor", UsdGeom.Tokens.uniform, 3, 4)

    async def test_reduce_and_flatten(self):
        """Test reducing and flattening a primvar"""

        stage = self._open_stage("optimizePrimvars.usda")

        # Assert initial state
        self._checkPrimvar(
            stage, "/World/FaceVaryingIndexedToUniform", "displayColor", UsdGeom.Tokens.faceVarying, 3, 16
        )

        context = _get_context(stage, report=True)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_FLATTEN
        args["simplify"] = True
        self._execute_command(args, context)

        # After, should be 4 flat values
        self._checkPrimvar(stage, "/World/FaceVaryingIndexedToUniform", "displayColor", UsdGeom.Tokens.uniform, 4, 0)

    async def test_primvar_filter(self):
        """Test filtering on primvar name"""

        stage = self._open_stage("optimizePrimvars.usda")

        # Assert initial state
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatindexed", UsdGeom.Tokens.uniform, 1, 4)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatnonindexed", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2indexed", UsdGeom.Tokens.uniform, 1, 4)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2nonindexed", UsdGeom.Tokens.faceVarying, 16, 0)

        context = _get_context(stage, report=True)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_INDEX
        args["primvars"] = ["floatindexed"]
        args["simplify"] = True
        self._execute_command(args, context)

        # After, only floatindexed should have changed
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatindexed", UsdGeom.Tokens.constant, 1, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "floatnonindexed", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2indexed", UsdGeom.Tokens.uniform, 1, 4)
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "float2nonindexed", UsdGeom.Tokens.faceVarying, 16, 0)

    async def test_remove_primvars(self):
        """Test removing primvars"""

        stage = self._open_stage("optimizePrimvars.usda")

        prims = ["/World/ArbitraryPrimvars", "/World/FaceVaryingIndexedToUniform"]

        primvarCount = _count_authored_primvars(stage, prims)
        self.assertEqual(primvarCount, 10)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_REMOVE
        self._execute_command(args)

        primvarCount = _count_authored_primvars(stage, prims)
        self.assertEqual(primvarCount, 0)

    async def test_remove_primvars_bound(self):
        """Test removing primvars only when bound to a material"""

        stage = self._open_stage("optimizePrimvars.usda")

        prims = ["/World/ArbitraryPrimvars", "/World/BoundWithColor"]

        primvarCount = _count_authored_primvars(stage, prims)
        self.assertEqual(primvarCount, 10)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_REMOVE
        args["removeIfBound"] = True
        self._execute_command(args)

        # Only the one displayColor primvar should have been removed from /World/BoundWithColor
        primvarCount = _count_authored_primvars(stage, prims)
        self.assertEqual(primvarCount, 9)

        # Run function again, with removeIfBound disabled
        args["removeIfBound"] = False
        self._execute_command(args)

        # Everything should be removed now
        primvarCount = _count_authored_primvars(stage, prims)
        self.assertEqual(primvarCount, 0)

    async def test_reindexing(self):
        """Test reindexing badly indexed data"""

        stage = self._open_stage("optimizePrimvars.usda")

        prims = ["/World/ArbitraryPrimvars"]

        primvarCount = _count_authored_primvars(stage, prims)
        self.assertEqual(primvarCount, 9)

        # Assert the bad indexing
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "badindexing", UsdGeom.Tokens.faceVarying, 16, 16)

        # Run only in index mode
        json = """[
            {"operation": "optimizePrimvars",
            "paths": ["/World/ArbitraryPrimvars"],
            "mode": 1
             }]"""
        status = standalone.execute_commands_from_json(stage, json)

        # Assert the bad indexing remains
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "badindexing", UsdGeom.Tokens.faceVarying, 16, 16)

        # Run in forced index mode
        json = """[
            {"operation": "optimizePrimvars",
            "paths": ["/World/ArbitraryPrimvars"],
            "mode": 2
             }]"""
        status = standalone.execute_commands_from_json(stage, json)

        # Assert the bad indexing was fixed
        self._checkPrimvar(stage, "/World/ArbitraryPrimvars", "badindexing", UsdGeom.Tokens.faceVarying, 2, 16)

    async def test_invalid_mesh(self):
        """Test mesh with missing data"""

        stage = self._open_stage("optimizePrimvars.usda")

        prims = ["/World/InvalidMesh"]

        # Empty, no indices
        self._checkPrimvar(stage, "/World/InvalidMesh", "st", UsdGeom.Tokens.faceVarying, 0, 0)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_INDEX
        args["simplify"] = True
        self._execute_command(args)

        # Should still be zero but not have crashed!
        self._checkPrimvar(stage, "/World/InvalidMesh", "st", UsdGeom.Tokens.faceVarying, 0, 0)

    async def test_simplify_string_primvar(self):
        """Test simplifying string primvars"""

        stage = self._open_stage("optimizePrimvars.usda")

        self._checkPrimvar(stage, "/World/StringPrimvars", "stringValues", UsdGeom.Tokens.faceVarying, 16, 0)
        self._checkPrimvar(stage, "/World/StringPrimvars", "stringValuesUniform", UsdGeom.Tokens.faceVarying, 16, 0)

        # Run function
        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_INDEX
        args["simplify"] = True
        args["paths"] = ["/World/StringPrimvars"]
        self._execute_command(args)

        self._checkPrimvar(stage, "/World/StringPrimvars", "stringValues", UsdGeom.Tokens.constant, 1, 0)
        self._checkPrimvar(stage, "/World/StringPrimvars", "stringValuesUniform", UsdGeom.Tokens.uniform, 4, 4)

    async def test_indexing_reference(self):
        """Test setting indices on referenced data"""

        stage = self._open_stage("optimizePrimvarsRef.usda")

        self._checkPrimvar(stage, "/World/ReferencedCube/Cube", "st", UsdGeom.Tokens.faceVarying, 24, 0)

        args = DEFAULT_ARGS.copy()
        args["mode"] = MODE_INDEX
        self._execute_command(args)

        self._checkPrimvar(stage, "/World/ReferencedCube/Cube", "st", UsdGeom.Tokens.faceVarying, 4, 24)

    async def test_analysis_mode(self):
        """Test analysis mode"""

        stage = self._open_stage("validatePrimvars.usda")

        context = _get_context(stage, analysis=True)

        success, result = self._execute_command(DEFAULT_ARGS, context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Assert the expected indexable issues
        indexable = analysis["indexable"]

        # Assert the length, then assert all the things we expect.
        # This means we have indirectly asserted that things that shouldn't be here aren't.
        self.assertEqual(len(indexable), 39)

        self.assertIn("/World/CubeIndexable.primvars:st", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableColor3d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableColor3f", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableColor3h", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableColor4d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableColor4f", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableColor4h", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableDouble", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableDouble2", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableDouble3", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableDouble4", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableFloat", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableFloat2", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableFloat3", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableFloat4", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableHalf", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableHalf2", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableHalf3", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableHalf4", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableInt2", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableInt3", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableInt4", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableMatrix3d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableMatrix4d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableNormal3d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableNormal3f", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableNormal3h", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexablePoint3d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexablePoint3f", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableString", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableTexCoord2d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableTexCoord2f", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableTexCoord2h", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableTexCoord3d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableTexCoord3f", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableTexCoord3h", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableVector3d", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableVector3f", indexable)
        self.assertIn("/World/CubeManyIndexable.primvars:indexableVector3h", indexable)

        # Assert the expected OOB issues
        outOfBounds = analysis["outOfBounds"]
        self.assertEqual(len(outOfBounds), 1)

        self.assertIn("/World/CubeOutOfBounds.primvars:st", outOfBounds)

        # Assert the expected non-array issues
        nonArray = analysis["nonArray"]
        self.assertEqual(len(nonArray), 1)

        self.assertIn("/World/CubeNonArray.primvars:nonArray", nonArray)
