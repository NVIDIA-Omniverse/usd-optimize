# Usd Optimize — Developer Library

Usd Optimize is a USD scene optimization library: a broad set of operations for processing and optimizing Universal Scene Description (USD) stages. A published package includes C++ headers, Python bindings, and prebuilt libraries so you can embed Usd Optimize in your own applications and pipelines — **without installing Omniverse Kit**.

> This project is currently not accepting contributions.

## Documentation

Detailed docs live alongside the source:

| Component | Where |
| --- | --- |
| **Kit extension user manual** (Scene Optimizer inside Omniverse Kit) | [docs.omniverse.nvidia.com](https://docs.omniverse.nvidia.com/extensions/latest/ext_scene-optimizer/user-manual.html) |
| **Operations catalog** (45+ ops) | [`docs/operations.rst`](docs/operations.rst) |
| **Choosing operations** | [`docs/choosing-operations.rst`](docs/choosing-operations.rst) |
| **Performance validators** | [`docs/performance-validators.rst`](docs/performance-validators.rst) |
| **CLI** | [`docs/cli.rst`](docs/cli.rst) |
| **Developer guide** (packman/premake, platform build notes) | [`docs/developer.rst`](docs/developer.rst) |

Support: [GitHub Issues](https://github.com/NVIDIA-Omniverse/usd-optimize/issues) · Security: [SECURITY.md](SECURITY.md) · Governance: [Code of Conduct](https://github.com/NVIDIA-Omniverse/usd-optimize/blob/HEAD/CODE_OF_CONDUCT.md)

## Quickstart

After cloning this repository (see [Requirements](#requirements) for the C++ toolchain):

```bash
./repo.sh build
./repo.sh test
```

`./repo.sh ci format` checks formatting the way CI does. OpenUSD, Python, premake, and other build-time dependencies are fetched automatically via Packman during `build` — no manual install required.

To consume a **published binary drop** instead of building from source, follow the install guide for [Windows](docs/install-prebuilt-windows.md) or [Linux (x86_64, aarch64)](docs/install-prebuilt-linux.md).

## Operations

Usd Optimize ships 45+ operations — mesh cleanup and decimation, geometry/material deduplication, hierarchy flattening, UV generation, remeshing, and more. Each operation declares typed arguments and can run individually or chained into a JSON *stack*.

See the full catalog with per-operation arguments and JSON examples in **[`docs/operations.rst`](docs/operations.rst)**, and guidance on *which* to apply for a given goal in **[`docs/choosing-operations.rst`](docs/choosing-operations.rst)**.

> **Note:** A few operations (`Python Script`, `Delete Hidden Prims`, `Remove Untyped Prims`, `Move Materials`) are Python-plugin operations — they run via the `usd-optimize` wheel/bindings but not from the standalone `usdOptimize` CLI, which does not host a Python interpreter. See [`docs/cli.rst`](docs/cli.rst).

## Performance Validators

Usd Optimize integrates with the [`usd-validation-nvidia`](https://pypi.org/project/usd-validation-nvidia/) PyPI package to expose its performance and geometry checks as validation rules under the `Usd:Performance` and `Omni:Geometry` categories (each rule wraps the analysis mode of an operation). With the `usd-optimize` wheel installed, the rules register themselves through a package entry point — no `register_all()` needed:

```python
from usd_validation_nvidia import ValidationEngine
from pxr import Usd

engine = ValidationEngine()  # rules auto-register on import of usd_validation_nvidia
results = engine.validate(Usd.Stage.Open("source/tests/data/simpleFourCubes.usda"))
for issue in results.issues():
    print(issue.severity, issue.rule.__name__, issue.message)
```

From a source checkout (no installed wheel), call `register_all()` yourself, importing `usd_validation_nvidia` first:

```python
from usd_validation_nvidia import ValidationEngine
from usd_optimize.validators import register_all

register_all()
```

For running the validators from a source checkout, prefer **`tools/validators/run.sh`** (see the [`run-validators`](.agents/skills/run-validators/SKILL.md) skill) — it aligns `PYTHONPATH`/loader paths with the build tree and avoids the CLI footguns. Full details, including raw `nvidia_usd_validate` usage and the `libusd`/`pxr` alignment matrix, are in **[`docs/performance-validators.rst`](docs/performance-validators.rst)** and the [`run-validators`](.agents/skills/run-validators/SKILL.md) skill.

## Prebuilt Packages

Prefer not to build from source? Consume a **published binary drop** — C++ headers, prebuilt libraries, and Python bindings — via the per-OS install guide:

| Platform | Install guide |
| --- | --- |
| Windows | [`docs/install-prebuilt-windows.md`](docs/install-prebuilt-windows.md) |
| Linux (x86_64, aarch64) | [`docs/install-prebuilt-linux.md`](docs/install-prebuilt-linux.md) |

## Package Contents

| Directory | Description |
| --- | --- |
| `include/` | C++ public headers (`usd_optimize/core/`) |
| `lib/` | Prebuilt libraries and Windows import libraries |
| `bin/` | The `usdOptimize` command-line tool |
| `python/` | Python bindings and modules |
| `usdpy/` | USD Python runtime modules |
| `extraLibs/` | Third-party dependency libraries (MaterialX, TBB; USD 25.x drops additionally carry Alembic and OpenSubdiv) |
| `config_presets/` | Ready-made operation stacks for `usdOptimize -c` |
| `docs/` | The documentation set, including the per-OS install guides |
| `.agents/` | Task-specific skill files (`.agents/skills/<name>/SKILL.md`) |
| `PACKAGE-LICENSES/` | License files for all included components |

## Supported Platforms & Versions

| Platform | Architecture |
| --- | --- |
| Windows | x86_64 |
| Linux | x86_64, aarch64 |

| Component | Version |
| --- | --- |
| OpenUSD | Varies by drop — see the `usd_<ver>` token in the package name, or call `Usd.GetVersion()` |
| Python | Varies by drop — see the `py_<ver>` token in the package name |
| C++ standard | C++17 |

Source builds use the `usd_ver` / `python_ver` tokens in `repo.toml`; supported values are listed in `deps/usd_flavors.json`. Published drops are built once per USD flavor, so a drop's own package name is authoritative for both — this file ships unchanged into every flavor and cannot name a single version.

The **`usd-optimize` wheel** produced by `./repo.sh py_package` declares a specific Python **minor** in its tags (see `requires-python` in `tools/pyproject/pyproject.toml` and the `cp3xx` segment in the wheel filename). It is a `cp312` wheel, so set **`PYTHON_BIN`** to a `python3.12` interpreter and use **`"$PYTHON_BIN" -m pip`** for install and for any **`python -m …`** invocations.

<details>
<summary>Building against other USD / Python versions</summary>

The wheel is always built against **USD 26.08** — matching the `usd-exchange` / `usd-validation-nvidia` runtime it binds from PyPI — regardless of the repository default USD version. The pinned version lives in `tools/pyproject/wheel_usd_versions.json`, which holds exactly one version because the wheel filename carries no USD tag; `./repo.sh py_package` fails fast if the build tree targets another version. Build and smoke-test it locally with:

```bash
./repo.sh --set-token usd_ver:26.08 build && ./repo.sh py_package --test
```

`--test` installs the freshly built wheel into a throwaway virtualenv and runs an import + operation smoke test.

Three USD versions are supported — **25.05**, **25.11** and **26.08**; the default for `./repo.sh build` is the `usd_ver` token in `repo.toml`. All supported flavors use Python 3.12. Flavors are defined in `deps/usd_flavors.json`. To build against a non-default version, pass the flavor/version tokens:

```bash
./repo.sh --set-token usd_flavor:usd --set-token usd_ver:26.08 build -r
```

When changing flavors, start clean: `./repo.sh build --rebuild`.

</details>

## Requirements

- [**Git**](https://git-scm.com/downloads) and [**Git LFS**](https://git-lfs.com/) (LFS tracks USD assets, textures, and other binary fixtures via `.gitattributes`).
- **(Windows, C++)** Visual Studio 2019/2022 (or Build Tools) with the **Desktop development with C++** workload, plus the **Windows SDK**.
- **(Linux)** `build-essential` (`sudo apt-get install build-essential`).

All other build-time dependencies (USD, Python, third-party libraries, premake) are pulled via Packman during `./repo.sh build`.

Platform-specific build notes — Windows host-toolchain discovery, `PYTHONUTF8` on non-English locales, and the extra `patchelf`/PyPI requirements of `./repo.sh py_package` on Linux — are in [`docs/developer.rst`](docs/developer.rst).

## License

Usd Optimize Core is licensed under the [Apache License, Version 2.0](https://github.com/NVIDIA-Omniverse/usd-optimize/blob/HEAD/LICENSE). Prebuilt drops carry the same text at `PACKAGE-LICENSES/LICENSE`.

Copyright (c) 2022-2026, NVIDIA CORPORATION.

For third-party and bundled components in a published package, see `PACKAGE-LICENSES/`.
