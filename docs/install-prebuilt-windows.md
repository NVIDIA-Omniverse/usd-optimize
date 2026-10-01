# Installing the Prebuilt Usd Optimize Package on Windows

This guide is for **consumers of a published `usd_optimize` package** on Windows — for example, a drop named like:

```
usd_optimize_usd_<usd_ver>_py_<py_ver>@<version>.<platform>.release
```

If you are building Usd Optimize from source, see the top-level [README](../README.md) and use `repo.bat build` / `repo.bat test` instead. The published package does **not** include `repo.bat`, source code, or test fixtures — only headers, prebuilt libraries, and Python bindings.

## Package Layout

| Directory | Purpose |
| --- | --- |
| `include/` | C++ public headers (`usd_optimize/core/`) |
| `lib/` | Prebuilt DLLs and Windows import libraries (`usd_optimize.core.dll`, plugin DLLs, `operation_mapping.json` — deprecated-name aliases for `mapConfig()`, not the list of operations) |
| `bin/` | The `usdOptimize` command-line tool |
| `python/` | Python bindings (`usd_optimize.core`) |
| `usdpy/` | OpenUSD Python runtime modules (`pxr.*`) — the package brings its own USD |
| `extraLibs/` | Third-party runtime libraries (MaterialX, TBB; USD 25.x drops additionally carry Alembic and OpenSubdiv) and the matching CPython runtime DLL (e.g. `python312.dll` for `py_3.12`) |
| `config_presets/` | Ready-made operation stacks for `usdOptimize -c` |
| `docs/` | This guide and the rest of the documentation set |
| `.agents/` | Task-specific skill files (`.agents/skills/<name>/SKILL.md`) |

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

### Microsoft Visual C++ Redistributable — required

Every consumer needs the **Microsoft Visual C++ 2015–2022 Redistributable (x64)** installed on the target machine — including pure-Python consumers, not only those linking their own C++ application against `usd_optimize.core.lib`. The bundled USD DLLs on the `import pxr` path (e.g. `usd_tf.dll`) import `MSVCP140.dll`, `VCRUNTIME140.dll`, and `VCRUNTIME140_1.dll`, which the package does **not** bundle.

```powershell
winget install --id Microsoft.VCRedist.2015+.x64
```

