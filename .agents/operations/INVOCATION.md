<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Operation invocation reference

Canonical patterns for running Scene Optimizer operations from Python and
from the shell. Operation guides under `.agents/operations/` describe each
operation's parameters and starting configs; this file shows how to actually
*call* them.

## Quickest path: the `run-operations` skill

```bash
# POSIX — optional helper wrapper from a source checkout; saves to
# <tmp>/scene-optimizer-operations/<sha1>/<asset_stem>.optimized.usdc
tools/perf_operations/run.sh run path/to/asset.usd \
    --config '[{"operation":"meshCleanup","mergeVertices":true}]'

# Or pick the output path explicitly
tools/perf_operations/run.sh run path/to/asset.usd \
    --config '[{"operation":"meshCleanup","mergeVertices":true}]' \
    --output path/to/out.usdc

# Windows (PowerShell)
& tools\perf_operations\run.bat run path\to\asset.usd `
    --config '[{"operation":"meshCleanup","mergeVertices":true}]' `
    --output path\to\out.usdc
```

The driver opens the stage, runs the chain, **saves the optimized stage**
(default path or explicit `--output`), and writes a per-op summary. Pass
`--no-save` to skip the save and discard the in-memory stage. See
`.agents/skills/run-operations/SKILL.md` for full options.

For curated multi-op chains organized by bottleneck (memory, load time, mesh
count, data quality), see `.agents/operations/PIPELINES.md`.

---

## Python API — single operation

```python
from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from pxr import Usd

stage = Usd.Stage.Open("path/to/asset.usd")

context = ExecutionContext()
context.set_stage(stage)         # caches stage in UsdUtilsStageCache; sets context.usdStageId

success, error, output = SceneOptimizerCore.getInstance().executeOperation(
    "meshCleanup",               # operation key (see .agents/operations/INDEX.md)
    context,
    {                            # args dict — keys match the operation's addArgument() calls
        "mergeVertices": True,
        "removeIsolatedVertices": True,
    },
)

if not success:
    raise RuntimeError(f"meshCleanup failed: {error}")

stage.GetRootLayer().Save()      # write the result back
```

Return tuple: `(success: bool, error_or_none: str | None, output_or_none: dict | None)`.

## Python API — chained operations

`executeConfig` runs a list of operations against the same context:

```python
from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from pxr import Usd

stage = Usd.Stage.Open("path/to/asset.usd")
context = ExecutionContext()
context.set_stage(stage)

config = [
    {"operation": "meshCleanup", "mergeVertices": True},
    # decimateMeshes: prefer maxMeanError for quality-driven reduction
    # (reductionFactor is a percentage 0-100, NOT a fraction).
    {"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": True},
]

results = SceneOptimizerCore.getInstance().executeConfig(context, config)
for (success, error, output), op in zip(results, config):
    if not success:
        raise RuntimeError(f"{op['operation']} failed: {error}")

stage.GetRootLayer().Save()
```

`results` is a list of `(success, error, output)` tuples in the same order as `config`.

## Python API — JSON-driven

`execute_commands_from_json` accepts either a JSON string or a path to a JSON
file. Inline JSON is the friendliest format for ad-hoc agent use; file paths
are friendlier for reusable pipelines.

```python
from omni.scene.optimizer.core.scripts import standalone
from pxr import Usd

stage = Usd.Stage.Open("path/to/asset.usd")

ok = standalone.execute_commands_from_json(stage, """[
    {"operation": "meshCleanup", "mergeVertices": true},
    {"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": true}
]""")

if not ok:
    raise RuntimeError("one or more operations failed")

stage.GetRootLayer().Save()
```

Special entries:

```json
[
  {"operation": "executionContext", "verbose": true, "captureStats": true},
  {"operation": "meshCleanup", "mergeVertices": true}
]
```

`{"operation": "executionContext", ...}` entries don't run an operation —
they update `ExecutionContext` flags for the rest of the chain. Recognized
keys: `debug`, `singleThreaded`, `verbose`, `generateReport`, `captureStats`.

The starting-config JSON in each `.agents/operations/<key>.md` guide is in
exactly this format, so you can copy it directly.

---

## ExecutionContext flags

```python
context = ExecutionContext()
context.set_stage(stage)

context.analysisMode = 1         # read-only — operation reports findings, doesn't mutate the stage
context.verbose = 1              # extra logging
context.generateReport = 1       # populate output dict with summary
context.captureStats = 1         # populate output["stats"] with timing/counts
context.singleThreaded = 1       # disable parallelism (debugging)
context.debug = 1                # extra debug spew
```

