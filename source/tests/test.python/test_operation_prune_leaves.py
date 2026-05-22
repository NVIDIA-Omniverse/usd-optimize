# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core import SceneOptimizerCore
from pxr import Usd, UsdGeom

from .test_utils import Test_Operation, _get_context


def _count_leaf_groupings(stage):
    """Counts the number of Xform or Scope prims in the stage that have no children"""
    count = 0
    it = iter(Usd.PrimRange(stage.GetPseudoRoot(), Usd.TraverseInstanceProxies()))
    for prim in it:
        if prim.IsA(UsdGeom.Xform) or prim.IsA(UsdGeom.Scope):
            if len(prim.GetFilteredChildren(Usd.TraverseInstanceProxies())) == 0:
                count += 1

    return count


class Test_Operation_Prune_Leaves(Test_Operation):
    async def test_PruneLeaves(self):
        """Test pruning leaf grouping prims"""
        stage = self._open_stage("pruneLeaves.usda")

        print("Stats Before:")
        context = _get_context(stage)

        success, _, _ = SceneOptimizerCore.getInstance().executeOperation("printStats", context, {})
        self.assertTrue(success)

        # Test existing number of leaf grouping prims
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 15)

        # Prune
        self._execute_json(stage, "pruneLeaves.json")

        # Test no leaf grouping prims remain
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 0)

        # Manually iterate stage and verify only "NotLeaf" xforms still remain
        it = iter(Usd.PrimRange(stage.GetPseudoRoot(), Usd.TraverseInstanceProxies()))
        for prim in it:
            if prim.IsA(UsdGeom.Xform) or prim.IsA(UsdGeom.Scope):
                name = str(prim.GetName())
                if "Leaf" in name:
                    self.assertTrue("NotLeaf" in name)

    async def test_PruneLeaves_specific_prims(self):
        # Test pruning specific paths top level paths.
        stage = self._open_stage("pruneLeaves.usda")

        # Test existing number of leaf grouping prims
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 15)

        # This time we are pruning only some prims:
        # ["/World/Leaf1", "/World/Leaf2"]
        self._execute_json(stage, "pruneLeavesPrimPaths.json")

        # Test that /World/Leaf1 and /World/Leaf2 and children are gone
        prim1 = stage.GetPrimAtPath("/World/Leaf1")
        self.assertFalse(prim1)
        prim2 = stage.GetPrimAtPath("/World/Leaf2")
        self.assertFalse(prim2)

        # Test other top level paths still exist
        prim3 = stage.GetPrimAtPath("/World/Leaf3")
        self.assertTrue(prim3)
        prim4 = stage.GetPrimAtPath("/World/Leaf4")
        self.assertTrue(prim4)
        prim5 = stage.GetPrimAtPath("/World/NotLeaf1")
        self.assertTrue(prim5)
        prim6 = stage.GetPrimAtPath("/World/NotLeaf2")
        self.assertTrue(prim6)

        # Test nested leaf grouping prims still remain
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 7)

        # Test pruning a specific nested path, ensure it leave parent path intact even though it is now a leaf itself.
        stage = self._open_stage("pruneLeaves.usda")

        # Test existing number of leaf grouping prims
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 15)

        # This time we are pruning a more deeply nested path:
        # ["/World/Leaf2/Leaf2/Leaf1"]
        self._execute_json(stage, "pruneLeavesPrimPaths2.json")

        # Test that /World/Leaf2/Leaf2/Leaf1 and children are gone
        prim1 = stage.GetPrimAtPath("/World/Leaf2/Leaf2/Leaf1")
        self.assertFalse(prim1)

        # Test that the parent path still exists, even though it is now a leaf itself.
        prim2 = stage.GetPrimAtPath("/World/Leaf2/Leaf2")
        self.assertTrue(prim2)

        # Test the rest of the other leaves remain intact.
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 11)

        # Test pruning paths that are overlapping, this shouldn't error and just be handled gracefully.
        stage = self._open_stage("pruneLeaves.usda")

        # Prune paths that happen to overlap ie a parent or child of another path.
        # ["/World/Leaf2/Leaf2", "/World/Leaf2"]
        self._execute_json(stage, "pruneLeavesPrimPaths3.json")

        # Test that /World/Leaf2 and children are gone
        prim1 = stage.GetPrimAtPath("/World/Leaf2")
        self.assertFalse(prim1)

        # Test existing number of leaf grouping prims
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 8)


class Test_Operation_Prune_Leaves_Command(Test_Operation):

    OPERATION = "pruneLeaves"

    async def test_PruneLeaves(self):
        """Test pruning leaf grouping prims"""
        stage = self._open_stage("pruneLeaves.usda")

        # Test existing number of leaf grouping prims
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 15)

        args = {"paths": [], "pruneMode": 1}
        self._execute_command(args)

        # Test no leaf grouping prims remain
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 0)

        # Manually iterate stage and verify only "NotLeaf" xforms still remain
        it = iter(Usd.PrimRange(stage.GetPseudoRoot(), Usd.TraverseInstanceProxies()))
        for prim in it:
            if prim.IsA(UsdGeom.Xform) or prim.IsA(UsdGeom.Scope):
                name = str(prim.GetName())
                if "Leaf" in name:
                    self.assertTrue("NotLeaf" in name)

    async def test_PruneLeavesPath(self):
        """Test pruning leaf grouping prims with a path filter"""
        stage = self._open_stage("pruneLeaves.usda")

        # Test existing number of leaf grouping prims
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 15)

        prim = stage.GetPrimAtPath("/World/Leaf1")
        self.assertTrue(prim.IsActive())

        # Get args and execute command
        # Custom execution context
        context = _get_context(stage, verbose=True)
        context.debug = 1

        # Use "deactivate" to test that works
        args = {"paths": ["/World/Leaf1"], "pruneMode": 2}
        self._execute_command(args, context)

        # 14 in the stage as we disabled one
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 14)

        # Leaf1 should be inactive
        self.assertFalse(prim.IsActive())

    async def test_PruneInstance(self):
        """Test pruning starting from an instance"""

        stage = self._open_stage("pruneLeaves.usda")

        # Test existing number of leaf grouping prims
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 15)

        # Get args and execute command
        # Custom execution context
        context = _get_context(stage, verbose=False)

        # Use "deactivate" to test that works
        args = {"paths": ["/World/NotLeaf2/NotLeaf1"], "pruneMode": 2}
        self._execute_command(args, context)

        # Nothing changed - because this is an instance containing instance proxies
        # we can't remove anything.
        leafCount = _count_leaf_groupings(stage)
        self.assertEqual(leafCount, 15)

    async def test_PruneAnalysis(self):
        """Test analysis mode"""

        stage = self._open_stage("pruneLeaves.usda")

        # Run operation with default arguments
        context = _get_context(stage, analysis=True)
        success, result = self._execute_command({}, context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        expected = [
            "/World/Leaf1",
            "/World/Leaf2",
            "/World/Leaf3",
            "/World/NotLeaf1/Leaf1",
            "/World/NotLeaf1/NotLeaf1/NotLeaf1/Leaf1",
            "/World/NotLeaf1/NotLeaf1/Leaf1",
            "/World/Leaf4",
        ]

        # Assert the analysis (straight list of prims) matches the expected result.
        self.assertEqual(analysis, expected)
