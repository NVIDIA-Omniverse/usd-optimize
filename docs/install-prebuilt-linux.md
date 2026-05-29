# Installing the Prebuilt Scene Optimizer Package on Linux

This guide is for **consumers of a published `scene_optimizer_core` package** on Linux — for example, a drop named like:

```
scene_optimizer_core_usd_<usd_ver>_py_<py_ver>@<version>.<platform>.release
```

where `<platform>` is `linux-x86_64` or `linux-aarch64`. The two arches share the same layout and the same prerequisites; only the contents of the `.so` files differ.

If you are building Scene Optimizer from source, see the top-level [README](../README.md) and use `repo.sh build` / `repo.sh test` instead. The published package does **not** include `repo.sh`, source code, or test fixtures — only headers, prebuilt libraries, and Python bindings.

## Package Layout

| Directory | Purpose |
| --- | --- |
| `include/` | C++ public headers (`omni/scene.optimizer/core/`) |
| `lib/` | Prebuilt shared libraries (`libomni.scene.optimizer.core.so`, plugin `.so` files, `operation_mapping.json` — deprecated-name aliases for `map_config()`, not the list of operations) |
| `python/` | Python bindings (`omni.scene.optimizer.core`) and bundled tests under `python/tests/test.python/` |
| `usdpy/` | OpenUSD Python runtime modules (`pxr.*`) — the package brings its own USD |
| `extraLibs/` | Third-party runtime libraries (Alembic, MaterialX, OpenSubdiv, TBB) |

Two notable differences from the Windows drop:

- There is **no `python` interpreter in the package** — you must supply your own that matches the package's Python ABI.
- The Linux drop **does not bundle `libpython3.X.so.1.0`**. The bundled `pxr` modules link against it dynamically, so it must come from the Python you install (see below).

## Prerequisites

### Python — must match the package name

The Python version is encoded in the package directory name (`py_3.12` in the example above). The bundled USD `.so` modules are linked against `libpython3.12.so.1.0`, so loading them under any other Python (3.10, 3.11, 3.13, …) fails at import time with an undefined-symbol or `cannot open shared object file: libpython3.12.so.1.0` error.