All flags default to 0 / off.

## Analysis mode (read-only checks)

Set `context.analysisMode = 1` to run an operation as a detector without
mutating the stage. The findings come back in `output["analysis"]`:

```python
context = ExecutionContext()
context.set_stage(stage)
context.analysisMode = 1

success, error, output = SceneOptimizerCore.getInstance().executeOperation(
    "meshCleanup", context, {"mergeVertices": True}
)

analysis = (output or {}).get("analysis", {})
# analysis is operation-specific JSON — read the relevant validator class
# (source/core/python/omni/scene/optimizer/validators/<name>.py) to see
# which keys each analysis populates.
```

This is exactly how the validator framework wraps operations — see
`source/core/python/omni/scene/optimizer/validators/_base.py:94-142` for the
canonical pattern.

---

## Output saving

```python
stage.GetRootLayer().Save()                  # in-place save (overwrites the source)

stage.GetRootLayer().Export("out.usda")      # save to a different path / format
```

`Export` honors the file extension: `.usdc` writes binary, `.usda` writes
text, `.usd` autoselects (defaults to `.usdc`). For data-heavy outputs,
prefer `.usdc`.

If you need to preserve the original asset, **open it once, save the
optimized stage to a new path, and never call `Save()` on the input layer.**
Operations are destructive in-memory; `Save()` writes the mutation back.

---

## Driver script (shell)

For shell-driven runs from a checkout that provides them, the optional wrappers
at `tools/perf_operations/run.{sh,bat}` set `LD_LIBRARY_PATH` / `PATH` /
`PYTHONPATH` to the build's bundled USD and Python, then call
`run_operations.py`. Use these the same way you'd use
`tools/perf_validators/run.{sh,bat}` — both follow the same convention. Do not
assume Kit, standalone package, or wheel installs provide these wrappers.

```bash
# Inline config (saves to default output path)
tools/perf_operations/run.sh run asset.usd \
    --config '[{"operation":"meshCleanup","mergeVertices":true}]'

# Config file with explicit output
tools/perf_operations/run.sh run asset.usd --config-file pipeline.json --output out.usda

# Named pipeline (keys defined in .agents/operations/PIPELINES.md)
tools/perf_operations/run.sh run asset.usd --pipeline memory-reduction --output out.usda

# No save — useful for timing / dry-runs
tools/perf_operations/run.sh run asset.usd --pipeline memory-reduction --no-save
```

`--config` is **inline JSON only**; for paths use `--config-file`.

---

## Common gotchas

- **`set_stage` caches the stage.** Re-using the same `ExecutionContext`
  across stages without `remove_stage()` between them keeps the prior stage
  cached. For one-shot scripts this is fine — `set_stage` updates the cache
  to point at the new stage. For long-lived hosts, call `remove_stage()`
  when done if you opened the stage on an anonymous layer (otherwise the
  stage is held alive by the cache).
- **`output` may be `None`** for operations that don't generate report
  data. Always guard with `(output or {}).get(...)`.
- **The selected SO Python/runtime paths must be consistent** for the C++ core
  to find its dependencies. In a source checkout with helper wrappers, invoke
  through `tools/perf_operations/run.{sh,bat}` rather than the system
  `python3`. Same constraint as the validators driver, same reason (`pxr` and
  the C++ core must resolve to the same `libusd`).
- **Operations reset their argument state after execution.** A second call
  must re-supply all non-default args. (The args dict on the call site is
  the source of truth — operation member variables get re-bound on each
  call.)
- **Argument keys are operation-specific.** Look them up in the operation's
  `addArgument()` calls in `source/operations/<key>/<OperationClass>.cpp`,
  or in the Parameters table of `.agents/operations/<key>.md`. Don't guess
  — invalid keys cause silent no-ops or warnings, not errors.
- **For chains, prefer `executeConfig` or `execute_commands_from_json`**
  over a manual loop — they reuse the cached stage and apply
  `executionContext` overrides correctly.

---

## See also

- `.agents/operations/INDEX.md` — operation key index.
- `.agents/operations/<key>.md` — per-operation tuning guide.
- `.agents/operations/PIPELINES.md` — curated multi-op chains by bottleneck.
- `.agents/skills/run-operations/SKILL.md` — driver-script skill.
- `.agents/skills/tune-parameters/SKILL.md` — interactive parameter tuning.
- `source/tests/test.python/test_operation_*.py` — idiomatic
  per-operation invocation examples.
- `source/tests/standalone_support/scripts/standalone.py` — implementation
  of `execute_commands_from_json`.
