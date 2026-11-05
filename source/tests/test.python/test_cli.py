# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Black-box tests for the ``usdOptimize`` command-line tool.

The CLI is a C++ console application (``source/cli/``) that wraps the core
library.  These tests exercise it the way a user would: by spawning the built
``usdOptimize`` binary as a subprocess and asserting on its exit code, stdout
and stderr.  They are deliberately independent of the in-process Python
bindings used by the other tests in this suite.
"""

import os
import pathlib
import shutil
import subprocess
import tempfile
import unittest

from pxr import Usd, UsdGeom

from .test_utils import _get_test_data_file_path

# Input stage shared by most cases.
_INPUT_STAGE = "countVerts.usd"


def _find_cli_binary():
    """Locate the built ``usdOptimize`` executable."""
    exe = "usdOptimize.exe" if os.name == "nt" else "usdOptimize"

    candidates = []

    # we have to check using the LD_LIBRARY_PATH / PATH because this file can
    # be a symlink which means it can live in different places depending on the
    # build
    var = "PATH" if os.name == "nt" else "LD_LIBRARY_PATH"
    for entry in os.environ.get(var, "").split(os.pathsep):
        if not entry:
            continue
        p = pathlib.Path(entry)
        # The runner puts <build>/lib on the path; its sibling is <build>/bin.
        candidates.append(p.parent / "bin" / exe)

    seen = set()
    for c in candidates:
        cs = str(c)
        if cs in seen:
            continue
        seen.add(cs)
        if c.is_file() and os.access(cs, os.X_OK):
            return cs

    return None


_CLI = _find_cli_binary()


class Test_Cli(unittest.TestCase):
    """Exercise the ``usdOptimize`` CLI as a black box."""

    def setUp(self):
        self._tmp = tempfile.mkdtemp(prefix="usd_optimize_cli_")

    def tearDown(self):
        shutil.rmtree(self._tmp, ignore_errors=True)

    # -- helpers ----------------------------------------------------------

    def _run(self, *args):
        """Run the CLI with *args* and return the CompletedProcess.

        The environment is inherited so the subprocess picks up the
        LD_LIBRARY_PATH / PATH set up by the test runner.
        """
        result = subprocess.run(
            [_CLI, *args],
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            text=True,
        )
        return result

    def _stage_path(self, name=_INPUT_STAGE):
        return _get_test_data_file_path(name)

    def _tmp_path(self, name):
        return os.path.join(self._tmp, name)

    # -- help / usage -----------------------------------------------------

    def test_no_args_prints_help_and_fails(self):
        """With no arguments the CLI prints general help and exits non-zero."""
        result = self._run()
        self.assertEqual(result.returncode, 1)
        self.assertIn("Usage: usdOptimize", result.stdout)
        self.assertIn("Available Operations:", result.stdout)

    def test_help_flag(self):
        """``-h`` prints the general help (including the operation list)."""
        for flag in ("-h", "--help"):
            with self.subTest(flag=flag):
                result = self._run(flag)
                self.assertEqual(result.returncode, 0)
                self.assertIn("Usage: usdOptimize", result.stdout)
                self.assertIn("Required Args:", result.stdout)
                self.assertIn("Available Operations:", result.stdout)

    def test_help_for_operation(self):
        """``-h <operation>`` prints operation-specific help."""
        result = self._run("-h", "printStats")
        self.assertEqual(result.returncode, 0)
        # Display name and key both appear in the per-operation header.
        self.assertIn("(printStats) Help:", result.stdout)
        self.assertIn("Args:", result.stdout)
        # The footer carries a version + author line.
        self.assertIn("NVIDIA", result.stdout)

    def test_help_for_unknown_operation_falls_back_to_general_help(self):
        """``-h <not-an-op>`` falls through to the general help."""
        result = self._run("-h", "definitelyNotAnOperation")
        self.assertEqual(result.returncode, 0)
        self.assertIn("Available Operations:", result.stdout)
        self.assertIn("Unknown operation", result.stderr)

    def test_help_followed_by_flag_does_not_warn(self):
        """A flag after ``-h`` is another option, not a mistyped operation name."""
        for flag in ("-v", "-an"):
            with self.subTest(flag=flag):
                result = self._run("-h", flag)
                self.assertEqual(result.returncode, 0)
                self.assertIn("Available Operations:", result.stdout)
                self.assertNotIn("Unknown operation", result.stderr)

    # -- argument parsing errors -----------------------------------------

    def test_unknown_argument(self):
        """An unrecognized flag before other arguments is an error."""
        # The parser treats the final argument separately, so this case and
        # test_unknown_argument_in_final_position cover different branches.
        result = self._run("--bogus", "-i", self._stage_path())
        self.assertEqual(result.returncode, 1)
        self.assertIn("unknown argument", result.stderr)

    def test_unknown_argument_in_final_position(self):
        """A flag-shaped final argument is rejected, not taken as the input stage."""
        for flag in ("--bogus-flag", "--version"):
            with self.subTest(flag=flag):
                result = self._run(flag)
                self.assertEqual(result.returncode, 1)
                self.assertIn("unknown argument", result.stderr)

    def test_no_input_specified(self):
        """An operation with no input stage reports that no stage was given."""
        # The operation name is consumed by -o, so nothing is left to be
        # interpreted as the positional input.
        result = self._run("-o", "countVertices")
        self.assertEqual(result.returncode, 1)
        self.assertIn("No stage specified", result.stdout)

    def test_invalid_operation(self):
        """An unknown operation name is rejected."""
        result = self._run("-i", self._stage_path(), "-o", "notARealOperation")
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid operation specified", result.stderr)

    def test_python_operations_not_available_in_cli(self):
        """Python-plugin operations are intentionally unavailable from the native CLI.

        ``pythonScript``, ``deleteHiddenPrims``, ``removeUntypedPrims`` and
        ``moveMaterials`` are implemented as Python plugins
        (``source/operations/<name>/__init__.py``).
        The core only loads those plugin directories when a Python interpreter is
        initialized (the ``Py_IsInitialized()`` gate in ``Core.cpp``).  The
        standalone CLI binary never initializes Python, so these operations are
        not registered and must be rejected.  They remain available via the
        ``usd-optimize`` Python wheel / bindings.
        """
        for op in ("pythonScript", "deleteHiddenPrims", "removeUntypedPrims", "moveMaterials"):
            with self.subTest(operation=op):
                result = self._run("-i", self._stage_path(), "-o", op)
                self.assertEqual(result.returncode, 1)
                self.assertIn("invalid operation specified", result.stderr)

    def test_argument_without_operation(self):
        """``-a`` with no preceding ``-o`` has nothing to apply to."""
        result = self._run("-i", self._stage_path(), "-a", "high=5")
        self.assertEqual(result.returncode, 1)
        self.assertIn("no operations to add argument", result.stderr)

    def test_invalid_argument_for_operation(self):
        """An argument name the operation doesn't define is rejected."""
        result = self._run("-o", "countVertices", "-a", "notAnArgument=1", "-i", self._stage_path())
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid argument specified", result.stderr)

    # -- run behaviors ----------------------------------------------------

    def test_input_only_does_nothing(self):
        """An input stage with no operations is a successful no-op."""
        result = self._run("-i", self._stage_path())
        self.assertEqual(result.returncode, 0)
        self.assertIn("Nothing to do", result.stdout)

    def test_positional_input_is_accepted(self):
        """The final positional argument is treated as the input stage."""
        # countVertices is consumed by -o; the trailing path is the input.
        result = self._run("-an", "-o", "countVertices", self._stage_path())
        self.assertEqual(result.returncode, 0)

    def test_analysis_run_succeeds(self):
        """A real analysis-mode operation completes successfully."""
        result = self._run("-an", "-o", "countVertices", "-a", "high=40000", self._stage_path())
        self.assertEqual(result.returncode, 0)

    def test_config_file(self):
        """Operations can be supplied via a ``-c`` JSON config file."""
        config = self._tmp_path("config.json")
        with open(config, "w") as f:
            f.write('[{"operation": "printStats"}]')
        result = self._run("-c", config, self._stage_path())
        self.assertEqual(result.returncode, 0)

    def test_write_json_config(self):
        """``-j`` serializes the assembled operation config to JSON.

        This also exercises argument type coercion: ``high`` is an int
        argument, so the emitted value must be a JSON number, not a string.
        """
        out = self._tmp_path("emitted.json")
        result = self._run("-an", "-o", "countVertices", "-a", "high=12345", "-j", out, self._stage_path())
        self.assertEqual(result.returncode, 0)
        self.assertTrue(os.path.isfile(out))

        import json

        with open(out) as f:
            commands = json.load(f)
        self.assertEqual(len(commands), 1)
        self.assertEqual(commands[0]["operation"], "countVertices")
        # Coerced to an int, not the string "12345".
        self.assertEqual(commands[0]["high"], 12345)

    def test_bool_argument_values(self):
        """Bool arguments take the ``true``/``false`` that ``-h`` prints, in any case, as well as 1 and 0.

        NVBug 6429822: ``true`` and ``false`` used to abort with an internal ``stoi`` error.
        """
        import json

        cases = [("true", True), ("false", False), ("True", True), ("FALSE", False), ("1", True), ("0", False)]
        for value, expected in cases:
            with self.subTest(value=value):
                out = self._tmp_path(f"bool_{value}.json")
                result = self._run(
                    "-o", "deduplicateGeometry", "-a", f"considerDeepTransforms={value}", "-j", out, self._stage_path()
                )
                self.assertEqual(result.returncode, 0, result.stderr)
                with open(out) as f:
                    self.assertIs(json.load(f)[0]["considerDeepTransforms"], expected)

    def test_invalid_bool_argument_value(self):
        """A value that is not a boolean is rejected by argument name, not with an internal ``stoi`` error."""
        for value in ("maybe", "1maybe", "2"):
            with self.subTest(value=value):
                result = self._run(
                    "-o", "deduplicateGeometry", "-a", f"considerDeepTransforms={value}", self._stage_path()
                )
                self.assertEqual(result.returncode, 1)
                self.assertIn("considerDeepTransforms takes true, false, 1 or 0", result.stderr)
                self.assertNotIn("stoi", result.stderr)

        # An empty value is not the bare-name shortcut for true.
        result = self._run("-o", "deduplicateGeometry", "-a", "considerDeepTransforms=", self._stage_path())
        self.assertEqual(result.returncode, 1)
        self.assertIn("invalid argument value specified: considerDeepTransforms=", result.stderr)

    def test_invalid_paths_expression(self):
        """A ``paths`` value that isn't a path expression fails the run instead of writing an unchanged stage."""
        out = self._tmp_path("invalid_paths.usda")
        result = self._run("-o", "computeExtents", "-a", 'paths=["/World"]', "-w", out, self._stage_path())
        self.assertEqual(result.returncode, 1)
        self.assertFalse(os.path.exists(out))

    def test_write_output_stage(self):
        """``-w`` exports a usable output stage."""
        out = self._tmp_path("out.usda")
        result = self._run("-o", "countVertices", "-an", "-w", out, self._stage_path())
        self.assertEqual(result.returncode, 0)
        self.assertTrue(os.path.isfile(out))
        self.assertGreater(os.path.getsize(out), 0)
        # The exported file must be a valid, openable USD stage.
        stage = Usd.Stage.Open(out)
        self.assertIsNotNone(stage)

    def test_find_occluded_meshes_falls_back_to_cpu(self):
        """``findOccludedMeshes`` reruns on the CPU when the GPU check fails.

        With PTX JIT off, the GPU check fails on any GPU but Turing (mesh_tools ships Turing code only).
        """
        out = self._tmp_path("out.usda")
        env = dict(os.environ, CUDA_DISABLE_PTX_JIT="1", CUDA_CACHE_DISABLE="1")
        args = ["-o", "findOccludedMeshes", "-a", "useGpu=1", "-a", "minimumGapSize=4", "-w", out]
        result = subprocess.run(
            [_CLI, *args, self._stage_path("hiddenMesh.usda")], env=env, capture_output=True, text=True
        )
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        stage = Usd.Stage.Open(out)
        prim = stage.GetPrimAtPath("/root/hidden/hidden")
        self.assertEqual(UsdGeom.Imageable(prim).ComputeVisibility(), UsdGeom.Tokens.invisible)
