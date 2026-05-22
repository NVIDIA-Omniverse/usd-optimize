# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Custom test runner that handles relative imports in the test.python
directory (whose dotted name makes it incompatible with regular unittest
discover -t).

Usage: python run_discover.py <test_dir>
"""

import importlib.util
import os
import sys
import types
import unittest

_PKG_NAME = "test_python"

# Tests that require infrastructure not available in this build
# (e.g. ISceneOptimizer carb interface, command framework).
_STANDALONE_SKIP_TESTS = {
    "test_operation_misc.Test.test_pathResolver",
    "test_operation_misc.Test.test_base_command",
    "test_operation_misc.Test.test_JsonParserAllCommands",
}


def _create_package(test_dir):
    """Register a synthetic package backed by *test_dir* so that relative
    imports (``from .test_utils import ...``) resolve correctly."""
    pkg = types.ModuleType(_PKG_NAME)
    pkg.__path__ = [test_dir]
    pkg.__file__ = os.path.join(test_dir, "__init__.py")
    pkg.__package__ = _PKG_NAME
    sys.modules[_PKG_NAME] = pkg
    return pkg


def _import_module(test_dir, filename):
    """Import *filename* as a sub-module of the synthetic package."""
    module_name = filename[:-3]
    fqn = f"{_PKG_NAME}.{module_name}"
    spec = importlib.util.spec_from_file_location(
        fqn,
        os.path.join(test_dir, filename),
        submodule_search_locations=[],
    )
    module = importlib.util.module_from_spec(spec)
    module.__package__ = _PKG_NAME
    sys.modules[fqn] = module
    spec.loader.exec_module(module)
    return module


def _test_short_name(test):
    """Return 'module.Class.method' without the synthetic package prefix."""
    fqn = test.id()
    prefix = _PKG_NAME + "."
    return fqn[len(prefix) :] if fqn.startswith(prefix) else fqn


def _filter_skip_tests(suite, skip_set):
    """Remove tests listed in *skip_set* from the suite."""
    filtered = unittest.TestSuite()
    for test_group in suite:
        if isinstance(test_group, unittest.TestSuite):
            filtered.addTest(_filter_skip_tests(test_group, skip_set))
        else:
            if _test_short_name(test_group) not in skip_set:
                filtered.addTest(test_group)
    return filtered


def main():
    test_dir = sys.argv[1] if len(sys.argv) > 1 else "."

    _create_package(test_dir)

    # Pre-import test_utils so it's available for relative imports
    _import_module(test_dir, "test_utils.py")

    loader = unittest.TestLoader()
    suite = unittest.TestSuite()
    errors = []

    for filename in sorted(os.listdir(test_dir)):
        if not filename.startswith("test_") or not filename.endswith(".py"):
            continue
        module_name = filename[:-3]
        if filename == "test_utils.py":
            continue
        try:
            module = _import_module(test_dir, filename)
            suite.addTests(loader.loadTestsFromModule(module))
        except Exception as e:
            errors.append((module_name, e))

    if errors:
        print(f"\n--- {len(errors)} module(s) failed to import ---", file=sys.stderr)
        for name, exc in errors:
            print(f"  {name}: {exc}", file=sys.stderr)
        print("", file=sys.stderr)
        sys.exit(1)

    skip_set = _STANDALONE_SKIP_TESTS
    suite = _filter_skip_tests(suite, skip_set)

    runner = unittest.TextTestRunner(verbosity=2)
    result = runner.run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
