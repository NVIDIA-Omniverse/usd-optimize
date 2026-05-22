# Installing the Prebuilt Scene Optimizer Package on Windows

This guide is for **consumers of a published `scene_optimizer_core` package** on Windows — for example, a drop named like:

```
scene_optimizer_core_usd_<usd_ver>_py_<py_ver>@<version>.<platform>.release
```

If you are building Scene Optimizer from source, see the top-level [README](../README.md) and use `repo.bat build` / `repo.bat test` instead. The published package does **not** include `repo.bat`, source code, or test fixtures — only headers, prebuilt libraries, and Python bindings.

## Package Layout

| Directory | Purpose |
| --- | --- |
| `include/` | C++ public headers (`omni/scene.optimizer/core/`) |
| `lib/` | Prebuilt DLLs and Windows import libraries (`omni.scene.optimizer.core.dll`, plugin DLLs, `operation_mapping.json` — deprecated-name aliases for `map_config()`, not the list of operations) |
| `python/` | Python bindings (`omni.scene.optimizer.core`) and bundled tests under `python/tests/test.python/` |
| `usdpy/` | OpenUSD Python runtime modules (`pxr.*`) — the package brings its own USD |
| `extraLibs/` | Third-party runtime libraries (Alembic, MaterialX, OpenSubdiv, TBB) and the matching CPython runtime DLL (e.g. `python312.dll` for `py_3.12`) |

There is **no `python.exe` in the package** — you must supply your own interpreter that matches the package's Python ABI.

## Prerequisites

### Python — must match the package name

The Python version is encoded in the package directory name (`py_3.12` in the example above). The bundled USD `.pyd` modules link against `python312.dll`, so loading them under any other Python (3.10, 3.11, 3.13, …) fails with:

```
ImportError: Module use of python312.dll conflicts with this version of Python.
```

This is a hard ABI requirement, not a preference. Install the matching Python — for the `py_3.12` package:

```powershell
winget install --id Python.Python.3.12 --scope user
```

User-scope keeps the installer out of `C:\Program Files\` and avoids touching any system Python you already have.

### C++ runtime (only if you link against the C++ libraries)

You only need the **Microsoft Visual C++ Redistributable** (or a Visual Studio install with the C++ workload) on the target machine if you are linking your own C++ application against `omni.scene.optimizer.core.lib`. Pure-Python consumers can skip this — `python312.dll` and the bundled USD/TBB DLLs cover the runtime needs.

## Installing

### 1. Extract the package

Place the unpacked directory anywhere — for the rest of this guide we assume it lives at:

```
%PACKAGE_ROOT% = C:\path\to\scene_optimizer_core_usd_25.11_py_3.12@<version>.windows-x86_64.release
```

### 2. (Recommended) Create a Python virtual environment

A venv keeps Scene Optimizer's `PYTHONPATH` tweaks isolated from any other Python project on the machine:

```powershell
$root = "C:\path\to\scene_optimizer_core_usd_25.11_py_3.12@<version>.windows-x86_64.release"
& "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe" -m venv "$root\.venv"
```

Adjust the interpreter path to wherever your matching Python lives (`py -3.12 -c "import sys; print(sys.executable)"` will print it).

### 3. Set environment variables

Two paths must be exported every session:

| Variable | Why |
| --- | --- |
| `PYTHONPATH` += `python;usdpy` | Lets the interpreter find both `omni.scene.optimizer.*` and `pxr.*` |
| `PATH` += `lib;extraLibs` | Lets Windows resolve transitive DLL dependencies (USD, TBB, Alembic, plugin DLLs) |

PowerShell:

```powershell
$root = "C:\path\to\scene_optimizer_core_usd_25.11_py_3.12@<version>.windows-x86_64.release"
$env:PYTHONPATH = "$root\python;$root\usdpy;$env:PYTHONPATH"
$env:PATH = "$root\lib;$root\extraLibs;$env:PATH"
```

cmd.exe:

```bat
set PACKAGE_ROOT=C:\path\to\scene_optimizer_core_usd_25.11_py_3.12@<version>.windows-x86_64.release
set PYTHONPATH=%PACKAGE_ROOT%\python;%PACKAGE_ROOT%\usdpy;%PYTHONPATH%
set PATH=%PACKAGE_ROOT%\lib;%PACKAGE_ROOT%\extraLibs;%PATH%
```

To make the settings durable, wrap them in an activation script (e.g. extend `.venv\Scripts\activate.ps1`) or set them via the **System Properties → Environment Variables** dialog.

## Verifying the Install

A two-step smoke test confirms the bindings load and a real operation executes against an in-memory USD stage. Save the script as `smoke_check.py` and run it with the matching Python.

```python
# smoke_check.py
import json
from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from omni.scene.optimizer.core.scripts import standalone
from pxr import Usd, UsdGeom

# 1. Bindings + USD load
ctx = ExecutionContext()
assert ctx.usdStageId == -1
stage = Usd.Stage.CreateInMemory()
assert ctx.set_stage(stage) and ctx.usdStageId != -1
ctx.remove_stage()
print("[1/3] bindings + USD: OK")

