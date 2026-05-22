# Scene Optimizer — Developer Library

Scene Optimizer is a USD scene optimization library: a broad set of operations for processing and optimizing Universal Scene Description (USD) stages. A published package includes C++ headers, Python bindings, and prebuilt libraries so you can embed Scene Optimizer in your own applications and pipelines—**without installing Omniverse Kit**.

This project is currently not accepting contributions.

## Links

| Topic | Where |
| --- | --- |
| **Quickstart** | [Build from source](#quickstart) |
| **Prebuilt packages** | [Install guides](#prebuilt-packages) |
| **Documentation** | [User manual](https://docs.omniverse.nvidia.com/extensions/latest/ext_scene-optimizer/user-manual.html) |
| **Contribute** | [Open an issue](https://github.com/NVIDIA-Omniverse/scene-optimizer-core/issues/new) |
| **Support** | [GitHub Issues](https://github.com/NVIDIA-Omniverse/scene-optimizer-core/issues) |
| **Security** | [SECURITY.md](SECURITY.md) |
| **Governance** | [Code of Conduct](CODE_OF_CONDUCT.md) |

## Quickstart

After cloning this repository:

```bash
./repo.sh build
./repo.sh test
```

Prerequisites are listed under [Requirements](#requirements). Run `./repo.sh ci format` to check formatting (as CI does). To link the **packaged** libraries into your own build (packman and premake), see [Documentation](#documentation).

## Package Contents

| Directory | Description |
| --- | --- |
| `include/` | C++ public headers (`omni/scene.optimizer/core/`) |
| `lib/` | Prebuilt libraries and Windows import libraries |
| `python/` | Python bindings and modules |
| `usdpy/` | USD Python runtime modules |
| `extraLibs/` | Third-party dependency libraries (Alembic, MaterialX, OpenSubdiv, TBB) |
| `PACKAGE-LICENSES/` | License files for all included components |

## Prebuilt Packages

To consume a published binary drop (headers, prebuilt libraries, and Python bindings) instead of building from source, follow the install guide for your operating system:

| Platform | Install guide |
| --- | --- |
| Windows | [`docs/install-prebuilt-windows.md`](docs/install-prebuilt-windows.md) |
| Linux (x86_64, aarch64) | [`docs/install-prebuilt-linux.md`](docs/install-prebuilt-linux.md) |

## Supported Platforms

| Platform | Architecture |
| --- | --- |
| Windows | x86_64 |
| Linux | x86_64, aarch64 |

## Supported Versions

| Component | Version |
| --- | --- |
| OpenUSD | 25.11 |
| Python | 3.10 / 3.11 / 3.12 (3.12 default for `./repo.sh build`) |
| C++ standard | C++17 |

OpenUSD and Python are fetched automatically via Packman during `./repo.sh build` — no manual install required. The C++ toolchain must still be installed as described under [Requirements](#requirements).

The **`omniverse-scene-optimizer` wheel** produced by `./repo.sh py_package` declares a specific Python **minor** in its tags (see `requires-python` in `tools/pyproject/pyproject.toml` and the `cp3xx` segment in the wheel filename). Set **`PYTHON_BIN`** to a matching interpreter (e.g. `python3.12` for a `cp312` wheel, or `python3.10` / `python3.11` after rebuilding with `--set-token python_ver:…` — see [Building Usd Versions](#building-usd-versions)) and use **`"$PYTHON_BIN" -m pip`** for install and for any **`python -m …`** invocations below.

## Building Usd Versions

Multiple flavors of Usd/Python for building against are supported and are defined in deps/usd_flavors.json
To build against different versions to the default, the build command can be called with additional tokens to set the usd flavor, version, and python version:

```bash
./repo.sh --set-token usd_flavor:usd --set-token usd_ver:25.11 --set-token python_ver:3.10 build -r
```

This builds against usd-25.11 and python 3.10

Note: when changing the build flavors its best practice to start with a clean build:

```bash
./repo.sh --clean
```

## Requirements

### Required Software Dependencies

- [**Git**](https://git-scm.com/downloads): For version control and repository management

- [**Git LFS**](https://git-lfs.com/): For managing large files within the repository (USD assets, textures, and other binary fixtures tracked via `.gitattributes`)

- **(Windows - C++ Only) Microsoft Visual Studio (2019 or 2022)**: Install the latest version from [Visual Studio Downloads](https://visualstudio.microsoft.com/downloads/) (or **Build Tools for Visual Studio**). Ensure the **Desktop development with C++** workload is selected so **MSVC** is installed. A VS install without the C++ toolset is not enough — if `Microsoft.VCToolsVersion.default.txt` is missing under `VC/Auxiliary/Build`, use the **Visual Studio Installer** to add the C++ workload. The build uses a **host-installed** compiler (`msbuild.link_host_toolchain` in `repo.toml`), not Packman `msvc`. If discovery fails, set `msbuild.vs_path` and/or `msbuild.vs_version` in `repo.toml` — see [repo_build toolchains](https://docs.omniverse.nvidia.com/kit/docs/repo_build/latest/docs/toolchains.html).

- **(Windows - C++ Only) Windows SDK**: Install this alongside MSVC. You can find it as part of the Visual Studio Installer. If discovery fails, set `msbuild.winsdk_path` in `repo.toml` — see [repo_build toolchains](https://docs.omniverse.nvidia.com/kit/docs/repo_build/latest/docs/toolchains.html).

- **(Windows, non-English locale) `PYTHONUTF8=1`**: Repo tooling reads UTF-8 configuration files such as `tools/repoman/repo_tools.toml`, but Python on Windows decodes text files using the system ANSI code page (e.g. **CP949** on Korean, **CP932** on Japanese, **GBK** on Simplified Chinese installs). The mismatch surfaces as a `UnicodeDecodeError` during `repo.bat build`. Enable Python's UTF-8 mode to force UTF-8 for all I/O:

  ```cmd
  :: current session (cmd.exe)
  set PYTHONUTF8=1

  :: persist for new shells
  setx PYTHONUTF8 1
  ```

  ```powershell
  # current session (PowerShell)
  $env:PYTHONUTF8 = "1"
  ```

  English / US-locale Windows defaults to CP1252, which overlaps with UTF-8 for ASCII bytes and so usually hides this bug — `PYTHONUTF8=1` is harmless there and a safe default. See [PEP 540](https://peps.python.org/pep-0540/) for the underlying mechanism.

- **(Linux) build-essential**: A package that includes `make`, `gcc`, and other essential tools for building applications. For Ubuntu, install with:

  ```bash
  sudo apt-get install build-essential
  ```

  - **`patchelf` (Linux, only if you run `./repo.sh py_package`):** `auditwheel repair` needs it (`ValueError: Cannot find required utility 'patchelf'` if missing). Ubuntu: `sudo apt-get install patchelf`.

  - **Outbound HTTPS / PyPI (only if you run `./repo.sh py_package`):** `tools/pyproject/pybuild.sh` creates `_build/host-deps/py_package_venv/` and pip-installs Poetry, auditwheel, and related tools from PyPI. Air-gapped or proxy-only environments must supply a reachable index or mirrors; the README commands assume normal PyPI access.

  - **`py_package` venv recovery:** If that bootstrap fails partway, the `_build/host-deps/py_package_venv` directory may exist but lack a working toolchain; the wrapper then skips reinstall and might pick up an incompatible system Poetry. Fix: remove the directory and rerun `./repo.sh py_package` (`rm -rf _build/host-deps/py_package_venv`).

  > **(Linux x86_64) C++11 ABI**
  > The C++11 ABI is enabled for x86_64 builds via `premake.linux_x86_64_cxx_abi` in `repo.toml`.

All other build-time dependencies (USD, Python, third-party libraries, **premake**, etc.) are pulled automatically via Packman when you run `./repo.sh build`. See [Supported Versions](#supported-versions) for the specific versions.

## Operations

| Operation | Description |
| --- | --- |
| Auto UV Unwrap | Generate texture coordinates using UV unwrap methods with low distortion |
| Box Clip | Clip meshes to a user-defined axis-aligned bounding box |
| Compute Extents | Compute and author extent attributes for mesh prims |
| Compute Pivot | Place the parent transform at the center of the bounding box of the target geometry |
| Count Vertices | Create a report of prims with excessive vertex counts |
| Decimate Meshes | Reduce mesh polygon count while preserving overall shape |
| Deduplicate Geometry | Replace duplicate meshes with instanced references to reduce memory usage |
| Delete Hidden Prims | Delete prims that are not visible in the scene |
| Delete Prims | Delete prims from a stage |
| Dice Meshes | Dice meshes into a grid of smaller uniform pieces |
| Edit Stage Metrics | Change the metersPerUnit and/or upAxis of a stage |
| Find Coincident Geometry | Find geometry that occupies the same positional space in a scene |
| Find Flat Hierarchies | Find prims that have more than a specified number of children |
| Find Occluded Meshes | Find meshes that are occluded by others and can be safely removed |
| Find Overlapping Meshes | Find interfering geometry in a stage |
| Fit Primitives | Replace complex geometry with best-fit primitive shapes |
| Flatten Hierarchy | Remove redundant Xforms from a stage to reduce prim count |
| Generate Normals | Generate vertex normals for meshes |
| Generate Projection UVs | Generate texture coordinates using projection methods |
| Generate Scene | Generate a USD stage for benchmarking and testing |
| Manifold Meshes | Make meshes into manifold meshes |
| Merge Static Meshes | Replace multiple meshes sharing common properties with a single merged mesh |
| Merge Vertices | Merge vertices closer than a given tolerance |
| Mesh Cleanup | Apply various cleanups to meshes (merge vertices, remove degenerate faces, etc.) |
| Optimize Materials | Deduplicate identical materials and consolidate bindings |
| Optimize Primvars | Simplify and index primvar data to reduce memory usage |
| Optimize Skeleton Roots | Merge all meshes attached to a skeleton to improve performance |
| Optimize Time Samples | Remove redundant time samples from attributes throughout a stage |
| Organize Prototypes | Reparent internal scene-graph instance prototypes under a common root |
| Primitives to Meshes | Convert geometric primitives to mesh representations |
| Prune Leaves | Find and prune leaf grouping primitives from a stage |
| Python Script | Execute a user-defined Python script on a USD stage |
| Remesh Meshes | Remesh input UsdGeom mesh primitives |
| Remove Attributes | Remove attributes from prims |
| Remove Prims | Identify and remove prims from the stage |
| Remove Small Geometry | Identify and remove small and/or degenerate geometry from a stage |
| Remove Unused UVs | Find and remove unused UV primvar sets from meshes |
| RTX Mesh Count | Count the number of RTX meshes in the stage and how many are unique |
| Shrinkwrap | Convert meshes to a level set volume and extract a watertight mesh (OpenVDB-based) |
| Sparse Meshes | Analyze sparse meshes and suggest optimizations |
| Split Meshes | Split meshes containing multiple disjoint mesh descriptions |
| Stats | Collect and display statistics about the contents of a USD stage |
| Subdivide Meshes | Subdivide meshes to increase geometric detail |
| Triangulate Meshes | Triangulate mesh faces |
| Utility Function | Helper functions such as deinstancing, unbinding materials, and setting instanceable |

## Performance Validators

Scene Optimizer integrates with the [`omniverse-asset-validator`](https://pypi.org/project/omniverse-asset-validator/) PyPI package to expose its performance checks as asset-validator rules under the `Performance` category. Each rule wraps the analysis mode of a Scene Optimizer operation. Most register by default; a few are opt-in because they're slow on large stages — pass `include_expensive=True` to `register_all()` to register the slow ones too. The authoritative rule lists live in `source/core/python/omni/scene/optimizer/validators/_plugin.py` (`_default_rule_classes()` and `_expensive_rule_classes()`).

`omniverse-asset-validator` is pulled in automatically as a runtime dependency of the `omniverse-scene-optimizer` wheel.

### Programmatic use

Use the **same Python** and, on Linux, the **`PYTHONPATH` / loader alignment** from [CLI use](#cli-use) so **`pxr` and Scene Optimizer’s C++ core bind the same `libusd`**.

```python
from omni.scene.optimizer.validators import register_all
from omni.asset_validator import ValidationEngine
from pxr import Usd

register_all()
# Use your own USD path; this repo ships a small fixture you can open as-is:
results = ValidationEngine().validate(Usd.Stage.Open("source/tests/data/simpleFourCubes.usda"))
for issue in results.issues():
    print(issue.severity, issue.rule.__name__, issue.message)
```

### CLI use

**From a source checkout**, prefer **`tools/perf_validators/run.sh`** (see the **`run-validators`** skill): it aligns `PYTHONPATH` and loader paths with the `./repo.sh build` tree, calls `register_all()` directly (no wheel entry-point plumbing), and avoids several footguns documented below.

**Raw `omni_asset_validate`** is still useful for pipelines that rely on upstream's CLI behavior after a **`pip install`**.

`omni_asset_validate` discovers Scene Optimizer’s plugin through **`importlib.metadata` distribution entry points**. The declarator lives under `[project.entry-points."omni.asset_validator"]` in `tools/pyproject/pyproject.toml` and is stamped onto the **`omniverse-scene-optimizer` wheel**.

**Installing the Python package is required.** Putting this repository on `PYTHONPATH` is enough for `python -c "from omni.scene.optimizer.validators …"` imports, but it **does not** publish entry-point metadata, so the CLI still sees only `'omni.asset_validator:DefaultPlugin'`, emits no `SceneOptimizer*` rules, logs no warning that Scene Optimizer is missing—and `-c Performance` fails with “invalid choice” because upstream only knows its built-in categories until our plugin registers the `Performance` category.

Minimal install from source (libraries first, wheel second). **Pick `PYTHON_BIN`** so its minor version matches the wheel under `_build/packages/` (the `cp3xx` in the filename must agree — see [Supported Versions](#supported-versions)):

```bash
./repo.sh build
./repo.sh py_package                                          # emits _build/packages/omniverse_scene_optimizer-*.whl
PYTHON_BIN=python3.12                                         # e.g. python3.10 / python3.11 if you rebuilt py_package for that minor
"$PYTHON_BIN" -m pip install _build/packages/omniverse_scene_optimizer-*.whl
```

Use the **same Python environment** for `omni_asset_validate` below (e.g. activate the venv you installed into, or ensure the `omni_asset_validate` on `PATH` came from **`$PYTHON_BIN`**).

**Linux — align `libusd` / `pxr` after `pip install`:** The wheel bundles Scene Optimizer and repaired dependency `.so` files under `omniverse_scene_optimizer.libs/`, but **`usd-exchange`** (a runtime dependency of the wheel) can install **`pxr` linked to a different auditwheel-mangled `libusd_*.so`**. Then Python and the C++ core disagree on `UsdUtilsStageCache`, every rule fails with a stage-ID / cache error. Until packaging deduplicates that stack, **`pip install` from a local `./repo.sh py_package` build should export the dev-tree USD you built against**:

```bash
# From repo root. Set PLATFORM to your _build/ child (examples: linux-x86_64, linux-aarch64).
PLATFORM=linux-x86_64
SO_ROOT="$PWD/_build/${PLATFORM}/release"
export PYTHONPATH="$SO_ROOT/python:$PWD/_build/target-deps/usd/release/lib/python:${PYTHONPATH:-}"
export LD_LIBRARY_PATH="$SO_ROOT/lib:$SO_ROOT/extraLibs:${LD_LIBRARY_PATH:-}"
```

Repair still reduces **loader** coupling for bundled Scene Optimizer bits; **`PYTHONPATH` alignment is separate** — see `.agents/skills/validators/SKILL.md` § *CLI invocation*. Unrepaired wheels need dev-tree loader paths plus all core `.so`s from `_build/` (that skill spells out the unrepaired-path matrix).

**Second gate:** once the wheel is installed, the asset-validator’s `PluginManager` uses `OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS` as an allow-list—which plugins to instantiate among those **discovered** via metadata:

```bash
export OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS="omni.asset_validator:DefaultPlugin,omni.scene.optimizer.validators:SceneOptimizerValidatorPlugin"
```

Smoke-check discovery with the **same interpreter** as **`$PYTHON_BIN`** / `omni_asset_validate` (expect at least one `SceneOptimizer*` line). **Upstream quirk:** `omni_asset_validate --listChecks` may exit non-zero even when listing succeeds; **`grep` exiting 0** still means discovery worked. Prefer wiring failure off the **`grep`** result instead of trusting only the CLI exit code under `set -e`.

```bash
omni_asset_validate --listChecks | grep SceneOptimizer || { echo >&2 "No SceneOptimizer rules — install omniverse_scene_optimizer wheel or inspect LOG for plugin discovery"; exit 1; }
```

Example runs (**`simpleFourCubes.usda`** is a repo fixture; swap in your `.usd` / `.usdz` path):

```bash
ASSET=source/tests/data/simpleFourCubes.usda
omni_asset_validate "$ASSET" --json-output issues.json    # structured per-prim findings
omni_asset_validate "$ASSET" --csv-output issues.csv       # flat per-issue rows
omni_asset_validate -c Performance "$ASSET"              # only Scene Optimizer's Performance rules
```

For the full runbook (file logging, adding new validators, `register_all()` vs entry-point semantics), see `.agents/skills/validators/SKILL.md`.

## Documentation

The **[Scene Optimizer user manual](https://docs.omniverse.nvidia.com/extensions/latest/ext_scene-optimizer/user-manual.html)** includes the C++ and Python API references, the developer guide (including **packman** and **premake** integration for C++), and per-operation details.

## License

Scene Optimizer Core is licensed under the [Apache License, Version 2.0](LICENSE).

Copyright (c) 2022-2026, NVIDIA CORPORATION.

For third-party and bundled components in a published package, see `PACKAGE-LICENSES/`.
