---
name: prebuilt-package
description: Install and verify a prebuilt Usd Optimize package (no source build, no repo.sh). Use for binary-drop deployments.
allowed-tools: Shell
metadata:
  author: NVIDIA Corporation
  version: "1.0.0"
  tags: [install, deployment, package]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Installing a Prebuilt Usd Optimize Package

This skill covers consuming a **published binary drop** (e.g. `usd_optimize_usd_<usd_ver>_py_<py_ver>@<version>.<platform>.release`). It is **not** for building from source — for that, use the `build` skill or `repo.bat build` / `repo.sh build`.

OS-specific install walkthroughs (interpreter install, environment-variable syntax, virtual-env activation):

- Windows: [`docs/install-prebuilt-windows.md`](../../../docs/install-prebuilt-windows.md)
- Linux: [`docs/install-prebuilt-linux.md`](../../../docs/install-prebuilt-linux.md)

The notes below are platform-neutral and capture the load-bearing details.

## What this skill covers

Search this doc for keywords like `usdpy`, `PYTHONPATH`, `LD_LIBRARY_PATH`, `entry-point`, `register_all`, `analysisMode`, `nvidia_usd_validate`, `auditwheel` to jump.

- **What's in a prebuilt drop** — directory layout (`include/`, `lib/`, `python/`, `usdpy/`).
- **Prerequisites** — interpreter / OS requirements.
- **Smoke-check the install** — minimal verification commands.
- **What does NOT work in the drop** — repo.sh, source rebuilds, dev driver scripts.
- **Public API surface** — what's importable.
- **Purpose / Limitations / Troubleshooting** — scope summary, scope boundaries, common failure modes (PYTHONPATH, libusd alignment, validator plugin auto-loading).

Companion skills: `build` (build from source instead), `run-validators` / `run-operations` (dev driver scripts that need a source build, NOT a drop).

## What's in a prebuilt drop

| Directory | Contents |
| --- | --- |
| `include/` | C++ public headers |
| `lib/` | Compiled core and plugin libraries (Windows: `*.dll` + `*.lib`; Linux: `*.so`) plus `operation_mapping.json` (small deprecated-name alias table for `mapConfig()`, not the operation catalog) |
| `python/` | `usd_optimize.*` Python bindings |
| `usdpy/` | OpenUSD Python runtime (`pxr.*`) — the package brings its own USD |
| `extraLibs/` | Third-party runtime libraries (MaterialX, TBB; USD 25.x drops additionally carry Alembic and OpenSubdiv), plus the matching CPython runtime on **both** platforms — `python312.dll` for `py_3.12` on Windows, `libpython3.X.so.1.0` on Linux. Consumers supply the interpreter, not the runtime library. |

No Python interpreter is bundled — the consumer supplies one.

## Prerequisites

1. **Python version must match the `py_<ver>` token in the package name.** The bundled `pxr` extension modules are compiled against a specific CPython ABI. On Windows a mismatch surfaces at import as `ImportError: Module use of python<XY>.dll conflicts with this version of Python.` **On Linux, importing `pxr` does not catch it** — the drop bundles an exact-match `libpython3.X.so.1.0`, so the loader has nothing to object to and a `pxr` import can *appear* to succeed under the wrong interpreter; you may instead get an `undefined symbol: PyXxx_...` ImportError. Which of the two you hit depends on the interpreter and on which symbols a module touches, so neither outcome is guaranteed and a clean `pxr` import does not mean the interpreter is right. Importing `usd_optimize.core` does check, and raises an `ImportError` naming both ABIs. Running a mismatched interpreter is unsupported and untested; the matching one is the consumer's responsibility. See the OS-specific guide above.
2. **`PYTHONPATH` must include both `python` and `usdpy`.** Missing `usdpy` produces `ModuleNotFoundError: No module named 'pxr'`.
3. **The platform's library-search path must include both `lib` and `extraLibs`, set *before the interpreter starts*.** That's `PATH` on Windows and `LD_LIBRARY_PATH` on Linux. The path is consulted at module-load time only — exporting it after the Python process is running has no effect; restart the interpreter.