# 2. Op registry populated
core = SceneOptimizerCore.getInstance()
ops = core.getOperations()
assert len(ops) > 0
print(f"[2/3] op registry: {len(ops)} operations registered")

# 3. End-to-end through the public 'standalone' API
stage = Usd.Stage.CreateInMemory()
UsdGeom.Xform.Define(stage, "/World")
UsdGeom.Cube.Define(stage, "/World/c1")
UsdGeom.Cube.Define(stage, "/World/c2")
ops_json = json.dumps([
    {"operation": "executionContext", "verbose": False},
    {"operation": "deletePrims", "primPaths": ["/World/c1"]},
])
assert standalone.execute_commands_from_json(stage, ops_json)
assert sum(1 for _ in stage.TraverseAll()) == 2  # one prim removed
print("[3/3] standalone.execute_commands_from_json: OK")

print("\nALL SMOKE CHECKS PASSED")
```

Run it:

```powershell
& "$root\.venv\Scripts\python.exe" smoke_check.py
```

Expected output:

```
[1/3] bindings + USD: OK
[2/3] op registry: <N> operations registered
[3/3] standalone.execute_commands_from_json: OK

ALL SMOKE CHECKS PASSED
```

The exact value of `<N>` varies by build — any positive number confirms the plugins loaded.

## Using Scene Optimizer in Your Code

The public Python entry point is `omni.scene.optimizer.core.scripts.standalone`. It accepts a `Usd.Stage` and a list of operation descriptors as JSON:

```python
from omni.scene.optimizer.core.scripts import standalone
from pxr import Usd

stage = Usd.Stage.Open("scene.usd")
ops = """[
    {"operation": "executionContext", "verbose": true},
    {"operation": "merge"},
    {"operation": "optimizeMaterials"}
]"""
ok = standalone.execute_commands_from_json(stage, ops)
stage.Save()
```

Valid **`operation`** strings are whatever the loaded plugins register — enumerate them at runtime with `SceneOptimizerCore.getInstance().getOperations()` (the exact count varies by build). The bundled tests under `python/tests/test.python/` show descriptor JSON for many operations. **`lib/operation_mapping.json` is not that catalog:** it only lists deprecated operation keys and a few legacy argument renames for `standalone.map_config()`, so keys such as `merge`, `deletePrims`, or `decimateMeshes` will not appear there. The full per-operation argument reference is in the [Scene Optimizer user manual](https://docs.omniverse.nvidia.com/extensions/latest/ext_scene-optimizer/user-manual.html).

## Notes on the Bundled Tests

`python/tests/test.python/` ships the full Python suite from the repository plus `run_discover.py`. **Do not expect `run_discover.py` to pass on a minimal binary-release install.**

- **`test_validators_*.py`** depend on NVIDIA **`omniverse-asset-validator`** from [PyPI](https://pypi.org/project/omniverse-asset-validator/) (`pip install omniverse-asset-validator`). They import `omni.asset_validator`; without that package you get **`ModuleNotFoundError: No module named 'omni.asset_validator'`** (one failure line per module at import time).

- **`run_discover.py` imports every `test_*.py` before unittest runs.** If **any** import fails, it prints all import failures to stderr and **`sys.exit(1)` without running tests** — so a typical release sees validator import errors only and **executes zero tests**, not a long report of fixture misses. Only after every module imports successfully does the runner execute tests; **many** of those tests still expect USD fixtures under `../../../exts/omni.scene.optimizer.core/data`, which exists in the source tree but not in the published package.

The self-contained tests in `test_core_python_bindings.py` (`test_executionContext`, `test_executionContext_reportPath_roundtrip`, `test_executionContext_reportPath_survives_executeOperation`, `test_sceneOptimizerCore`, `test_operation`) are equivalent to the smoke-check above.

## Troubleshooting

**`ImportError: Module use of python312.dll conflicts with this version of Python.`**
Your interpreter does not match the package's `py_<version>` token. Install the matching Python.

**`ImportError: DLL load failed while importing _tf` (or another `pxr` module)**
`PATH` is missing `lib` or `extraLibs`. Both must be on `PATH` before the Python process starts so Windows can resolve transitive DLLs. Setting them after `import pxr` has already run will not help — restart the interpreter.

**`ModuleNotFoundError: No module named 'omni.scene.optimizer'` or `'pxr'`**
`PYTHONPATH` is missing `python` or `usdpy`. Both directories must be on `PYTHONPATH`.

**`ModuleNotFoundError: No module named 'omni.asset_validator'`** (common when running bundled `run_discover.py`)
The `test_validators_*.py` modules require PyPI **`omniverse-asset-validator`** (`pip install omniverse-asset-validator`). Without it, `run_discover.py` fails during its import phase and runs no tests. Prefer `test_core_python_bindings.py` or the [smoke check](#verifying-the-install) for package verification alone.

**`SceneOptimizerCore.getInstance().getOperations()` returns an empty list**
The plugin DLLs in `lib/` did not load. Confirm the directory is on `PATH`, that no DLLs were quarantined by antivirus, and that the package matches your platform (`windows-x86_64`).
