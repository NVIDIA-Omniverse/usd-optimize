# Installing the Prebuilt Usd Optimize Package on Linux

This guide is for **consumers of a published `usd_optimize` package** on Linux — for example, a drop named like:

```
usd_optimize_usd_<usd_ver>_py_<py_ver>@<version>.<platform>.release
```

where `<platform>` is `linux-x86_64` or `linux-aarch64`. The two arches share the same layout and the same prerequisites; only the contents of the `.so` files differ.

If you are building Usd Optimize from source, see the top-level [README](../README.md) and use `repo.sh build` / `repo.sh test` instead. The published package does **not** include `repo.sh`, source code, or test fixtures — only headers, prebuilt libraries, and Python bindings.

## Package Layout

| Directory | Purpose |
| --- | --- |
| `include/` | C++ public headers (`usd_optimize/core/`) |
| `lib/` | Prebuilt shared libraries (`libusd_optimize.core.so`, plugin `.so` files, `operation_mapping.json` — deprecated-name aliases for `mapConfig()`, not the list of operations) |
| `bin/` | The `usdOptimize` command-line tool |
| `python/` | Python bindings (`usd_optimize.core`) |
| `usdpy/` | OpenUSD Python runtime modules (`pxr.*`) — the package brings its own USD |
| `extraLibs/` | Third-party runtime libraries (MaterialX, TBB; USD 25.x drops additionally carry Alembic and OpenSubdiv) and the matching CPython runtime (`libpython3.X.so.1.0`) |
| `config_presets/` | Ready-made operation stacks for `usdOptimize -c` |
| `docs/` | This guide and the rest of the documentation set |
| `.agents/` | Task-specific skill files (`.agents/skills/<name>/SKILL.md`) |

Two notable differences from the Windows drop:

