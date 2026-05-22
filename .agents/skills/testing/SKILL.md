---
name: testing
description: Run all tests or individual tests for Scene Optimizer (repo.sh on Linux, repo.bat on Windows). Use for unit, binding, and coverage runs.
version: "1.0.0"
allowed-tools: Shell
metadata:
  author: NVIDIA Corporation
  tags: [testing, qa, doctest]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Testing Scene Optimizer

## What this skill covers

Search this doc for keywords like `pytest`, `doctest`, `python`, `coverage`, `gcov`, `-k`, `-s`, `--config` to jump.

- **Run All Tests** — `./repo.sh test`.
- **Test Suites** — `cpp`, `python` and how to scope a run.
- **Build Config** — running tests in `release` vs `debug`.
- **Running Individual Tests** — Doctest filters and Python suite limitations.
- **Test Locations** — where each suite's tests live in the tree.
- **Code Coverage (Linux only)** — gcov collection workflow.

Companion skills: `build` (must run before tests), `validators` (validator-specific tests under `test_validators_*`).

> The repo ships two equivalent entry scripts: `./repo.sh` for Linux/bash-compatible shells and `repo.bat` for Windows `cmd.exe`/PowerShell. They accept the same arguments — every example below shows both. Pick whichever matches the active shell.

## Run All Tests

```bash
./repo.sh test       # Linux / bash
repo.bat test        # Windows cmd.exe / PowerShell
```

This runs the default test suites: **cpp** (C++ unit tests) and **python** (Python binding tests). The defaults come from `repo.toml` → `[repo_test].default_suite = ["cpp", "python"]`.

> **Heads-up on the Run Summary:** the final `repo_test Run Summary` block only counts `glob_and_exec`-kind suites, so it reports `All 1 tests processes returned 0` even when cpp also ran. Confirm cpp ran by looking earlier in the output for `[doctest] Status: SUCCESS!` lines — don't trust the trailing summary alone.

## Test Suites

Available suites: `cpp`, `python`, `all`

```bash
# Run only C++ unit tests
./repo.sh test -s cpp
repo.bat test -s cpp

# Run only Python binding tests
./repo.sh test -s python
repo.bat test -s python

# Run all suites
./repo.sh test -s all
repo.bat test -s all
```

> **Note:** `test.cuda.utils/` under `source/tests/` is a helper *shared library* (`TestCudaUtils.so` / `.dll`) used by the cpp suite for `isCudaAvailable()` threading tests — it is **not** itself a separately runnable test suite.

## Build Config

Tests default to the **release** build. To test against debug:

```bash
./repo.sh test -c debug
repo.bat test -c debug
```

## Running Individual Tests

### C++ Unit Tests (Doctest)

The C++ tests use the Doctest framework. The test executable is at `_build/<platform>/<config>/test.cpp` (Linux) or `_build/windows-x86_64/<config>/test.cpp.exe` (Windows).

