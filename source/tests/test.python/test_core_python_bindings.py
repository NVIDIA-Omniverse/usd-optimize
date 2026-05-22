# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import os
import tempfile
import unittest

from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from pxr import Usd


class TestCorePythonBindings(unittest.TestCase):

    def test_executionContext(self):
        """
        Test the basic set_stage / remove_stage functionality of the ExecutionContext, which is Python-only
        """
        # create  a new execution context and check that the usdStageId is initially -1
        context = ExecutionContext()
        self.assertEqual(context.usdStageId, -1)

        # test setting a stage on the context
        stage = Usd.Stage.CreateInMemory()
        self.assertTrue(context.set_stage(stage))
        self.assertNotEqual(context.usdStageId, -1)

        # test removing the stage from the context
        context.remove_stage()
        self.assertEqual(context.usdStageId, -1)

    def test_executionContext_reportPath_roundtrip(self):
        """Regression: pybind char* def_readwrite stored a pointer into a
        temporary std::string that destructed after the setter — context.reportPath
        could read garbage once the heap was reused. The setter must own the buffer."""
        context = ExecutionContext()
        self.assertIsNone(context.reportPath)

        path = "C:/some/path/to/report.txt" if os.name == "nt" else "/tmp/report.txt"
        context.reportPath = path
        self.assertEqual(context.reportPath, path)

        # Heap churn — pre-fix this could clobber the dangling pointer.
        _ = ["x" * 64 for _ in range(1000)]
        self.assertEqual(context.reportPath, path)

        # Reassignment must release the prior buffer (no leak/double-free) and store the new one.
        context.reportPath = path + ".new"
        self.assertEqual(context.reportPath, path + ".new")

        # None clears.
        context.reportPath = None
        self.assertIsNone(context.reportPath)

    def test_executionContext_reportPath_survives_executeOperation(self):
        """Regression: with generateReport=1, Operation::execute reads
        context.reportPath as the report file path. Pre-fix ~80% of runs across
        common operations either failed silently (no file created) or wrote the
        report to a random heap-resident path."""
        so_core = SceneOptimizerCore.getInstance()

        # Run multiple iterations; pre-fix, this would fail probabilistically.
        for i in range(10):
            with tempfile.TemporaryDirectory(prefix="so_report_regression_") as tmpdir:
                stage = Usd.Stage.CreateInMemory()
                target = os.path.join(tmpdir, f"report_{i}.txt")

                context = ExecutionContext()
                context.set_stage(stage)
                context.generateReport = 1
                context.captureStats = 1
                context.reportPath = target

                result = so_core.executeOperation("merge", context, {})
                self.assertTrue(result[0], f"iter {i}: op failed: {result[1]!r}")

                # The path must round-trip unchanged after execute.
                self.assertEqual(
                    context.reportPath, target, f"iter {i}: reportPath was clobbered during executeOperation"
                )
                # The report file must exist at the user's intended path.
                self.assertTrue(os.path.exists(target), f"iter {i}: report file not created at requested path {target}")

                context.remove_stage()

    def test_sceneOptimizerCore(self):
        """
        Test getting and executing operations on SceneOptimizerCore.
        """
        # create a new scene optimizer core and check that a non-zero number of operations have loaded
        so_core = SceneOptimizerCore.getInstance()
        self.assertTrue(len(so_core.getOperations()) > 0)

        # now test executing operation, we won't check the results of the operation here since this is done by other
        # tests, just that it executes without error
        stage = Usd.Stage.CreateInMemory()
        context = ExecutionContext()
        context.set_stage(stage)
        result = so_core.executeOperation("merge", context, {})

        # check operation result
        self.assertTrue(result[0])
        self.assertIsNone(result[1])
        self.assertIsNone(result[2])

        context.remove_stage()

    def test_operation(self):
        """
        Test defining our own Operation in Python and executing it via the SceneOptimizerCore.
        """
        # pass
        so_core = SceneOptimizerCore.getInstance()

        # load plugins from this directory - this will pick up the test operation
        so_core.loadPluginsFromPath(os.path.dirname(os.path.realpath(__file__)))

        # because Scene Optimizer has loaded the test operation by appending it to the python path, we can now import it
        import test_operation

        test_operation.EXECUTED = False

        # the global variable should be false before execution
        self.assertFalse(test_operation.EXECUTED)

        prim_name = "testPrim"

        # execute out test operation
        stage = Usd.Stage.CreateInMemory()
        context = ExecutionContext()
        context.set_stage(stage)
        result = so_core.executeOperation("testOperation", context, {"primName": prim_name})

        # check operation result
        self.assertTrue(result[0])
        self.assertIsNone(result[1])
        self.assertIsNone(result[2])

        # the global variable should be true after execution
        self.assertTrue(test_operation.EXECUTED)

        # check the operation has created the test prim based on the argument
        self.assertTrue(stage.GetPrimAtPath(f"/{prim_name}"))

        context.remove_stage()
