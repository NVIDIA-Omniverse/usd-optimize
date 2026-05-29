---
name: prebuilt-package
description: Install and verify a prebuilt Scene Optimizer package (no source build, no repo.sh). Use for binary-drop deployments.
version: "1.0.0"
allowed-tools: Shell
metadata:
  author: NVIDIA Corporation
  tags: [install, deployment, package]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Installing a Prebuilt Scene Optimizer Package

This skill covers consuming a **published binary drop** (e.g. `scene_optimizer_core_usd_<usd_ver>_py_<py_ver>@<version>.<platform>.release`). It is **not** for building from source — for that, use the `build` skill or `repo.bat build` / `repo.sh build`.

OS-specific install walkthroughs (interpreter install, environment-variable syntax, virtual-env activation):

- Windows: [`docs/install-prebuilt-windows.md`](../../../docs/install-prebuilt-windows.md)
- Linux: [`docs/install-prebuilt-linux.md`](../../../docs/install-prebuilt-linux.md)

The notes below are platform-neutral and capture the load-bearing details.

## What this skill covers

Search this doc for keywords like `usdpy`, `PYTHONPATH`, `LD_LIBRARY_PATH`, `entry-point`, `register_all`, `analysisMode`, `omni_asset_validate`, `auditwheel` to jump.

- **What's in a prebuilt drop** — directory layout (`include/`, `lib/`, `python/`, `usdpy/`).
- **Prerequisites** — interpreter / OS requirements.
- **Smoke-check the install** — minimal verification commands.
- **What does NOT work in the drop** — repo.sh, source rebuilds, dev driver scripts.
- **Public API surface** — what's importable.
- **Purpose / Limitations / Troubleshooting** — scope summary, scope boundaries, common failure modes (PYTHONPATH, libusd alignment, allow-list env var).

Companion skills: `build` (build from source instead), `validators` (run-time validator infrastructure), `run-validators` / `run-operations` (dev driver scripts that need a source build, NOT a drop).

## What's in a prebuilt drop

| Directory | Contents |
| --- | --- |
| `include/` | C++ public headers |
| `lib/` | Compiled core and plugin libraries (Windows: `*.dll` + `*.lib`; Linux: `*.so`) plus `operation_mapping.json` (small deprecated-name alias table for `map_config()`, not the operation catalog) |
| `python/` | `omni.scene.optimizer.*` Python bindings; bundled tests under `python/tests/test.python/` |
| `usdpy/` | OpenUSD Python runtime (`pxr.*`) — the package brings its own USD |
| `extraLibs/` | Third-party runtime libraries (Alembic, MaterialX, OpenSubdiv, TBB). **Windows** releases also ship the matching CPython runtime DLL (e.g. `python312.dll` for `py_3.12`). **Linux** releases do **not** bundle `libpython3.X.so.1.0` — it must come from the interpreter install (see Linux guide). |

No Python interpreter is bundled — the consumer supplies one.

## Prerequisites

1. **Python version must match the `py_<ver>` token in the package name.** The bundled `pxr` extension modules link against a specific CPython ABI; loading them under any other Python version fails at import time. On Windows the failure surfaces as `ImportError: Module use of python<XY>.dll conflicts with this version of Python.`; on Linux it surfaces as undefined-symbol or version-mismatch errors from the dynamic linker. The matching interpreter is the consumer's responsibility — see the OS-specific guide above.
2. **`PYTHONPATH` must include both `python` and `usdpy`.** Missing `usdpy` produces `ModuleNotFoundError: No module named 'pxr'`.
3. **The platform's library-search path must include both `lib` and `extraLibs`, set *before the interpreter starts*.** That's `PATH` on Windows and `LD_LIBRARY_PATH` on Linux. The path is consulted at module-load time only — exporting it after the Python process is running has no effect; restart the interpreter.

**Linux — `libpython` on disk vs `ldconfig -p`.** Bundled `pxr` needs `libpython3.X.so.1.0` from your interpreter; `ldconfig -p` only reflects the linker **cache** and can be stale. Verify with the snippet in [`docs/install-prebuilt-linux.md`](../../../docs/install-prebuilt-linux.md) (Python prerequisites): set `PYLIBDIR` from `sysconfig.get_config_var("LIBDIR")`, `ls` there, and compare to `ldconfig -p | grep libpython…`. Interpretation: **(1)** no `.so` under `PYLIBDIR` → install shared `libpython` or rebuild Python with `--enable-shared`; **(2)** `.so` under `/usr/lib` or `/lib` (multiarch counts) but `ldconfig -p` empty → run **`sudo ldconfig`** once — avoid reinstall churn or redundant `LD_LIBRARY_PATH` when libs already sit in the default linker layout; **(3)** `.so` only under pyenv/conda-style prefixes → add that `PYLIBDIR` to `LD_LIBRARY_PATH` (conda activation usually does this).

## Smoke-check the install

A short script that proves the bindings load and an op runs against an in-memory stage. Save as `smoke_check.py` and run with the matching Python:

```python
import json
from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from omni.scene.optimizer.core.scripts import standalone
from pxr import Usd, UsdGeom

core = SceneOptimizerCore.getInstance()
assert len(core.getOperations()) > 0

stage = Usd.Stage.CreateInMemory()
UsdGeom.Xform.Define(stage, "/World")
UsdGeom.Cube.Define(stage, "/World/c1")
UsdGeom.Cube.Define(stage, "/World/c2")
ok = standalone.execute_commands_from_json(stage, json.dumps([
    {"operation": "deletePrims", "primPaths": ["/World/c1"]},
]))
assert ok and sum(1 for _ in stage.TraverseAll()) == 2
print("OK")
```

If this prints `OK` the drop is healthy. Any positive op-registry count confirms the plugins loaded; the exact count varies by build.

## What does NOT work in the drop

- **`repo.sh build` / `repo.bat test`** — `repo.sh`/`repo.bat` is not shipped. The README's "Quickstart" applies to the source repo, not to the binary drop.
- **`python/tests/test.python/run_discover.py`** — the bundled `test_validators_*.py` modules require PyPI **`omniverse-asset-validator`** (`omni.asset_validator`). Without it, imports fail before unittest runs. If **any** module fails to import, `run_discover.py` exits with code 1 **without executing tests**. Even with imports fixed, most modules still expect fixtures under `tests/data`. The self-contained cases in `test_core_python_bindings.py` (`test_executionContext`, `test_executionContext_reportPath_roundtrip`, `test_executionContext_reportPath_survives_executeOperation`, `test_sceneOptimizerCore`, `test_operation`) match the smoke check above.

## Public API surface

The supported entry point for standalone consumers is `omni.scene.optimizer.core.scripts.standalone`:

- `execute_commands_from_json(stage, filepath_or_json)` — runs a JSON list of `{"operation": …, …}` descriptors against a `Usd.Stage`. Returns `bool` (overall success).
- `map_config(config)` — applies the operation/argument renames in `lib/operation_mapping.json` so older configs keep working.
- `get_output_paths(operation)` / `get_output_path_arrays(operation)` — return `[]` in standalone mode (only meaningful inside Kit).

Operation keys accepted by `execute_commands_from_json` are the strings from `SceneOptimizerCore.getInstance().getOperations()` at runtime (count varies by build). Bundled tests under `python/tests/test.python/` illustrate descriptor JSON for many operations. `lib/operation_mapping.json` is only a small backward-compatibility alias table for `map_config()`, not the full operation list.

## Purpose

Stand up a working Scene Optimizer install from a prebuilt binary drop —
no source clone, no `repo.sh`/`repo.bat`, no compiler. Cover the layout
of the drop, the strict interpreter / library-path requirements, the
canonical smoke check, and the boundaries of the supported public API
so consumers can integrate the package into their own pipeline without
reaching for the dev tooling.

## Limitations

This is a **runtime / consumer** skill, not a development skill.
The following are intentionally out of scope:

- **Source rebuilds.** A drop has no `repo.sh` / `repo.bat` and no
  compiler toolchain. Use the `build` skill against a checkout instead.
- **Dev driver scripts.** `tools/perf_validators/run.sh`,
  `tools/perf_operations/run.sh`, and similar require a source tree
  with `_build/<platform>/<config>/`; they will not work against a drop.
- **Most bundled tests.** `run_discover.py` needs every `test_*.py` to
  import cleanly first; missing **`omniverse-asset-validator`** breaks the
  `test_validators_*.py` set immediately. Any import failure prevents the
  unittest phase. Use the smoke check above as the supported install verification.
- **Mixing your own USD with the drop's `usdpy`.** `pxr` and the C++
  core must resolve to the same `libusd` build — point `PYTHONPATH`
  at the drop's `usdpy` and don't shadow it with another USD install.
- **Non-published platforms.** Drops are produced for specific
  platform/USD/Python combinations (encoded in the package name).
  Other combinations need a source build.

## Troubleshooting

| Symptom | Likely cause |
| --- | --- |
| At-import error naming a specific `python<XY>.dll` / `libpython<XY>.so` ABI mismatch | Interpreter version doesn't match the `py_<ver>` token. |
| Linux: `ImportError: libpython3.X.so.1.0: cannot open shared object file` | Compare **`PYLIBDIR`/`ls`** vs **`ldconfig -p`** per [Linux prebuilt guide](../../../docs/install-prebuilt-linux.md): often stale cache (`sudo ldconfig`), pyenv/conda needs `LD_LIBRARY_PATH`, or missing `libpython` install — see troubleshooting there. |
| Importing any `pxr.*` module fails to resolve a transitive native dependency (Windows: `DLL load failed`; Linux: `cannot open shared object file`) | Library-search path missing `lib` or `extraLibs`, or set after the process started. |
| `ModuleNotFoundError: No module named 'pxr'` or `'omni.scene.optimizer'` | `PYTHONPATH` missing `usdpy` or `python` respectively. |
| `getOperations()` returns `[]` | Plugin libraries in `lib/` failed to load — wrong-platform package, antivirus quarantine, or library-search-path issue. |