(Or install [`vc_redist.x64.exe`](https://aka.ms/vs/17/release/vc_redist.x64.exe) directly.) A python.org interpreter ships `VCRUNTIME140.dll` / `VCRUNTIME140_1.dll` next to `python.exe` but **not** `MSVCP140.dll`, so the redistributable is required regardless of how Python was installed. It is widely present on developer and desktop machines — so this often works with no action — but it is not part of a base Windows install and is frequently absent on clean images, Windows Server Core, containers, and minimal CI runners.

## Installing

### 1. Extract the package

Place the unpacked directory anywhere — for the rest of this guide we assume it lives at:

```
%PACKAGE_ROOT% = C:\path\to\usd_optimize_usd_25.11_py_3.12@<version>.windows-x86_64.release
```

### 2. (Recommended) Create a Python virtual environment

A venv keeps Usd Optimize's `PYTHONPATH` tweaks isolated from any other Python project on the machine:

```powershell
$root = "C:\path\to\usd_optimize_usd_25.11_py_3.12@<version>.windows-x86_64.release"
& "$env:LOCALAPPDATA\Programs\Python\Python312\python.exe" -m venv "$root\.venv"
```

Adjust the interpreter path to wherever your matching Python lives (`py -3.12 -c "import sys; print(sys.executable)"` will print it).

### 3. Set environment variables

Two paths must be exported every session:

| Variable | Why |
| --- | --- |
| `PYTHONPATH` += `python;usdpy` | Lets the interpreter find both `usd_optimize.*` and `pxr.*` |
| `PATH` += `lib;extraLibs` | Lets Windows resolve transitive DLL dependencies (USD, TBB, plugin DLLs) |

PowerShell:

```powershell
$root = "C:\path\to\usd_optimize_usd_25.11_py_3.12@<version>.windows-x86_64.release"
$env:PYTHONPATH = "$root\python;$root\usdpy;$env:PYTHONPATH"
$env:PATH = "$root\lib;$root\extraLibs;$env:PATH"
```

cmd.exe:

```bat
set PACKAGE_ROOT=C:\path\to\usd_optimize_usd_25.11_py_3.12@<version>.windows-x86_64.release
set PYTHONPATH=%PACKAGE_ROOT%\python;%PACKAGE_ROOT%\usdpy;%PYTHONPATH%
set PATH=%PACKAGE_ROOT%\lib;%PACKAGE_ROOT%\extraLibs;%PATH%
```

To make the settings durable, wrap them in an activation script (e.g. extend `.venv\Scripts\activate.ps1`) or set them via the **System Properties → Environment Variables** dialog.

## Verifying the Install

A two-step smoke test confirms the bindings load and a real operation executes against an in-memory USD stage. Save the script as `smoke_check.py` and run it with the matching Python.

```python
# smoke_check.py
from usd_optimize.core import ExecutionContext, UsdOptimizeCore
from pxr import Usd, UsdGeom

# 1. Bindings + USD load
ctx = ExecutionContext()
assert ctx.usdStageId == -1
stage = Usd.Stage.CreateInMemory()
assert ctx.set_stage(stage) and ctx.usdStageId != -1
ctx.remove_stage()
print("[1/3] bindings + USD: OK")

# 2. Op registry populated
core = UsdOptimizeCore.getInstance()
ops = core.getOperations()
assert len(ops) > 0
print(f"[2/3] op registry: {len(ops)} operations registered")

# 3. End-to-end through the public UsdOptimizeCore API
stage = Usd.Stage.CreateInMemory()
UsdGeom.Xform.Define(stage, "/World")
UsdGeom.Cube.Define(stage, "/World/c1")
UsdGeom.Cube.Define(stage, "/World/c2")
ctx.set_stage(stage)
results = core.executeConfig(ctx, [
    {"operation": "deletePrims", "primPaths": ["/World/c1"]},
])
assert all(success for success, _error, _output in results)
assert sum(1 for _ in stage.TraverseAll()) == 2  # one prim removed
print("[3/3] UsdOptimizeCore.executeConfig: OK")

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
[3/3] UsdOptimizeCore.executeConfig: OK

ALL SMOKE CHECKS PASSED
```

The exact value of `<N>` varies by build — any positive number confirms the plugins loaded.

## Using Usd Optimize in Your Code

The public Python entry point is the `UsdOptimizeCore` singleton in `usd_optimize.core`. Bind a `Usd.Stage` to an `ExecutionContext`, then apply a list of operation descriptors with `executeConfig`. It takes a parsed Python list (`json.loads`/`json.load` for JSON input, not raw text or a file path) and returns one `(success, error, output)` tuple per operation:

```python
import json
from usd_optimize.core import ExecutionContext, UsdOptimizeCore
from pxr import Usd

stage = Usd.Stage.Open("scene.usd")
context = ExecutionContext()
context.set_stage(stage)
ops = """[
    {"operation": "merge"},
    {"operation": "optimizeMaterials"}
]"""
results = UsdOptimizeCore.getInstance().executeConfig(context, json.loads(ops))
if not all(ok for ok, _err, _out in results):
    raise RuntimeError("optimization failed -- check Usd Optimize log")
stage.Save()
```

Valid **`operation`** strings are whatever the loaded plugins register — enumerate them at runtime with `UsdOptimizeCore.getInstance().getOperations()` (the exact count varies by build). The bundled `config_presets/*.json` show descriptor JSON for many operations. **`lib/operation_mapping.json` is not that catalog:** it only lists deprecated operation keys and a few legacy argument renames for `UsdOptimizeCore.getInstance().mapConfig()`, so keys such as `merge`, `deletePrims`, or `decimateMeshes` will not appear there. The full per-operation argument reference is in the [Usd Optimize user manual](https://docs.omniverse.nvidia.com/extensions/latest/ext_scene-optimizer/user-manual.html).

## Notes on Testing a Drop

The Python test suite is **not** part of a published drop. It and its fixtures are routed into a separate, unpublished archive, so there is no `run_discover.py` to run. Use the smoke-check above to confirm an install is healthy.

## Troubleshooting

**`ImportError: Module use of python312.dll conflicts with this version of Python.`**
Your interpreter does not match the package's `py_<version>` token. Install the matching Python.

**`ImportError: DLL load failed while importing _tf` (or another `pxr` module)**
Two common causes:
- **The Microsoft Visual C++ Redistributable is not installed** — a prerequisite (see above); without it the bundled USD DLLs fail to load with this error or `STATUS_DLL_NOT_FOUND`.
- **`PATH` is missing `lib` or `extraLibs`** — both must be on `PATH` before the Python process starts; setting them after `import pxr` has already run will not help, so restart the interpreter.

**`ModuleNotFoundError: No module named 'usd_optimize'` or `'pxr'`**
`PYTHONPATH` is missing `python` or `usdpy`. Both directories must be on `PYTHONPATH`.

**`ModuleNotFoundError: No module named 'usd_validation_nvidia'`** (importing `usd_optimize.validators`)
The drop ships the validator rules but not the framework they build on. Install it from PyPI: `pip install usd-validation-nvidia`. Only the validators need it — the core bindings and the [smoke check](#verifying-the-install) work without it.

**`UsdOptimizeCore.getInstance().getOperations()` returns an empty list**
The plugin DLLs in `lib/` did not load. Confirm the directory is on `PATH`, that no DLLs were quarantined by antivirus, and that the package matches your platform (`windows-x86_64`).
