# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Test utilities for the python test suite."""

import asyncio
import functools
import json
import logging
import pathlib
import time
import unittest

from pxr import Sdf, Usd, UsdGeom, UsdUtils
from usd_optimize.core import ExecutionContext, UsdOptimizeCore

logger = logging.getLogger(__name__)


def _get_test_data_file_path(name):
    """Get the path for a file within the test data directory."""
    script_dir = pathlib.Path(__file__).resolve().parent
    test_data_path = script_dir / ".." / "data"
    test_file_path = test_data_path / name
    return str(test_file_path.resolve())


def _get_context(stage, analysis=False, report=False, verbose=False):
    """Get an execution context."""
    context = ExecutionContext()
    context.usdStageId = UsdUtils.StageCache.Get().Insert(stage).ToLongInt()

    if verbose:
        context.verbose = 1

    if analysis:
        context.analysisMode = 1

    if report:
        context.generateReport = 1

    return context


def _get_meshes(stage):
    """Return all meshes in the stage."""
    return [x for x in stage.TraverseAll() if x.IsA(UsdGeom.Mesh)]


def _get_instances(stage):
    """Return all instances in the stage."""
    return [x for x in stage.Traverse() if x.IsInstance()]


class _AsyncToSyncMeta(type):
    """Metaclass that wraps async test/setUp/tearDown methods so they run synchronously."""

    _WRAP_PREFIXES = ("test",)
    _WRAP_EXACT = ("setUp", "tearDown", "asyncSetUp", "asyncTearDown")

    def __new__(mcs, name, bases, namespace):
        for attr_name, attr_value in list(namespace.items()):
            if asyncio.iscoroutinefunction(attr_value):
                if attr_name.startswith(mcs._WRAP_PREFIXES) or attr_name in mcs._WRAP_EXACT:
                    namespace[attr_name] = mcs._make_sync(attr_value)
        return super().__new__(mcs, name, bases, namespace)

    @staticmethod
    def _make_sync(coro_fn):
        @functools.wraps(coro_fn)
        def wrapper(*args, **kwargs):
            loop = asyncio.new_event_loop()
            try:
                return loop.run_until_complete(coro_fn(*args, **kwargs))
            finally:
                loop.close()

        return wrapper


class _CombinedMeta(_AsyncToSyncMeta, type(unittest.TestCase)):
    """Resolve metaclass conflict between _AsyncToSyncMeta and ABCMeta (used by TestCase)."""

    pass


class _AwaitableNone:
    """Trivially awaitable object that returns None.

    Allows sync setUp/tearDown to be ``await``-ed from an async subclass
    override (e.g. ``await super().setUp()``).
    """

    def __await__(self):
        return iter(())


def _execute_operation(operation, args, context):
    """Execute a named Usd Optimize operation using UsdOptimizeCore directly.

    Returns ``(success, result_tuple)`` where
    ``result_tuple = (success, error_or_none, extra_or_none)``.

    The outer ``success`` is the operation's own success, ``result_tuple[0]``,
    so ``self.assertTrue(success)`` fails when the operation fails.
    """
    so_core = UsdOptimizeCore.getInstance()
    result = so_core.executeOperation(operation, context, args)
    return bool(result[0]), result


class Test_Operation(unittest.TestCase, metaclass=_CombinedMeta):
    """Base class for operation tests."""

    OPERATION = "Undefined"

    # Python 3.12 removed deprecated assertEquals; restore it for tests
    assertEquals = unittest.TestCase.assertEqual

    def setUp(self):
        self._start_time = time.time()
        self._current_stage = None
        return _AwaitableNone()

    def tearDown(self):
        self._current_stage = None
        cache = UsdUtils.StageCache.Get()
        for stage in cache.GetAllStages():
            cache.Erase(cache.GetId(stage))
        logger.debug("Elapsed time: {:.3f}".format(time.time() - self._start_time))
        return _AwaitableNone()

    def _open_stage(self, name, loadSet=Usd.Stage.LoadAll):
        """Open a stage from the test data directory and track it as the
        current stage (used by _execute_command when no context is given).

        Uses Sdf.Layer.FindOrOpen + Reload to guarantee a fresh layer even
        when a previous test opened the same file, while keeping the real
        file path so that USD references resolve correctly.

        Pass ``loadSet=Usd.Stage.LoadNone`` to leave payloads unloaded.
        """
        file_path = _get_test_data_file_path(name)
        layer = Sdf.Layer.FindOrOpen(file_path)
        if layer:
            layer.Reload()
        self.assertIsNotNone(layer, f"Failed to open layer: {file_path}")
        stage = Usd.Stage.Open(layer, loadSet)
        self.assertIsNotNone(stage)
        self._current_stage = stage
        return stage

    def _open_stage_unloaded(self, name):
        """Open a stage from the test data directory with all payloads left unloaded."""
        return self._open_stage(name, loadSet=Usd.Stage.LoadNone)

    def _execute_command(self, args, context=None):
        """Execute the operation using UsdOptimizeCore directly.

        When *context* is ``None``, a default context is created from the
        stage most recently returned by ``_open_stage``.  If no stage has
        been opened, an empty anonymous stage is created as a fallback.

        Returns ``(success, result_tuple)`` where
        ``result_tuple = (success, error_or_none, extra_or_none)``; the outer
        ``success`` is the operation's own success, ``result_tuple[0]``.
        """
        if self.OPERATION == "Undefined":
            raise RuntimeError("OPERATION not set — subclass must define OPERATION")
        if context is None:
            stage = self._current_stage
            if stage is None:
                stage = Usd.Stage.CreateInMemory()
                self._current_stage = stage
            context = _get_context(stage)
        return _execute_operation(self.OPERATION, args, context)

    def _execute_json_string(self, stage, json_str):
        """Execute the operations described in a JSON string on the given stage and assert success."""
        context = _get_context(stage)
        return UsdOptimizeCore.getInstance().executeConfig(context, json.loads(json_str))

    def _execute_json(self, stage, name):
        """Execute the operations described in a JSON file on the given stage and assert success."""
        with open(_get_test_data_file_path(name)) as f:
            context = _get_context(stage)
            status = UsdOptimizeCore.getInstance().executeConfig(context, json.load(f))
            self.assertTrue(status)
            return status
        return None
