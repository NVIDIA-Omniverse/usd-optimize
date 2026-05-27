# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Custom test runner that handles relative imports in the test.python
directory (whose dotted name makes it incompatible with regular unittest
discover -t).

Usage:
    python run_discover.py <test_dir> [options] [test_spec ...]

When run with only <test_dir>, every test in the directory is discovered
and executed (the default suite behavior).

When one or more test_spec arguments are given, only matching tests run.
A test_spec may be:
    module                          run all tests in the module
    module.ClassName                run all tests in the class
    module.ClassName.method         run a single test
The trailing ".py" on a module name is accepted and stripped.

Options:
    -k PATTERN      Only run tests whose dotted short name
                    (module.ClassName.method) contains PATTERN.
                    May be combined with positional test_spec args.
    -q              Quiet output (the default is verbose).
    -h / --help     Show this help text.
"""

import argparse
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
    # unittest.TestLoader.loadTestsFromName resolves dotted names by
    # getattr-walking from the parent package, so the submodule must be
    # attached to the synthetic package — not just registered in sys.modules.
    setattr(sys.modules[_PKG_NAME], module_name, module)
    spec.loader.exec_module(module)
    return module


def _test_short_name(test):
    """Return 'module.Class.method' without the synthetic package prefix."""
    fqn = test.id()
    prefix = _PKG_NAME + "."
    return fqn[len(prefix) :] if fqn.startswith(prefix) else fqn


def _iter_tests(suite):
    """Yield individual test cases from an arbitrarily nested suite."""
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from _iter_tests(item)
        else:
            yield item


def _filter_suite(suite, predicate):
    """Return a flat suite containing tests for which predicate(test) is True."""
    filtered = unittest.TestSuite()
    for test in _iter_tests(suite):
        if predicate(test):
            filtered.addTest(test)
    return filtered


def _normalize_spec(spec):
    """Strip an optional ".py" suffix from the module portion of a spec."""
    parts = spec.split(".")
    # Detect "module.py" or "module.py.Class[.method]" and drop the "py" segment.
    if len(parts) >= 2 and parts[1] == "py":
        parts = [parts[0]] + parts[2:]
    return ".".join(parts)


def _spec_module(spec):
    """Return the module name (first dotted segment) of a normalized spec."""
    return spec.split(".", 1)[0]


def _discover_all(test_dir, loader):
    """Discover every test module in test_dir. Returns (suite, errors)."""
    suite = unittest.TestSuite()
    errors = []
    for filename in sorted(os.listdir(test_dir)):
        if not filename.startswith("test_") or not filename.endswith(".py"):
            continue
        if filename == "test_utils.py":
            continue
        module_name = filename[:-3]
        try:
            module = _import_module(test_dir, filename)
            suite.addTests(loader.loadTestsFromModule(module))
        except Exception as e:
            errors.append((module_name, e))
    return suite, errors


def _load_specs(test_dir, loader, specs):
    """Load tests matching the user-supplied specs. Returns (suite, errors)."""
    suite = unittest.TestSuite()
    errors = []

    # Import only the modules referenced by the specs (faster than full
    # discovery and avoids unrelated import-time failures).
    wanted_modules = {_spec_module(s) for s in specs}
    failed_modules = set()
    for module_name in sorted(wanted_modules):
        filename = module_name + ".py"
        try:
            _import_module(test_dir, filename)
        except FileNotFoundError:
            errors.append((module_name, FileNotFoundError(f"no such test module: {filename}")))
            failed_modules.add(module_name)
        except Exception as e:
            errors.append((module_name, e))
            failed_modules.add(module_name)

    for spec in specs:
        if _spec_module(spec) in failed_modules:
            continue
        fqn = f"{_PKG_NAME}.{spec}"
        try:
            suite.addTests(loader.loadTestsFromName(fqn))
        except Exception as e:
            errors.append((spec, e))

    return suite, errors


def _parse_args(argv):
    parser = argparse.ArgumentParser(
        prog="run_discover.py",
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    parser.add_argument("test_dir", help="Directory containing test_*.py files")
    parser.add_argument("test_specs", nargs="*", help="Optional module / module.Class / module.Class.method specs")
    parser.add_argument("-k", dest="pattern", default=None, help="Only run tests whose short name contains PATTERN")
    parser.add_argument(
        "-q", dest="verbosity", action="store_const", const=1, default=2, help="Quiet output (default is verbose)"
    )
    # parse_intermixed_args lets users place -k either before or after the
    # positional test_spec args. Plain parse_args refuses `-k PAT spec`
    # because it greedily fills test_specs (nargs="*") with [] before -k.
    return parser.parse_intermixed_args(argv)


def main(argv=None):
    args = _parse_args(sys.argv[1:] if argv is None else argv)

    _create_package(args.test_dir)

    # Pre-import test_utils so it's available for relative imports.
    _import_module(args.test_dir, "test_utils.py")

    loader = unittest.TestLoader()

    specs = [_normalize_spec(s) for s in args.test_specs]
    if specs:
        suite, errors = _load_specs(args.test_dir, loader, specs)
    else:
        suite, errors = _discover_all(args.test_dir, loader)

    if errors:
        print(f"\n--- {len(errors)} module(s)/spec(s) failed to load ---", file=sys.stderr)
        for name, exc in errors:
            print(f"  {name}: {exc}", file=sys.stderr)
        print("", file=sys.stderr)
        sys.exit(1)

    # Apply the standalone skip list. In spec mode, any test currently in the
    # loaded suite that lands in the skip set was pulled in by a user-supplied
    # spec (whether the spec named the method, the class, or the module), so
    # surface that explicitly instead of silently dropping it.
    skip_set = _STANDALONE_SKIP_TESTS
    requested_but_skipped = set()
    if specs:
        requested_but_skipped = {name for name in (_test_short_name(t) for t in _iter_tests(suite)) if name in skip_set}
    if requested_but_skipped:
        print(
            "--- the following requested tests are unsupported in this " "standalone build and were skipped ---",
            file=sys.stderr,
        )
        for name in sorted(requested_but_skipped):
            print(f"  {name}", file=sys.stderr)
        print("", file=sys.stderr)

    suite = _filter_suite(suite, lambda t: _test_short_name(t) not in skip_set)

    if args.pattern is not None:
        pat = args.pattern
        suite = _filter_suite(suite, lambda t: pat in _test_short_name(t))

    # Catch the easy mistake of a filter that matched nothing (e.g. typo).
    had_filter = bool(specs) or args.pattern is not None
    if had_filter and suite.countTestCases() == 0 and not requested_but_skipped:
        print("--- no tests matched the given filter(s) ---", file=sys.stderr)
        for spec in specs:
            print(f"  spec: {spec}", file=sys.stderr)
        if args.pattern is not None:
            print(f"  pattern: -k {args.pattern}", file=sys.stderr)
        sys.exit(1)

    runner = unittest.TextTestRunner(verbosity=args.verbosity)
    result = runner.run(suite)
    sys.exit(0 if result.wasSuccessful() else 1)


if __name__ == "__main__":
    main()
