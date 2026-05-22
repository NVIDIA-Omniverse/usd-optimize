---
name: debug-operation
description: Triage a failing Scene Optimizer operation. Use when an op errors, silently no-ops, or returns unexpected output.
version: "1.0.0"
allowed-tools: Shell, Read, Grep, Glob
metadata:
  author: NVIDIA Corporation
  tags: [debug, troubleshooting, operations]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Debug Operation

Structured workflow for triaging an operation that fails, produces unexpected
output, or silently does nothing.

## What this skill covers

Search this doc for keywords like `verbose`, `analysisMode`, `argument key`,
`silent no-op`, `mesh-less`, `instanced`, `libusd` to jump.

- **Step 1** — reproduce and capture the log.
- **Step 2** — check the basics (argument keys, input stage).
- **Step 3** — enable verbose + analysis mode.
- **Step 4** — diagnose by failure pattern (tables for common failure modes).
- **Operation-family cheat sheet** — per-family gotchas.

Companion skills: `run-operations` (how to invoke), `tune-parameters`
(when the op runs but the output isn't right), `build` (when the build
itself is broken).

---

## Step 1 — Reproduce and capture the log

Run the operation with full logging so the failure is captured on disk.

```bash
# POSIX — verbose + captureStats
tools/perf_operations/run.sh run "$ASSET" \
    --config '[{"operation":"executionContext","verbose":true,"captureStats":true},{"operation":"<key>","<arg>":"<val>"}]' \
    --no-save > /tmp/debug-run.log 2>&1
```

`--no-save` avoids writing a bad output file. The log captures both the
C++ `[INFO]`/`[WARN]`/`[ERROR]` lines and the Python driver output.

If the user already has a log (e.g. from a prior `run-operations` run),
skip this step and read their log directly.

## Step 2 — Check the basics

Most "operation does nothing" reports fall into one of three categories.
Check them in order before going deeper.

### 2a. Argument key mismatch

Operations silently ignore unknown argument keys. Verify every key in the
user's config matches the operation's `addArgument()` declarations:

```bash
# Check the C++ source for the canonical argument keys
rg 'addArgument' source/operations/<key>/ --glob '*.cpp' --glob '*.h'
```

Or check the Parameters table in `.agents/operations/<key>.md` if a guide
exists. Common mistakes:

- Camel-case vs snake-case (`mergeVertices` not `merge_vertices`).
- Plural vs singular (`paths` not `path`, `meshPrimPaths` not `meshPrimPath`).
- Boolean as string (`true` not `"true"` in JSON).

### 2b. Wrong operation key

The operation key is the first string argument to the `Operation(...)` base
constructor, not the class name or the `SO_PLUGIN_INIT` argument. Verify:

```bash
rg 'Operation\(' source/operations/<key>/ --glob '*.cpp' -A1 | head -5
```

### 2c. Input stage has no matching prims

Many operations scope work via a `paths` argument or operate only on
specific prim types. If the stage has no prims matching the filter, the
operation returns success but does nothing.

```python
# Quick check: does the stage have meshes?
from pxr import Usd, UsdGeom
stage = Usd.Stage.Open("<asset>")
meshes = [p for p in stage.TraverseAll() if p.IsA(UsdGeom.Mesh)]
print(f"{len(meshes)} meshes")
```

## Step 3 — Enable verbose and analysis mode

If the basics check out, run in analysis mode to see what the operation
*would* do without mutating the stage:

```python
from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from pxr import Usd, UsdUtils

stage = Usd.Stage.Open("<asset>")
context = ExecutionContext()
context.usdStageId = UsdUtils.StageCache.Get().Insert(stage).ToLongInt()
context.analysisMode = 1
context.verbose = 1

success, error, output = SceneOptimizerCore.getInstance().executeOperation(
    "<key>", context, {<args>}
)

print("success:", success)
print("error:", error)
print("output:", output)
```

Not all operations support analysis mode. Check:

```bash
rg 'getSupportsAnalysis' source/operations/<key>/ --glob '*.cpp'
```

If the operation doesn't override `getSupportsAnalysis`, analysis mode is
unavailable — fall back to running on a disposable copy of the stage.

For the full `ExecutionContext` flags reference, see
`.agents/operations/INVOCATION.md`.

## Step 4 — Diagnose by failure pattern

### Hard failures (operation returns error)

| Log pattern | Likely cause | Fix |
|---|---|---|
| `Stage not found` or `Invalid stage id` | `ExecutionContext.usdStageId` not set or stale. | Use `context.set_stage(stage)` or set `usdStageId` via `UsdUtils.StageCache`. |
| `Operation '<key>' not found` | Operation plugin didn't load — either not built or not on the plugin search path. | Rebuild (`./repo.sh build`). Check `SceneOptimizerCore.getInstance().getOperations()` for the registered list. |
| `libusd` mismatch / stage-cache miss (see `Operation.cpp` error) | Two copies of `libusd` loaded — Python's `pxr` resolves to a different one than the C++ core. | Use the build's bundled Python via the wrapper scripts. See `validators` skill § Known CLI issues. |
| Crash / segfault during execution | Usually a real bug. | Isolate the failing prim path, write a minimal repro, and file against the operation. |

### Silent no-ops (operation returns success, nothing changed)

