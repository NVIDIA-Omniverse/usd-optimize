---
name: validators
description: Reference for Scene Optimizer's validator infrastructure (registration, CLI, logging, REQUIRES_MESH cache). Do not use for ad-hoc validation runs — use run-validators instead.
version: "1.0.0"
allowed-tools: Shell
metadata:
  author: NVIDIA Corporation
  tags: [validation, performance, reference]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Performance Validators

Scene Optimizer ships a set of `omniverse-asset-validator` rules under the
**Performance** category that wrap analysis-mode operations. Most are
registered by default; a few are opt-in because they're slow on large
stages.

## What this skill covers

This skill is the *reference doc* for validator infrastructure. For the day-to-day "validate this asset" workflow, use `run-validators`; for reading the report, use `interpret-validators`. Search this doc for keywords like `REQUIRES_MESH`, `cache`, `entry-point`, `allow-list`, `register_all`, `analysisMode`, `libusd`, `auditwheel` to jump.

- **Rule families** — current shape of the rule list.
- **Programmatic invocation** — `register_all()` + `ValidationEngine().validate(stage)`.
- **Optional helper wrapper path** — `tools/perf_validators/run.{sh,bat}` when the selected SO environment or build checkout provides it.
- **CLI invocation** — `omni_asset_validate` setup, with caveats around `auditwheel`, `LD_LIBRARY_PATH`, the `libusd` alignment requirement.
- **Logging to file** — CSV / JSON / log sinks.
- **Performance behavior** — analysis-result cache and the `REQUIRES_MESH` short-circuit (mesh-less stages skip mesh-only rules silently — search for `REQUIRES_MESH` to find this).
- **Gotcha — entry-point allow-list** — `OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS` requirement.
- **Spec compliance** — entry-point declaration matches the AVEP spec.
- **Running validator tests** — `./repo.sh test -s python`.
- **Adding a new performance validator** — recipe for new rule classes.

The authoritative lists live in `_plugin.py` — see `_default_rule_classes()`
and `_expensive_rule_classes()`. Avoid duplicating either list in this doc;
the source is the single source of truth.

## Rule families (current shape)

| Group | Backing op(s) |
|---|---|
| Mesh-cleanup family | `meshCleanup` |
| Duplicates / structure | `deduplicateGeometry`, `deduplicateHierarchies`, `optimizeMaterials`, `findCoincidingGeometry`, `pruneLeaves`, `findFlatHierarchies`, `flattenHierarchy`, `removePrims` |
| Renderer / performance | `rtxMeshCount`, `countVertices`, `fitPrimitives`, `sparseMeshes`, `optimizeTimeSamples`, `removeSmallGeometry`, `computeExtents` |
| Geometry checks | `findOverlappingMeshes`, `findOccludedMeshes` |
| Misc | `generateNormals`, `removeUnusedUVs`, `optimizePrimvars` |

Which rules are default vs. opt-in is authoritative in `_default_rule_classes()` / `_expensive_rule_classes()` in `_plugin.py`; check there rather than this table.

All rule classes have the `SceneOptimizer` prefix (e.g. `SceneOptimizerNonManifoldChecker`).

Source: `source/core/python/omni/scene/optimizer/validators/<name>.py`. Tests: `source/tests/test.python/test_validators_*.py`.

## Programmatic invocation

```python
from omni.scene.optimizer.validators import register_all
from omni.asset_validator import ValidationEngine
from pxr import Usd

register_all()                         # default rules only

results = ValidationEngine().validate(Usd.Stage.Open("asset.usd"))
for issue in results.issues():
    print(issue.severity, issue.rule.__name__, issue.message)
```

`register_all()` is idempotent.

## Optional helper wrapper path

For ad-hoc validation and comparing runs (e.g. before/after an operation
parameter change) from a source checkout, use `tools/perf_validators/run.sh`
(Linux) or `tools/perf_validators/run.bat` (Windows) only when those wrappers
are present. They set up the build-tree library paths and invoke
`perf_validators.py` under the bundled Python. **Prefer this over the
`omni_asset_validate` CLI** for dev-tree runs (see caveats below).

> **Dev-tree only.** Both wrappers point at `_build/$platform/$config/...`
> from the local checkout. They assume `./repo.sh build` has already run.
> Do not assume Kit or standalone SO installs provide them, and do not use
> them for a pip-installed deployment — for that path, see *CLI invocation*
> below.