Use `-e=` to pass extra arguments through to the doctest executable (the `=` is required so that `--` prefixed values aren't parsed as repo flags):

```bash
# Run a specific test case by name
./repo.sh test -s cpp -e="--test-case=_deletePrims"
repo.bat test -s cpp -e="--test-case=_deletePrims"

# Run test cases matching a pattern
./repo.sh test -s cpp -e="--test-case=*Plugins*"
repo.bat test -s cpp -e="--test-case=*Plugins*"

# List all available test cases without running them
./repo.sh test -s cpp -e="--list-test-cases"
repo.bat test -s cpp -e="--list-test-cases"
```

You can also run the wrapper script directly for full doctest CLI control (use the bundled wrapper — `.sh` on Linux, `.bat` on Windows — so `LD_LIBRARY_PATH` / `PATH` are set correctly):

```bash
# Linux
./_build/linux-x86_64/release/test.cpp.sh --test-case="*Plugins*"
./_build/linux-x86_64/release/test.cpp.sh --test-case="_deletePrims" --subcase="DeleteOption::ePrimOnly"

# Windows (cmd.exe / PowerShell)
.\_build\windows-x86_64\release\test.cpp.bat --test-case="*Plugins*"
.\_build\windows-x86_64\release\test.cpp.bat --test-case="_deletePrims" --subcase="DeleteOption::ePrimOnly"
```

### Python Binding Tests

Run Python binding tests through the repo wrapper so the bundled Python, USD,
and Scene Optimizer paths are set correctly:

```bash
./repo.sh test -s python
repo.bat test -s python
```

The generated Python wrapper currently runs the full `python` suite. Do not use
`repo.sh test -s python -f "test_operation_*.py"` for per-file filtering: `-f`
filters the generated wrapper executable (`test.python.sh` / `.bat`), not the
files under `source/tests/test.python/`.

## Test Locations

```
source/tests/
├── test.cpp/omni.scene.optimizer.core/   # C++ unit tests (Doctest)
│   ├── TestPlugins.cpp
│   ├── TestMerge.cpp
│   ├── TestDeletePrims.cpp
│   ├── TestOptimizeMaterials.cpp
│   ├── TestSpatialAnalysis.cpp
│   └── TestMisc.cpp
├── test.python/                          # Python binding tests
│   ├── test_operation_merge.py
│   ├── test_operation_decimate_meshes.py
│   ├── test_operation_shrinkwrap.py
│   └── ... (one file per operation)
└── test.cuda.utils/                      # Helper shared lib for cpp suite (not a runnable suite)
```

## Code Coverage (Linux only)

The gcov-based coverage flow only runs on Linux because it depends on a gcc-built binary; `repo.bat` does not expose this command.

```bash
./repo.sh build --rebuild --config release --enable-gcov
./repo.sh test
./repo.sh cxx_coverage --collect --generate-html --remove --report
# Output: _build/coverage/
./repo.sh cxx_coverage --zero-coverage   # Reset counters
```

## Purpose

Drive the repo's test runner across the C++ unit suite (Doctest) and the
Python bindings suite. Cover both "run everything" and supported targeted
invocations (C++ test case filters, debug config, coverage run). Always go through `./repo.sh test`
or `repo.bat test` — never invoke the test executables raw, because
the wrappers set the library and Python paths the build expects.

## Prerequisites

- A built repo for the config you're testing — `./repo.sh build`
  (or `--config debug` for a debug-config run). The `build` skill
  covers this.
- For coverage: a `--enable-gcov` rebuild on Linux. Coverage is not
  available on Windows.
- For Python binding tests: nothing extra — the bundled Python is
  resolved by the test wrapper.

## Limitations

- This skill runs tests; it does **not** author them. New tests for a
  new operation are produced by the `new-operation` skill (Step 3).
- The Run Summary block at the end of `repo.sh test` only counts
  `glob_and_exec`-kind suites — the C++ suite is reported earlier in
  the output. Don't trust the trailing summary line alone.
- Code coverage (`cxx_coverage`) is **Linux only**.
- This skill does not run validator-driver tests against external
  assets — those use `run-validators` against the asset directly.

## Troubleshooting

Tests are themselves a form of validation; when they fail, treat the
failure as the signal to look at — don't paper over it.

| Symptom | Likely cause | Fix |
|---|---|---|
| All tests fail with `library not found` / `DLL load failed` | Built one config but ran tests against another (e.g. built `debug`, ran default `release`). | Match `--config` between `build` and `test` (or pass `-c debug` to both). |
| `[doctest] Status: SUCCESS!` but the trailing summary says `1 tests processes returned 0` | Quirk of the runner — the C++ suite isn't `glob_and_exec`, so the summary undercounts. | Scroll up to confirm the `[doctest]` line. The exit code is authoritative. |
| Python binding test errors with `ImportError: cannot import name SceneOptimizerCore` | Build didn't produce the bindings, or `PYTHONPATH` is shadowed. | Check `_build/<platform>/<config>/python/` exists. Run via `./repo.sh test -s python` (don't invoke pytest directly). |
| `repo.sh test -s python -f "test_operation_foo.py"` reports `All 0 tests processes returned 0` | `-f` filtered out the `test.python` wrapper instead of selecting a Python test file. | Run `./repo.sh test -s python`; the current Python wrapper runs the full suite. |
| Coverage HTML is empty after `cxx_coverage --collect` | Counters weren't zeroed, or no test was actually executed under the gcov build. | Run `./repo.sh cxx_coverage --zero-coverage` first, then `./repo.sh test`, then `--collect --generate-html`. |
| A specific Doctest case won't match `--test-case` filter | Pattern needs quoting — shells can eat `*`. | Use `-e="--test-case=*Plugins*"` (the `=` is required so the runner doesn't parse the value as its own flag). |

