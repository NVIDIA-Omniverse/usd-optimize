# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import importlib.machinery
import os
import subprocess
import sys
import tempfile
import textwrap
import unittest
import warnings

from usd_optimize.impl.core import _EXTENSION_STEM, _abi_error, _check_python_abi


def _fake_extension(directory, suffix):
    open(os.path.join(directory, _EXTENSION_STEM + suffix), "w").close()


class TestPythonAbiGuard(unittest.TestCase):

    def test_matching_abi_is_accepted(self):
        expected = importlib.machinery.EXTENSION_SUFFIXES[0]
        self.assertIsNone(_abi_error([expected], expected))
        self.assertIsNone(_abi_error([expected, ".so"], expected))

    def test_mismatched_abi_is_reported(self):
        reason = _abi_error([".cpython-310-x86_64-linux-gnu.so"], ".cpython-312-x86_64-linux-gnu.so")
        self.assertIn("cpython-310", reason)
        self.assertIn("cpython-312", reason)

    def test_degenerate_cases_do_not_pass_silently(self):
        self.assertEqual(_abi_error([], ".cp312-win_amd64.pyd"), "no extension module found")
        self.assertIn("no ABI tag", _abi_error([".so"], ".cpython-312-x86_64-linux-gnu.so"))
        self.assertIn("no ABI tag", _abi_error([".pyd"], ".cp312-win_amd64.pyd"))

    def test_guard_is_silent_on_a_correct_install(self):
        # warnings-as-errors: one that warned on every correct import would be worse than none.
        with warnings.catch_warnings():
            warnings.simplefilter("error")
            _check_python_abi()

    # The rest drive _check_python_abi() itself, so discovery, suffix extraction and
    # the raise-vs-warn routing are covered rather than just the decision.

    def test_matching_suffix_on_disk_is_silent(self):
        with tempfile.TemporaryDirectory() as directory:
            _fake_extension(directory, importlib.machinery.EXTENSION_SUFFIXES[0])
            with warnings.catch_warnings():
                warnings.simplefilter("error")
                _check_python_abi(directory)

    def test_mismatch_on_disk_raises(self):
        with tempfile.TemporaryDirectory() as directory:
            _fake_extension(directory, ".cpython-310-x86_64-linux-gnu.so")
            with self.assertRaises(ImportError) as caught:
                _check_python_abi(directory)
            self.assertIn("cpython-310", str(caught.exception))

    def test_untagged_on_disk_warns(self):
        with tempfile.TemporaryDirectory() as directory:
            _fake_extension(directory, ".so")
            with self.assertWarns(ImportWarning):
                _check_python_abi(directory)

    def test_absent_on_disk_warns(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertWarns(ImportWarning):
                _check_python_abi(directory)

    def test_guard_runs_on_import(self):
        # Covers the module-scope call itself: the cases above would all still pass
        # if that line were deleted. Run in a subprocess so a half-initialised
        # module cannot leak into the rest of the suite.
        source = textwrap.dedent("""
            import glob, os
            real = glob.glob
            glob.glob = lambda p: (
                [os.path.join(os.path.dirname(p), "{stem}.cpython-310-x86_64-linux-gnu.so")]
                if "{stem}" in p else real(p)
            )
            import usd_optimize.impl.core
            """).format(stem=_EXTENSION_STEM)
        done = subprocess.run([sys.executable, "-c", source], capture_output=True, text=True, timeout=60)
        self.assertNotEqual(done.returncode, 0, done.stdout)
        self.assertIn("cpython-310", done.stderr)


if __name__ == "__main__":
    unittest.main()