```bash
# Run against an asset, write a small summary JSON
tools/perf_validators/run.sh run path/to/asset.usd --summary before.json

# ...edit the asset or change validator/op defaults, rebuild...

tools/perf_validators/run.sh run path/to/asset.usd --summary after.json

# Diff: per-rule counts + total + validate-time delta
tools/perf_validators/run.sh compare before.json after.json
```

`run` also accepts `--csv` (per-issue CSV, asset-validator format), `--json` (full
asset-validator JSON), `--fix <path>` (run `IssueFixer` on Scene Optimizer issues
and write the fixed stage there). The `--summary` JSON is the format `compare`
reads — it is purpose-built for diffing and is independent of asset-validator's
JSON shape.

The wrapper requires `./repo.sh build` to have run; it auto-installs
`omniverse-asset-validator` into the bundled Python on first use.

## CLI invocation

The `omni_asset_validate` CLI ships with `omniverse-asset-validator`. Using it
to drive Scene Optimizer's rules requires the `omniverse-scene-optimizer`
wheel.

**Checkout default:** Prefer **`tools/perf_validators/run.{sh,bat}`** (same as *Optional helper wrapper path* above)—it aligns `PYTHONPATH` / loaders with `./repo.sh build`.

**Alternative — Path-B CLI + `pip install`:** Needed when callers require the upstream `omni_asset_validate` entry binary and discovery via wheel metadata. Several gotchas below.

### Building and installing the wheel

`omniverse-scene-optimizer` is **not on PyPI**. `./repo.sh build` produces shared
libraries; the wheel is a separate target:

```bash
./repo.sh build         # required first
./repo.sh py_package    # produces _build/packages/omniverse_scene_optimizer-*.whl
# Wheel metadata declares Python 3.12-only unless you rebuilt first with `./repo.sh --set-token python_ver:3.10 py_package`
# or `python_ver:3.11`.
python3.12 -m pip install _build/packages/omniverse_scene_optimizer-*.whl
```

Caveats:

- **Host toolchain (Linux).** `auditwheel repair` needs **`patchelf`** installed (`sudo apt-get install patchelf`). Without it, repair fails before wheels land under `_build/packages/`.
- **Network.** `tools/pyproject/pybuild.sh` bootstraps a venv under `_build/host-deps/py_package_venv/` and pip-installs Poetry, auditwheel, and tooling from PyPI. Air‑gapped or blocked hosts must pre-seed equivalents.
- **`py_package` venv wedged:** If bootstrap failed part‑way but `_build/host-deps/py_package_venv/` exists, the helper may reuse a broken env and invoke an incompatible system Poetry. **`rm -rf _build/host-deps/py_package_venv`** and rerun `./repo.sh py_package`.

- **Toolchain assumption (Linux).** The `auditwheel` repair step that runs
  inside `py_package` targets manylinux_2_35. On distros newer than the build
  matrix (e.g. GCC 13 / Ubuntu 24.04, which produces manylinux_2_39 binaries),
  the repair fails and `py_package` exits non-zero. The unrepaired wheel
  staged at `_build/pyproject/dist/*.whl` is still installable locally — but
  it ships only the operation plugin libs, **not** the Scene Optimizer core
  libs (those are normally bundled by auditwheel). To use it you must point
  the loader at your build tree (next bullets).
- **`LD_LIBRARY_PATH` (Linux).** **`auditwheel` repair** bundles many Scene Optimizer deps into **`omniverse_scene_optimizer.libs/`** (`RPATH` usually resolves them — **often no `LD_LIBRARY_PATH` needed** except when troubleshooting). **`auditwheel` repair fails:** install the unstaged/unrepaired wheel from `_build/pyproject/dist/` and export dev-tree loaders:

  ```bash
  export LD_LIBRARY_PATH=_build/$platform/release/lib:_build/$platform/release/extraLibs:$LD_LIBRARY_PATH
  ```

