# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Unit tests for the validators wrapper driver (``tools/validators/validators.py``).

The driver reads upstream's ``--verbose``/``-v`` as a per-prim toggle (without
consuming it) and adds a non-destructive, opt-in ``--fix`` (new file by default;
``--fix-in-place`` overwrites the source) on top of the upstream
``usd_validation_nvidia`` CLI. These tests stub the heavy imports
``main()`` performs, so they run with or without a build and never invoke real
validation.
"""

import importlib.util
import io
import os
import shutil
import stat
import sys
import tempfile
import types
import unittest
from contextlib import redirect_stderr, redirect_stdout
from unittest import mock


def _load_driver():
    """Import tools/validators/validators.py, or return None if not found.

    Walks up from this file so it works from a source checkout and from the
    extracted test package (different depth; ``tools/`` not shipped, but in CI
    the source checkout is still an ancestor of the extraction dir).
    """
    d = os.path.dirname(os.path.abspath(__file__))
    while True:
        candidate = os.path.join(d, "tools", "validators", "validators.py")
        if os.path.isfile(candidate):
            spec = importlib.util.spec_from_file_location("usd_optimize_validators_driver", candidate)
            module = importlib.util.module_from_spec(spec)
            spec.loader.exec_module(module)
            return module
        if os.path.dirname(d) == d:
            return None
        d = os.path.dirname(d)


_driver = _load_driver()

# (argv, expected stderr substring) for guards that fail with exit 2 up front,
# before any filesystem work — no real asset needed.
_GUARD_CASES = [
    (["a.usd", "--fix-output", "out.usd"], "--fix-output requires --fix"),
    (["a.usd", "--fix-in-place"], "--fix-in-place requires --fix"),
    (["a.usd", "--fix", "--fix-output", "out.usd", "--fix-in-place"], "cannot be combined"),
    (["a.usd", "--fix", "--fix-output"], "requires a path argument"),
    (["--fix", "-r", "SomeRule"], "no .usd"),
    (["a.usd", "b.usd", "--fix"], "single source asset"),
    (["a.usd", "--fix-overwrite"], "--fix-overwrite requires --fix"),
    (["a.usd", "--fix", "--fix-overwrite", "--fix-in-place"], "--fix-overwrite cannot be combined"),
]


class Test(unittest.TestCase):
    def setUp(self):
        if _driver is None:
            self.skipTest("tools/validators/validators.py not found (packaged run without source tree)")
        self.calls = []  # sys.argv recorded for each cli_main() call
        self.verbose = []  # args recorded for each set_verbose() call

        def cli_main():
            self.calls.append(list(sys.argv))
            return 0

        uo = types.ModuleType("usd_optimize")
        uo.validators = types.ModuleType("usd_optimize.validators")
        uo.validators.register_all = lambda: None
        uo.validators.set_verbose = self.verbose.append
        uvn = types.ModuleType("usd_validation_nvidia")
        uvn.cli_main = cli_main

        # Stub main()'s imports, snapshot env/argv; addCleanup restores all three.
        for patcher in (
            mock.patch.dict(
                sys.modules,
                {"usd_validation_nvidia": uvn, "usd_optimize": uo, "usd_optimize.validators": uo.validators},
            ),
            mock.patch.dict(os.environ),
            mock.patch.object(sys, "argv", list(sys.argv)),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)
        os.environ.pop("USD_OPTIMIZE_FIX_IN_PLACE", None)

        self._tmp = tempfile.mkdtemp()
        self.addCleanup(shutil.rmtree, self._tmp, ignore_errors=True)

    def _run(self, args):
        sys.argv = ["validators.py", *args]
        out, err = io.StringIO(), io.StringIO()
        with redirect_stdout(out), redirect_stderr(err):
            rc = _driver.main()
        return rc, out.getvalue(), err.getvalue()

    def _asset(self, name="scene.usd", content=b"#usda 1.0\n"):
        path = os.path.join(self._tmp, name)
        with open(path, "wb") as f:
            f.write(content)
        return path

    # ---- helpers ------------------------------------------------------------

    def test_find_assets_skips_flag_values(self):
        self.assertEqual(_driver._find_assets(["--csv-output", "o.csv", "scene.usd", "--fix"]), [(2, "scene.usd")])
        self.assertEqual(_driver._find_assets(["--group-by", "layout.usda", "real.usd"]), [(2, "real.usd")])

    def test_find_assets_unknown_flag_value_is_ambiguous(self):
        # An unknown future flag taking a .usd value surfaces as a 2nd candidate.
        self.assertEqual(
            [t for _, t in _driver._find_assets(["--future", "other.usd", "real.usd"])], ["other.usd", "real.usd"]
        )

    def test_default_fixed_path(self):
        self.assertEqual(_driver._default_fixed_path("/a/b/scene.usd"), "/a/b/scene.fixed.usd")
        self.assertEqual(_driver._default_fixed_path("scene.usda"), "scene.fixed.usda")

    # ---- guards (fail loud, exit 2, never call cli_main) --------------------

    def test_flag_guards_exit_2(self):
        for args, expect in _GUARD_CASES:
            with self.subTest(args=args):
                rc, _, err = self._run(args)
                self.assertEqual(rc, 2)
                self.assertIn(expect, err)
                self.assertEqual(self.calls, [])

    def test_fix_output_resolving_to_source_rejected(self):
        # Both an explicit same-path and the source's own directory resolve to
        # the source file, which would overwrite it: reject, don't overwrite.
        asset = self._asset()
        outs = [asset, self._tmp]
        link = os.path.join(self._tmp, "link.usd")  # symlink to the source (realpath resolves it)
        try:
            os.symlink(asset, link)
            outs.append(link)
        except OSError:
            pass  # symlinks unsupported (e.g. Windows without privilege) — skip that case
        for out in outs:
            with self.subTest(out=out):
                self.calls.clear()
                rc, _, err = self._run([asset, "--fix", "--fix-output", out])
                self.assertEqual(rc, 2)
                self.assertIn("resolves to the source", err)
                self.assertEqual(self.calls, [])
        self.assertTrue(os.path.exists(asset))

    def test_existing_destination_not_clobbered(self):
        # The copy is made before validation runs, so without this guard even a run that
        # finds nothing to fix destroys the file. --fix-overwrite opts back in.
        asset = self._asset()
        out = os.path.join(self._tmp, "scene.fixed.usd")
        with open(out, "wb") as f:
            f.write(b"sentinel\n")

        rc, _, err = self._run([asset, "--fix"])
        self.assertEqual(rc, 2)
        self.assertIn("already exists", err)
        self.assertEqual(self.calls, [])
        with open(out, "rb") as f:
            self.assertEqual(f.read(), b"sentinel\n")

        rc, _, _ = self._run([asset, "--fix", "--fix-overwrite"])
        self.assertEqual(rc, 0)
        self.assertIn(out, self.calls[0])
        with open(out, "rb") as f:
            self.assertNotEqual(f.read(), b"sentinel\n")

    def test_overwrite_rejected_when_in_place_comes_from_env(self):
        # The guard reads the resolved mode: via the env var there is still no destination
        # file, and silently ignoring --fix-overwrite while mutating the source is the
        # failure being guarded against.
        asset = self._asset()
        os.environ["USD_OPTIMIZE_FIX_IN_PLACE"] = "1"
        rc, _, err = self._run([asset, "--fix", "--fix-overwrite"])
        self.assertEqual(rc, 2)
        self.assertIn("--fix-overwrite cannot be combined", err)
        self.assertEqual(self.calls, [])

    def test_missing_source_named_as_missing(self):
        # os.access is False for a missing path too, so the writability check must not
        # tell someone with a typo to go change permissions.
        missing = os.path.join(self._tmp, "nope.usd")
        rc, _, err = self._run([missing, "--fix"])
        self.assertEqual(rc, 2)
        self.assertIn("does not exist", err)
        self.assertNotIn("read-only", err)
        self.assertEqual(self.calls, [])

    def test_failed_copy_leaves_no_placeholder(self):
        # The destination is reserved before the copy; a failed copy must not leave it
        # behind, or the re-run hits "already exists".
        asset = self._asset()
        out = os.path.join(self._tmp, "scene.fixed.usd")
        with mock.patch.object(_driver.shutil, "copy2", side_effect=OSError("disk full")):
            with self.assertRaises(OSError):
                self._run([asset, "--fix"])
        self.assertFalse(os.path.exists(out))

    def test_failed_run_removes_the_unfixed_copy(self):
        # A run that dies after the copy must not leave an unfixed duplicate behind.
        asset = self._asset()
        out = os.path.join(self._tmp, "scene.fixed.usd")
        seen = []

        def boom():
            seen.append(os.path.exists(out))
            raise RuntimeError("Failed to open layer")

        sys.modules["usd_validation_nvidia"].cli_main = boom
        with self.assertRaises(RuntimeError):
            self._run([asset, "--fix"])
        self.assertEqual(seen, [True])  # the copy was there during the run
        self.assertFalse(os.path.exists(out))  # and was unwound when it failed
        self.assertTrue(os.path.exists(asset))  # source never at risk

    def test_issues_found_keeps_the_fixed_output(self):
        # "Issues found" is sys.exit(1) from a completed run; its output must survive.
        asset = self._asset()
        out = os.path.join(self._tmp, "scene.fixed.usd")

        def issues_found():
            sys.exit(1)

        sys.modules["usd_validation_nvidia"].cli_main = issues_found
        with self.assertRaises(SystemExit):
            self._run([asset, "--fix"])
        self.assertTrue(os.path.exists(out))

    def test_read_only_source_rejected(self):
        # copy2 preserves the mode, so the copy would be unwritable and upstream would
        # report the fix as applied while saving nothing.
        asset = self._asset()
        os.chmod(asset, stat.S_IREAD)
        if os.access(asset, os.W_OK):  # e.g. running as root, where the mode is advisory
            os.chmod(asset, stat.S_IREAD | stat.S_IWRITE)
            self.skipTest("cannot make a file unwritable for this user")
        try:
            rc, _, err = self._run([asset, "--fix"])
        finally:
            os.chmod(asset, stat.S_IREAD | stat.S_IWRITE)
        self.assertEqual(rc, 2)
        self.assertIn("read-only", err)
        self.assertEqual(self.calls, [])
        self.assertFalse(os.path.exists(os.path.join(self._tmp, "scene.fixed.usd")))

    # ---- happy paths --------------------------------------------------------

    def test_read_only_default(self):
        asset = self._asset()
        rc, _, _ = self._run([asset])
        self.assertEqual(rc, 0)
        self.assertEqual(self.calls, [["validators.py", asset]])  # unchanged, no fix flags

    def test_fix_writes_to_new_file_and_redirects(self):
        asset = self._asset("scene.usd", content=b"#usda 1.0\n(orig)\n")
        outdir = os.path.join(self._tmp, "outdir")
        os.mkdir(outdir)
        # (extra args, expected output file, env) — default, explicit path,
        # existing dir, and explicit path overriding an env in-place default.
        cases = [
            ([], os.path.join(self._tmp, "scene.fixed.usd"), {}),
            (["--fix-output", os.path.join(self._tmp, "sub", "f.usd")], os.path.join(self._tmp, "sub", "f.usd"), {}),
            (["--fix-output", outdir], os.path.join(outdir, "scene.usd"), {}),
            (
                ["--fix-output", os.path.join(self._tmp, "ov.usd")],
                os.path.join(self._tmp, "ov.usd"),
                {"USD_OPTIMIZE_FIX_IN_PLACE": "1"},
            ),
        ]
        for extra, expected, env in cases:
            with self.subTest(extra=extra):
                self.calls.clear()
                os.environ.pop("USD_OPTIMIZE_FIX_IN_PLACE", None)
                os.environ.update(env)
                rc, out, _ = self._run([asset, "--fix", *extra])
                self.assertEqual(rc, 0)
                self.assertTrue(os.path.isfile(expected))  # written to the expected file
                self.assertIn(expected, self.calls[0])  # upstream redirected at the copy
                self.assertNotIn(asset, self.calls[0])
                self.assertIn("writing fixes to a new file", out)
        with open(asset, "rb") as f:
            self.assertEqual(f.read(), b"#usda 1.0\n(orig)\n")  # source preserved throughout

    def test_in_place_targets_source(self):
        # Both --fix-in-place and the env var mutate the source (no copy made).
        for extra, env in (["--fix-in-place"], {}), ([], {"USD_OPTIMIZE_FIX_IN_PLACE": "1"}):
            with self.subTest(env=env):
                self.calls.clear()
                os.environ.pop("USD_OPTIMIZE_FIX_IN_PLACE", None)
                os.environ.update(env)
                asset = self._asset()
                rc, out, _ = self._run([asset, "--fix", *extra])
                self.assertEqual(rc, 0)
                self.assertFalse(os.path.exists(os.path.join(self._tmp, "scene.fixed.usd")))
                self.assertIn(asset, self.calls[0])

    def test_verbose_sets_flag_and_reaches_upstream(self):
        # Every spelling of upstream's verbosity flag turns on per-prim emission,
        # and all of them still reach upstream so its log level rises too.
        for flag in ("--verbose", "-v", "-vv"):
            with self.subTest(flag=flag):
                self.verbose.clear()
                self.calls.clear()
                rc, _, _ = self._run([self._asset(), flag])
                self.assertEqual(rc, 0)
                self.assertEqual(self.verbose, [True])
                self.assertIn(flag, self.calls[0])

    def test_non_verbose_flags_do_not_set_verbose(self):
        rc, _, _ = self._run([self._asset(), "-p", "IsFailure"])
        self.assertEqual(rc, 0)
        self.assertEqual(self.verbose, [])


if __name__ == "__main__":
    unittest.main()