| Pattern | Likely cause | Fix |
|---|---|---|
| Log says `0 meshes processed` or similar | `paths` filter doesn't match any prims, or stage has no matching prim types. | Check the `paths` argument. An empty `paths` array means "all prims" for most ops, but some interpret it as "nothing selected." |
| Operation ran but output is identical to input | Arguments are at defaults that produce no change (e.g. `reductionFactor: 100` keeps everything). | Review the parameter defaults in the guide or C++ source. |
| Log shows processing but the save failed | `--no-save` was passed, or `stage.GetRootLayer().Save()` / `.Export()` wasn't called. | Check the runner output for the save step. |

### Unexpected output (operation ran but result is wrong)

| Pattern | Likely cause | Fix |
|---|---|---|
| Only some prims were affected | Operation scoped by `paths` or a prim-type filter. | Widen `paths` or check `meshPrimPaths` / similar scope arguments. |
| Values look wrong (e.g. extreme decimation) | World-unit mismatch — parameter values are in stage units, and `metersPerUnit` doesn't match assumptions. | Check `UsdGeom.GetStageMetersPerUnit(stage)` and scale parameters accordingly. |
| Merge produced unexpected grouping | `mergePoint` / `rootPath` / `considerMaterials` settings. | See `.agents/operations/merge.md`. |

---

## Operation-family cheat sheet

### Mesh operations (`meshCleanup`, `decimateMeshes`, `merge`, etc.)

- Require `UsdGeomMesh` prims. No meshes = no work.
- `merge` won't merge a boundary with a single mesh unless
  `allowSingleMeshes: true`.
- `decimateMeshes` can't reduce below the topological minimum per mesh.
  Assets with many small parts will overshoot the target percentage.
- `reductionFactor` is a percentage (0–100), not a fraction. `0.5` means
  "keep 0.5%", not "keep 50%".

### Deduplication (`deduplicateGeometry`, `deduplicateHierarchies`)

- Prims with existing references/payloads are excluded from grouping.
- Instanced prims can't be authored in place — deinstance first.
- `deduplicateHierarchies` traverses from the stage's default prim.
  No default prim = no work.
- Material-related prims (`Material`, `Shader`, `NodeGraph`, `Looks`,
  `Materials` scopes) are intentionally skipped.

### Analysis-only operations (`findCoincidingGeometry`, `findFlatHierarchies`, `findOverlappingMeshes`, `findOccludedMeshes`)

- These detect but don't fix. The "fix" is a separate operation
  (e.g. `removePrims`, `flattenHierarchy`).
- Results come back in `output["analysis"]` — inspect that dict.

### Material operations (`optimizeMaterials`)

- `convertToColor: true` replaces material networks with flat colors.
  Unintended if the user didn't ask for it.

### Hierarchy operations (`flattenHierarchy`, `pruneLeaves`)

- `REQUIRES_MESH = False` — these run on mesh-less stages too.
- `flattenHierarchy` has params that control depth; defaults may not
  flatten enough.

---

## See also

- `.agents/operations/<key>.md` — per-operation parameter reference.
- `.agents/operations/INVOCATION.md` — ExecutionContext flags, API surface.
- `.agents/skills/run-operations/SKILL.md` — errors table for driver failures.
- `.agents/skills/tune-parameters/SKILL.md` — when the operation runs but
  output quality needs iteration.

## Purpose

Provide a structured triage workflow for an SO operation that fails,
silently no-ops, or produces unexpected output. Unifies log reading,
argument verification, verbose / analysis-mode invocation, and a
per-family cheat-sheet of common failure patterns so the agent can move
from a vague "it didn't work" report to a concrete root cause without
guessing.

## Prerequisites

- A built repo (`./repo.sh build` or `repo.bat build`) — verbose /
  analysis-mode invocation requires the bundled Python.
- The operation key (e.g. `meshCleanup`) and the user's invocation —
  config JSON, command line, or Python snippet — so the actual
  arguments are known.
- A reproducible invocation. If the failure is intermittent, capture a
  log first with `--verbose --captureStats --no-save`.

## Limitations

- This skill does not patch C++ source — its output is a diagnosis, not
  a code fix. For changes to the operation itself, the user works in
  `source/operations/<key>/` directly.
- Analysis mode is unavailable for operations that don't override
  `getSupportsAnalysis()`. The skill falls back to running on a
  disposable stage copy in those cases.
- Crashes / segfaults are surfaced as bug reports — the skill won't
  attach a debugger or symbolicate stacks.

## Troubleshooting

This whole skill *is* a troubleshooting guide for SO operations. The
table below covers the meta-failure modes — when this skill itself
can't make progress.

| Symptom | Likely cause | Fix |
|---|---|---|
| Verbose log is empty | `executionContext` step missing or placed after the failing op. | Put `{"operation":"executionContext","verbose":true,"captureStats":true}` as the first config entry. |
| Analysis mode reports `not supported` | Operation doesn't implement `executeAnalysisImpl`. | Drop analysis mode; run on a copy of the stage with `--no-save`. |
| Op succeeds standalone but fails in chain | Earlier step in the chain mutated the stage in a way that invalidates the inputs. | Bisect the chain — run the failing op directly after the input is loaded. |
| `libusd` mismatch error from `Operation.cpp` | Two `libusd` builds loaded — typical with system `pxr` + dev-tree SO. | Use the build's bundled Python via the wrapper scripts (`tools/perf_*/run.sh`). See `validators` skill § Known CLI issues. |