- **`PYTHONPATH` / dual `libusd` (Linux, repaired wheel vs `usd-exchange`):** Even after repair, **`pxr` is supplied by `usd-exchange`**, which may vendor a **different** auditwheel‑mangled `libusd_*.so` than the copy inside `omniverse_scene_optimizer.libs/`. If so, **`UsdUtilsStageCache` splits** and validators error with missing stage IDs. **Treat `PYTHONPATH` alignment as mandatory** unless you verified (e.g. `ldd` on `Usd/_usd.so` vs Scene Optimizer's `UsdUtils` linkage) both stacks resolve the **same** `libusd`:

  ```bash
  export PYTHONPATH=_build/$platform/release/python:_build/target-deps/usd/release/lib/python:$PYTHONPATH
  ```

The optional helper wrapper `tools/perf_validators/run.sh` sets **`PYTHONPATH` + `LD_LIBRARY_PATH`** for checkout builds; the raw **`omni_asset_validate` CLI does not**.

Track packaging work to unify `usd-exchange`'s USD with the repaired wheel artifact so this alignment step disappears for typical `pip` consumers.

### Prebuilt deployment

If you've received a **fully-repaired** wheel (manylinux\_2\_35 toolchain), **`pip install` + allow-list export** often suffices—but **still verify** dual-`libusd` does not recur (wheel + `usd-exchange` layout can change).

```bash
python3.12 -m pip install omniverse_scene_optimizer-*.whl   # interpreter must satisfy wheel tags
export OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS="omni.asset_validator:DefaultPlugin,omni.scene.optimizer.validators:SceneOptimizerValidatorPlugin"
omni_asset_validate path/to/asset.usd
```

What you typically **don't** need for **repaired wheels:** manual `LD_LIBRARY_PATH` pointing at `./repo.sh` `_build/` (bundled libs cover it).

What consumers **might** still need: **`PYTHONPATH` alignment** toward the Scene Optimizer build's USD whenever `usd-exchange` introduces a mismatched `pxr` linkage (see bullets above)—same remediation as checkout Path-B installs.

The entry-point allow-list env var remains required — see *Gotcha — entry-point allow-list* below.

To verify discovery: **`omni_asset_validate --listChecks | grep SceneOptimizer`**. **`grep` succeeding** confirms rules list; **`--listChecks` alone may exit non-zero** upstream under `set -e` despite good output — key off **`grep`'s exit code**, not solely the CLI's.

### Running the CLI

```bash
# Allow-list our entry-point plugin (see "Gotcha — entry-point allow-list" below)
export OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS="omni.asset_validator:DefaultPlugin,omni.scene.optimizer.validators:SceneOptimizerValidatorPlugin"

omni_asset_validate path/to/asset.usd                              # default + Performance rules
omni_asset_validate -c Performance asset.usd                       # only the Performance category
omni_asset_validate -r SceneOptimizerRtxMeshCountChecker asset.usd # one rule
```

Note: the entry-point plugin only registers the default rule set. To run
`FindOverlappingMeshes` from the CLI, register it manually before invoking the
validator.

### Known CLI issues

- **`pxr` and the C++ core must resolve to the same `libusd`.** Tracked as
  a packaging follow-up (coordinate `usd-exchange` + repaired wheel linkage).
  **Linux — default workaround:** prepend the Scene Optimizer build's USD bindings before invoking **`omni_asset_validate`**:

  ```bash
  export PYTHONPATH=_build/$platform/release/python:_build/target-deps/usd/release/lib/python:$PYTHONPATH
  export LD_LIBRARY_PATH=_build/$platform/release/lib:_build/$platform/release/extraLibs:$LD_LIBRARY_PATH
  ```

  The helper wrapper **`tools/perf_validators/run.sh`** does this automatically; **`omni_asset_validate` does not.**

  Skip it only after demonstrating one `UsdUtilsStageCache` (e.g. both `Usd/_usd.so` and `libusd` used by Scene Optimizer resolve identical `libusd_*.so`).

  Failures cite the offending stage ID and mismatch hypothesis (see `Operation.cpp`).

  *Why it happens:* multiple `libusd` images can load (`omniverse_scene_optimizer.libs/`, **`usd-exchange`’s libs**, unstaged `_build/` SOs …). **`UsdUtilsStageCache::Get()` is a static per image**, so stages registered from Python-visible `pxr` are invisible across a mismatched Scene Optimizer core.

- **`--help` does not list the Performance category.** Upstream
  `omni_asset_validate` doesn't surface plugin-registered category /
  rule taxonomy in `--help`, even after the env-var allow-list has
  loaded our plugin. Use this skill doc as the index until the upstream
  CLI gains plugin-aware help.

## Logging to file

Three sinks; mix and match:

```bash
# CSV: one row per issue (Asset, Rule, Message, Severity, Suggestion, Location).
omni_asset_validate asset.usd --csv-output issues.csv

# JSON: structured per-rule with full prim/property paths under "at".
omni_asset_validate asset.usd --json-output issues.json

# Capture C++ stdout/stderr and Python logging together:
omni_asset_validate asset.usd --json-output issues.json > validation.log 2>&1
```

Programmatic equivalents:

```python
from omni.asset_validator import IssueCSVData, export_json_file
import logging

logging.basicConfig(filename="validation.log", level=logging.INFO)   # validator lifecycle messages
IssueCSVData.from_(results).export_csv("issues.csv")
export_json_file("issues.json", [results])
```

Note: the C++ `[INFO]/[DEBUG]` lines from Scene Optimizer operations go to **stdout**, not Python `logging` — capture them at the shell level if needed.

## Performance behavior

The base class includes two optimizations that callers should know about.

### Analysis-result cache

Many rules wrap the same op with the same args (e.g. the mesh-cleanup-family rules read different fields of one `meshCleanup` result). The base class caches analysis output per `(root-layer-identifier, op_name, args)` so the analysis runs once per stage and the rest hit the cache.

**Lifetime**: cache persists for the life of the Python process. If a stage is mutated between `engine.validate(stage)` calls, the cached result is stale. Long-lived host processes that revalidate the same stage after edits should call:

```python
from omni.scene.optimizer.validators import clear_analysis_cache
clear_analysis_cache()
```

### Mesh-only short-circuit

Mesh-only rules early-out on stages with no `UsdGeomMesh`. The "has mesh" check is itself cached per stage. Rules that target hierarchy / materials / animation set `REQUIRES_MESH = False` so they keep running on mesh-less stages.

## Gotcha — entry-point allow-list

`omniverse-asset-validator>=1.15`'s `PluginManager` only loads `DefaultPlugin` unless `OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS` is set—but the env var is an **allow-list among plugins already discovered** via `importlib.metadata.entry_points(group="omni.asset_validator")`. So:

- **Programmatic users** call `register_all()` and ignore the env var.
- **CLI users** must **pip-install `omniverse-scene-optimizer`** (the wheel declares the `registrant` entry-point; a source checkout on `PYTHONPATH` does **not**) *and* export the env var (DefaultPlugin + ours) before running `omni_asset_validate`.

If `--listChecks` shows no `SceneOptimizer*` rows: unset / wrong env var **or** the wheel is missing from the interpreter `omni_asset_validate` runs on (`python -m pip show omniverse-scene-optimizer`). When only `DefaultPlugin` appears in INFO logs despite a correct export, suspect the missing-metadata case—not the env var alone.

> The current `omniverse-asset-validator` releases implement
> `OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS` as an allow-list (only listed
> entry-points load), even though the *Asset Validator Entrypoint Plugins*
> spec describes it as a debugging skip-list. The asset-validator team is
> aware; this section can be simplified once the loader matches the spec.

## Spec compliance

Our entry-point declaration follows the *Asset Validator Entrypoint Plugins* spec:

```toml
[project.entry-points."omni.asset_validator"]
registrant = "omni.scene.optimizer.validators:SceneOptimizerValidatorPlugin"
```

Discovery query: `importlib.metadata.entry_points(group="omni.asset_validator", name="registrant")`. The class exposes `on_startup()` / `on_shutdown()` (instance methods, accepted per the spec).

## Running validator tests

```bash
# Run validator tests as part of the standard python suite:
./repo.sh test -s python
```

The repo's Python test wrapper currently runs the full `python` suite; it does
not forward `-e` / `-k` filters to `source/tests/test.python/run_discover.py`.

## Adding a new performance validator

1. Add a new rule class under `source/core/python/omni/scene/optimizer/validators/<name>.py`, subclassing `SceneOptimizerRuleBase` from `_base.py`. Set `OPERATION_NAME` and `DEFAULT_ARGS`; override `_translate(stage, analysis)` to convert the operation's analysis JSON into `_AddWarning` / `_AddFailedCheck` / `_AddError` calls.
2. Set `REQUIRES_MESH = False` if the rule targets hierarchy / materials / animation rather than `UsdGeomMesh` content (see `flat_hierarchies.py` for an example). Defaults to `True`.
3. Re-export it from `validators/__init__.py` and add it to the appropriate list in `validators/_plugin.py`:
   - `_default_rule_classes()` for fast-scaling rules (registered by default)
   - `_expensive_rule_classes()` for rules whose backing op is too slow to enable by default
4. Add a test file `source/tests/test.python/test_validators_<name>.py` that opens a fixture stage, enables the rule on a `ValidationEngine(init_rules=False)`, and asserts on issue count + severity.
5. Update the smoke test (`test_validators_smoke.py`) `EXPECTED_RULES` tuple.
6. Rebuild — `source/core/premake5.lua` already copies the whole `validators/` tree into the wheel.

If the operation you want to back the rule with doesn't yet support analysis mode, add `getSupportsAnalysis()` + `executeAnalysisImpl()` to the C++ operation first (see `PLUGINS.md` § Analysis Mode).

## Purpose

Reference doc for Scene Optimizer's `omniverse-asset-validator`
integration: rule families, programmatic vs. CLI invocation, the
allow-list env var, libusd alignment, the analysis-result cache, the
`REQUIRES_MESH` short-circuit, and the recipe for adding a new rule
class. Other skills (`run-validators`, `interpret-validators`,
`new-operation`) link here for infrastructure details rather than
duplicating them.

## Prerequisites

- A local Scene Optimizer checkout when using checkout-provided helper
  wrappers or running tests; do not assume Kit or standalone SO installs
  provide those wrappers.
- A Python interpreter that can import `omni.scene.optimizer` and
  `omni.asset_validator`. The optional helper wrappers handle this for
  dev-tree runs; CLI use needs the appropriate `PYTHONPATH` /
  `LD_LIBRARY_PATH` setup described above.
- `omniverse-asset-validator` installed in the active Python (the dev
  driver auto-installs on first use; CLI users install via pip).

## Limitations

- This is a **reference doc**, not a workflow. For day-to-day "validate
  this asset" use, invoke the `run-validators` skill. For reading the
  resulting artifacts, invoke `interpret-validators`.
- The `omni_asset_validate` CLI has known quirks — `--help` doesn't
  show plugin-registered categories; **`pxr`** (often from **`usd-exchange`**) **and**
  Scene Optimizer's C++ stack must bind the **same** `libusd` image (**dual-`libusd`**, `UsdUtilsStageCache` mismatches otherwise). **`tools/perf_validators/run.sh`**
  sets `PYTHONPATH` / `LD_LIBRARY_PATH` against `./repo.sh build` checkout trees; **`omni_asset_validate` pip installs** typically need parallel exports from the same `_build/` tree (`_build/$platform/release/…`) unless packaging guarantees a unified USD stack — see *CLI invocation*. Prefer **`run-validators`/dev driver unless** callers require the upstream CLI binary.
- The `OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS` env var is currently
  enforced as an allow-list (only listed entry-points load), even
  though the AVEP spec describes it as a debugging skip-list. Track
  upstream for when this changes.
- Adding a new rule still requires C++ changes if the backing
  operation lacks analysis mode — see `PLUGINS.md` § Analysis Mode.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `omni_asset_validate --listChecks` shows no `SceneOptimizer*` rules | `OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS` unset or wrong, **or** `omniverse-scene-optimizer` not pip-installed so metadata has no `registrant` entry-point (common with source-only `PYTHONPATH`). | `pip install` the wheel (`./repo.sh py_package`), export the env var (see *Gotcha — entry-point allow-list*), or use `register_all()` / `tools/perf_validators/run.sh`. |
| Validator returns stage-id / cache / "`libusd`" mismatch errors | **`pxr` + C++ load different `libusd` images** (repaired wheel + separate `usd-exchange` libs vs `_build/`). | Prefer **`tools/perf_validators/run.sh`**. Otherwise set **`PYTHONPATH` _and_ `LD_LIBRARY_PATH`** from *Known CLI issues* (`_build/$platform/release/...`). |
| Stage edited but cached results stale | Analysis-result cache survives the Python process. | Call `clear_analysis_cache()` between runs. |
| Mesh-only rules silently skipped on a stage that obviously has meshes | `REQUIRES_MESH` short-circuit decided the stage has none — usually because meshes live below an unloaded payload. | Open the stage with `Usd.Stage.Open(path, Usd.Stage.LoadAll)` before validating. |
| `auditwheel` repair fails on Ubuntu 24.04 | Distro produces manylinux_2_39 binaries; `py_package` targets `_2_35`. | Install the unrepaired wheel from `_build/pyproject/dist/` and set `LD_LIBRARY_PATH` per the *CLI invocation* section. |