- There is **no `python` interpreter in the package** — you must supply your own that matches the package's Python ABI.
- The Linux drop **does** bundle `libpython3.X.so.1.0`, in `extraLibs/`. The bundled `pxr` modules link against it dynamically and resolve it from there once `extraLibs` is on `LD_LIBRARY_PATH` ([step 3](#3-set-environment-variables)), so you do **not** have to install a shared `libpython` yourself.

## Prerequisites

### Python — must match the package name

The Python version is encoded in the package directory name (`py_3.12` in the example above). The bundled USD `.so` modules are compiled against the CPython 3.12 ABI, so you must supply a **matching 3.12 interpreter**.

You do **not** need to supply the shared `libpython`. The drop bundles `libpython3.12.so.1.0` in `extraLibs/`, and the bundled `pxr` modules resolve it from there once `extraLibs` is on `LD_LIBRARY_PATH` (see [step 3](#3-set-environment-variables)). A plain interpreter install is enough.

If `python3.12 --version` already works, you have everything you need — skip ahead to [Installing](#installing).

If it does not, you need to obtain one. On Ubuntu/Debian the [deadsnakes PPA](https://launchpad.net/~deadsnakes/+archive/ubuntu/ppa) is the easiest source for a Python version your distro does not ship:

```bash
sudo add-apt-repository ppa:deadsnakes/ppa
sudo apt-get update
sudo apt-get install python3.12 python3.12-venv
```

> **Importing `pxr` does not catch a mismatched interpreter.** Because the drop supplies an exact-match `libpython3.12.so.1.0`, the dynamic linker has nothing to object to, so importing `pxr` under 3.10 or 3.11 can *appear* to succeed rather than failing with a clear ABI error. You may instead get an `undefined symbol: PyXxx_...` ImportError (see [Troubleshooting](#troubleshooting)). Which of the two you hit depends on the interpreter and on which symbols a module touches, so **neither outcome is guaranteed and a clean `pxr` import does not mean the interpreter is right**. Importing `usd_optimize.core` does check, and raises an `ImportError` naming both ABIs (see [Troubleshooting](#troubleshooting)). Running a mismatched interpreter is unsupported and untested; [step 2](#2-recommended-create-a-python-virtual-environment) shows how to confirm the one you use.

If you prefer a distro-neutral install, [pyenv](https://github.com/pyenv/pyenv) works on any Linux: `pyenv install 3.12`. You do **not** need to add `PYTHON_CONFIGURE_OPTS="--enable-shared"`: current pyenv passes `--enable-shared` itself unless you ask for `--disable-shared`. A deliberately static build works too, because the drop supplies its own `libpython3.12.so.1.0`.

> **With `extraLibs` on `LD_LIBRARY_PATH`, the drop's `libpython` takes precedence.** `LD_LIBRARY_PATH` outranks an interpreter's own `DT_RUNPATH`, so a shared interpreter loads the drop's `libpython3.12.so.1.0` rather than the one it shipped with. Across 3.12 micro versions that is harmless — they are ABI-compatible — but it does mean `sys.version` can report the drop's build rather than the one you installed.

### C++ runtime (only if you link against the C++ libraries)

You only need a host C++ toolchain on the target machine if you are linking your own C++ application against `libusd_optimize.core.so`. Pure-Python consumers can skip this.

The Linux x86_64 build uses the C++11 ABI (`premake.linux_x86_64_cxx_abi` in `repo.toml`), so applications linking against the package must be built with the same ABI. A reasonably recent `libstdc++.so.6` is also required at runtime — symptoms of an old one surface as `version 'GLIBCXX_X.X.XX' not found` from the dynamic linker.

## Installing

### 1. Extract the package

Place the unpacked directory anywhere — for the rest of this guide we assume it lives at:

```bash
PACKAGE_ROOT=/path/to/usd_optimize_usd_25.11_py_3.12@<version>.linux-x86_64.release
```

### 2. (Recommended) Create a Python virtual environment

A venv keeps Usd Optimize's `PYTHONPATH` tweaks isolated from any other Python project on the machine:

```bash
python3.12 -m venv "$PACKAGE_ROOT/.venv"
source "$PACKAGE_ROOT/.venv/bin/activate"
```

Adjust the interpreter name to wherever your matching Python lives (`which python3.12` will print it).

Confirm the venv runs the matching interpreter (`3.12.x` for a `py_3.12` package). The [smoke test](#verifying-the-install) uses this same interpreter:

```bash
"$PACKAGE_ROOT/.venv/bin/python" -c "import sys; print(sys.version)"   # expect 3.12.x
```

If it prints anything else, recreate the venv with the matching Python.

### 3. Set environment variables

Two paths must be exported every session:

| Variable | Why |
| --- | --- |
| `PYTHONPATH` += `python:usdpy` | Lets the interpreter find both `usd_optimize.*` and `pxr.*` |
| `LD_LIBRARY_PATH` += `lib:extraLibs` | Lets the dynamic linker resolve transitive shared-object dependencies (USD, TBB, plugin `.so`s) |

bash/zsh:

```bash
export PACKAGE_ROOT=/path/to/usd_optimize_usd_25.11_py_3.12@<version>.linux-x86_64.release
export PYTHONPATH="$PACKAGE_ROOT/python:$PACKAGE_ROOT/usdpy${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$PACKAGE_ROOT/lib:$PACKAGE_ROOT/extraLibs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

The `${VAR:+:$VAR}` form avoids appending a trailing colon when the variable is unset, which the dynamic linker would otherwise interpret as the current working directory.

To make the settings durable, append the exports to the venv's `bin/activate` script, or to your `~/.bashrc` / `~/.zshrc` if Usd Optimize is the only Python package you use in that shell.

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

```bash
"$PACKAGE_ROOT/.venv/bin/python" smoke_check.py
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

**`ImportError: libpython3.12.so.1.0: cannot open shared object file: No such file or directory`**
The drop bundles this library in `extraLibs/`, so this almost always means `extraLibs` is missing from `LD_LIBRARY_PATH` — or was exported *after* the interpreter started. Re-check [step 3](#3-set-environment-variables) and restart the interpreter.

```bash
ls "$PACKAGE_ROOT/extraLibs" | grep libpython   # should list libpython3.12.so.1.0
echo "$LD_LIBRARY_PATH"                         # must contain both lib and extraLibs
```

You do **not** need to install a system `libpython3.12`, run `sudo ldconfig`, or rebuild Python with `--enable-shared` — those steps were required by earlier revisions of this guide and are not needed with a bundled runtime.

**`ImportError: _usd_optimize_impl_core built for .cpython-312-x86_64-linux-gnu.so, but this interpreter expects .cpython-3XX-x86_64-linux-gnu.so`**
`usd_optimize.core` checks the interpreter on import, and yours does not match the package's `py_<version>` token. Recreate the venv with the matching Python ([step 2](#2-recommended-create-a-python-virtual-environment)).

**`ImportError: <something>.so: undefined symbol: PyXxx_...`**
Your interpreter does not match the package's `py_<version>` token. Install the matching Python version. Note the converse does not hold: importing only `pxr` under a mismatched interpreter often produces **no** error at all — see the [Python prerequisite](#python--must-match-the-package-name).

**`ImportError: <pxr/something>.so: cannot open shared object file: No such file or directory`**
`LD_LIBRARY_PATH` is missing `lib` or `extraLibs`. Both must be on `LD_LIBRARY_PATH` before the Python process starts so the dynamic linker can resolve transitive `.so`s. Setting them after `import pxr` has already run will not help — restart the interpreter.

**`ImportError: /usr/lib/x86_64-linux-gnu/libstdc++.so.6: version 'GLIBCXX_X.X.XX' not found`**
The `libstdc++.so.6` on the target is older than what the package was built against. Update `libstdc++` (e.g. via `gcc-13`/`libstdc++6` on Ubuntu), or run on a newer base image.

**`ModuleNotFoundError: No module named 'usd_optimize'` or `'pxr'`**
`PYTHONPATH` is missing `python` or `usdpy`. Both directories must be on `PYTHONPATH`.

**`ModuleNotFoundError: No module named 'usd_validation_nvidia'`** (importing `usd_optimize.validators`)
The drop ships the validator rules but not the framework they build on. Install it from PyPI: `pip install usd-validation-nvidia`. Only the validators need it — the core bindings and the [smoke check](#verifying-the-install) work without it.

**`UsdOptimizeCore.getInstance().getOperations()` returns an empty list**
The plugin `.so` files in `lib/` did not load. Confirm the directory is on `LD_LIBRARY_PATH` and that the package matches your platform (`linux-x86_64` vs `linux-aarch64`). Setting `LD_DEBUG=libs` before the Python process will print the linker's search trace and usually pinpoints the missing dependency.