**Linux — `libpython` comes from the drop, not the interpreter.** Bundled `pxr` needs `libpython3.X.so.1.0`, and `extraLibs/` supplies it; it resolves from there as soon as `extraLibs` is on `LD_LIBRARY_PATH` (prerequisite 3). So a plain interpreter install is enough — no system `libpython` package, no `sudo ldconfig`, no `--enable-shared` rebuild. If you see `cannot open shared object file: libpython3.X.so.1.0`, the cause is a missing or late-exported `LD_LIBRARY_PATH`, not a missing package; see [`docs/install-prebuilt-linux.md`](../../../docs/install-prebuilt-linux.md).

## Smoke-check the install

A short script that proves the bindings load and an op runs against an in-memory stage. Save as `smoke_check.py` and run with the matching Python:

```python
from usd_optimize.core import ExecutionContext, UsdOptimizeCore
from pxr import Usd, UsdGeom

core = UsdOptimizeCore.getInstance()
assert len(core.getOperations()) > 0

stage = Usd.Stage.CreateInMemory()
UsdGeom.Xform.Define(stage, "/World")
UsdGeom.Cube.Define(stage, "/World/c1")
UsdGeom.Cube.Define(stage, "/World/c2")
ctx = ExecutionContext()
ctx.set_stage(stage)
results = core.executeConfig(ctx, [
    {"operation": "deletePrims", "primPaths": ["/World/c1"]},
])
assert all(ok for ok, _err, _out in results) and sum(1 for _ in stage.TraverseAll()) == 2
print("OK")
```

If this prints `OK` the drop is healthy. Any positive op-registry count confirms the plugins loaded; the exact count varies by build.

## What does NOT work in the drop

- **`repo.sh build` / `repo.bat test`** — `repo.sh`/`repo.bat` is not shipped. The README's "Quickstart" applies to the source repo, not to the binary drop.
- **The Python test suite** — `python/tests/` is not part of a drop. The suite and its fixtures are routed into a separate, unpublished archive (`usd_optimize_tests` in `repo.toml`), so there is no `run_discover.py` to run. Use the smoke check above to confirm a drop is healthy.

## Public API surface

The supported entry point for standalone consumers is the `UsdOptimizeCore` singleton in `usd_optimize.core` (canonical examples: `docs/overview.rst`):

- `executeConfig(context, config)` — runs a list of `{"operation": …, …}` descriptor dicts against the stage bound to the `ExecutionContext`. Returns one `(success, error, output)` tuple per operation. Takes a Python list — for JSON input, pass `json.loads(text)` / `json.load(f)`, not the path or raw text.
- `executeOperation(name, context, args)` — runs a single operation; returns one `(success, error, output)` tuple.
- `mapConfig(config_json)` — applies the operation/argument renames in `lib/operation_mapping.json` (JSON string in, JSON string out) so older configs keep working.

Operation keys accepted by `executeConfig` are the strings from `UsdOptimizeCore.getInstance().getOperations()` at runtime (count varies by build). The bundled `config_presets/*.json` illustrate descriptor JSON for many operations. `lib/operation_mapping.json` is only a small backward-compatibility alias table for `mapConfig()`, not the full operation list.

## Purpose

Stand up a working Usd Optimize install from a prebuilt binary drop —
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
- **Dev driver scripts.** `tools/validators/run.sh` and similar
  require a source tree with `_build/<platform>/<config>/`; they will not
  work against a drop. (A drop ships its own `bin/usdOptimize` CLI for
  running operations.)
- **The Python test suite.** It is not part of a drop — the suite and its
  fixtures go to a separate, unpublished archive. Use the smoke check above
  as the supported install verification.
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
| Linux: `ImportError: libpython3.X.so.1.0: cannot open shared object file` | `extraLibs` missing from `LD_LIBRARY_PATH`, or exported after the interpreter started. The drop bundles this library — it is not a missing system package. |
| Importing any `pxr.*` module fails to resolve a transitive native dependency (Windows: `DLL load failed`; Linux: `cannot open shared object file`) | Library-search path missing `lib` or `extraLibs`, or set after the process started. |
| `ModuleNotFoundError: No module named 'pxr'` or `'usd_optimize'` | `PYTHONPATH` missing `usdpy` or `python` respectively. |
| `getOperations()` returns `[]` | Plugin libraries in `lib/` failed to load — wrong-platform package, antivirus quarantine, or library-search-path issue. |