This is a hard ABI requirement. Install the matching Python *with the shared library*. On Ubuntu/Debian, the [deadsnakes PPA](https://launchpad.net/~deadsnakes/+archive/ubuntu/ppa) is the easiest source for older or newer Python versions than your distro ships:

```bash
sudo add-apt-repository ppa:deadsnakes/ppa
sudo apt-get update
sudo apt-get install python3.12 python3.12-venv libpython3.12
```

`libpython3.12` is the package that provides `libpython3.12.so.1.0` on deadsnakes — installing only `python3.12` (the interpreter) is not enough; loading the bundled USD modules will fail.

> **The package that ships `libpython3.X.so.1.0` varies by where Python comes from:**
>
> | Source | Package providing `libpython3.X.so.1.0` |
> | --- | --- |
> | deadsnakes PPA (Ubuntu/Debian) | `libpython3.X` (sometimes pulled in by `python3.X-dev`) |
> | Stock Ubuntu/Debian (system Python) | `libpython3.X` (e.g. `libpython3.12` on Ubuntu 24.04) |
> | pyenv | none — must rebuild with `PYTHON_CONFIGURE_OPTS="--enable-shared"` |
> | conda / miniconda | included in the `python` package; usually under `$CONDA_PREFIX/lib` |
>
> Regardless of source, verify the shared library actually landed before troubleshooting Scene Optimizer:
>
> ```bash
> # Index of what ldconfig remembers — can be stale; see interpretation below.
> ldconfig -p | grep libpython3.12.so.1.0
> PYLIBDIR="$(python3.12 -c 'import sysconfig; print(sysconfig.get_config_var("LIBDIR"))')"
> ls "$PYLIBDIR" | grep libpython3.12
> ```
>
> **How to interpret this:** Finding `libpython3.12.so.1.0` under `PYLIBDIR` shows the interpreter has the shared library. Three common situations:
>
> 1. **`ls` does not show the `.so`** — install or rebuild Python with `--enable-shared` (see the table above).
> 2. **`ls` shows the `.so` in a distro default library directory** (typically under `/usr/lib` or `/lib`, including multiarch paths such as `/usr/lib/x86_64-linux-gnu` on Debian/Ubuntu), but **`ldconfig -p` is still empty** — the linker cache is stale. The package post-install scripts normally refresh it via `ldconfig`; after an interrupted `apt` run or partial upgrade that step may never have run. Run **`sudo ldconfig`** once, then grep `ldconfig -p` again. You do **not** need an extra reinstall or redundant `PYLIBDIR` entries on `LD_LIBRARY_PATH`.
> 3. **`ls` shows the `.so` only under an interpreter-specific prefix** (pyenv install dir, conda env, …) — that location is typically **outside** the default `ldconfig` search path even when fresh. Append that `PYLIBDIR` to `LD_LIBRARY_PATH` in [step 3](#3-set-environment-variables) below (conda activation usually handles this automatically).

If you prefer a distro-neutral install, [pyenv](https://github.com/pyenv/pyenv) works on any Linux: `PYTHON_CONFIGURE_OPTS="--enable-shared" pyenv install 3.12`. The `--enable-shared` flag is required so that `libpython3.12.so.1.0` is built; pyenv defaults to a static interpreter, which the package cannot link against. After install, the `.so` lives under `~/.pyenv/versions/3.12.<patch>/lib/`, which is **not** on the default `ldconfig` path — add it to `LD_LIBRARY_PATH` alongside `lib` and `extraLibs`.

### C++ runtime (only if you link against the C++ libraries)

You only need a host C++ toolchain on the target machine if you are linking your own C++ application against `libomni.scene.optimizer.core.so`. Pure-Python consumers can skip this.

The Linux x86_64 build uses the C++11 ABI (`premake.linux_x86_64_cxx_abi` in `repo.toml`), so applications linking against the package must be built with the same ABI. A reasonably recent `libstdc++.so.6` is also required at runtime — symptoms of an old one surface as `version 'GLIBCXX_X.X.XX' not found` from the dynamic linker.

## Installing

### 1. Extract the package

Place the unpacked directory anywhere — for the rest of this guide we assume it lives at:

```bash
PACKAGE_ROOT=/path/to/scene_optimizer_core_usd_25.11_py_3.12@<version>.linux-x86_64.release
```

### 2. (Recommended) Create a Python virtual environment

A venv keeps Scene Optimizer's `PYTHONPATH` tweaks isolated from any other Python project on the machine:

```bash
python3.12 -m venv "$PACKAGE_ROOT/.venv"
source "$PACKAGE_ROOT/.venv/bin/activate"
```

Adjust the interpreter name to wherever your matching Python lives (`which python3.12` will print it).

### 3. Set environment variables

Two paths must be exported every session:

| Variable | Why |
| --- | --- |
| `PYTHONPATH` += `python:usdpy` | Lets the interpreter find both `omni.scene.optimizer.*` and `pxr.*` |
| `LD_LIBRARY_PATH` += `lib:extraLibs` | Lets the dynamic linker resolve transitive shared-object dependencies (USD, TBB, Alembic, plugin `.so`s) |

bash/zsh:

```bash
export PACKAGE_ROOT=/path/to/scene_optimizer_core_usd_25.11_py_3.12@<version>.linux-x86_64.release
export PYTHONPATH="$PACKAGE_ROOT/python:$PACKAGE_ROOT/usdpy${PYTHONPATH:+:$PYTHONPATH}"
export LD_LIBRARY_PATH="$PACKAGE_ROOT/lib:$PACKAGE_ROOT/extraLibs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
```

The `${VAR:+:$VAR}` form avoids appending a trailing colon when the variable is unset, which the dynamic linker would otherwise interpret as the current working directory.

To make the settings durable, append the exports to the venv's `bin/activate` script, or to your `~/.bashrc` / `~/.zshrc` if Scene Optimizer is the only Python package you use in that shell.

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

```bash
"$PACKAGE_ROOT/.venv/bin/python" smoke_check.py
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

- **`run_discover.py` imports every `test_*.py` before unittest runs.** If **any** import fails, it prints all import failures to stderr and **`sys.exit(1)` without running tests** — so a typical release sees validator import errors only and **executes zero tests**, not a long report of fixture misses. Only after every module imports successfully does the runner execute tests; **many** of those tests expect USD fixtures under `../data`, which exists in the source tree but not in the published package.

The self-contained tests in `test_core_python_bindings.py` (`test_executionContext`, `test_executionContext_reportPath_roundtrip`, `test_executionContext_reportPath_survives_executeOperation`, `test_sceneOptimizerCore`, `test_operation`) are equivalent to the smoke-check above.

## Troubleshooting

**`ImportError: libpython3.12.so.1.0: cannot open shared object file: No such file or directory`**
The interpreter cannot locate the ABI-matching shared `libpython`. Work through [the verification snippet under Python prerequisites](#python--must-match-the-package-name): compare `PYLIBDIR`/`ls` to `ldconfig -p`.

- If **`ls` finds `libpython3.12.so.1.0` under `/usr/lib` or `/lib` (including multiarch subdirs)** but **`ldconfig -p` does not**, refresh **`sudo ldconfig`** and retry — an empty grep is often a stale linker cache after a skipped or failed apt trigger, not a missing package.
- If **`ls` finds the `.so` only under pyenv**, add `~/.pyenv/versions/3.12.<patch>/lib` to **`LD_LIBRARY_PATH`** (after confirming you built with `--enable-shared`; see Prerequisites).
- If **`ls` finds the `.so` only under conda**, activate that env (`$CONDA_PREFIX/lib`).
- Only if **`ls` does not find the `.so`**, install the shared library package (Ubuntu/Debian: **`sudo apt-get install libpython3.12`** for the matching interpreter) or rebuild with `--enable-shared`.

See the [Python prerequisite](#python--must-match-the-package-name) for the package-name table per Python source.

**`ImportError: <something>.so: undefined symbol: PyXxx_...`**
Your interpreter does not match the package's `py_<version>` token. Install the matching Python version.

**`ImportError: <pxr/something>.so: cannot open shared object file: No such file or directory`**
`LD_LIBRARY_PATH` is missing `lib` or `extraLibs`. Both must be on `LD_LIBRARY_PATH` before the Python process starts so the dynamic linker can resolve transitive `.so`s. Setting them after `import pxr` has already run will not help — restart the interpreter.

**`ImportError: /usr/lib/x86_64-linux-gnu/libstdc++.so.6: version 'GLIBCXX_X.X.XX' not found`**
The `libstdc++.so.6` on the target is older than what the package was built against. Update `libstdc++` (e.g. via `gcc-13`/`libstdc++6` on Ubuntu), or run on a newer base image.

**`ModuleNotFoundError: No module named 'omni.scene.optimizer'` or `'pxr'`**
`PYTHONPATH` is missing `python` or `usdpy`. Both directories must be on `PYTHONPATH`.

**`ModuleNotFoundError: No module named 'omni.asset_validator'`** (common when running bundled `run_discover.py`)
The `test_validators_*.py` modules require PyPI **`omniverse-asset-validator`** (`pip install omniverse-asset-validator`). Without it, `run_discover.py` fails during its import phase and runs no tests. Prefer `test_core_python_bindings.py` or the [smoke check](#verifying-the-install) for package verification alone.

**`SceneOptimizerCore.getInstance().getOperations()` returns an empty list**
The plugin `.so` files in `lib/` did not load. Confirm the directory is on `LD_LIBRARY_PATH` and that the package matches your platform (`linux-x86_64` vs `linux-aarch64`). Setting `LD_DEBUG=libs` before the Python process will print the linker's search trace and usually pinpoints the missing dependency.
