---
name: debug-operation
description: Triage a failing Usd Optimize operation. Use when an op errors, silently no-ops, or returns unexpected output.
allowed-tools: Shell, Read, Grep, Glob
metadata:
  author: NVIDIA Corporation
  version: "1.0.1"
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

Run the operation through the `usdOptimize` CLI with full logging, and omit
`-w` so no (bad) output file is written.

```bash
# POSIX — verbose, capture stats, no output written
BIN=_build/linux-x86_64/release/bin/usdOptimize
"$BIN" -i "$ASSET" -o <key> -a <arg>=<val> -v -s > "$TMPDIR/debug-run.log" 2>&1
```

`-v` is verbose and `-s` captures before/after stats. Omitting `-w` means
nothing is written. The log captures the C++ `[INFO]`/`[WARN]`/`[ERROR]`
lines. For a multi-op chain, use `-c <config.json>` instead of `-o`/`-a`.

**Do not add `-r` here.** `-r` diverts the operation's warnings into the
report file instead of stdout, so `[WARNING]` lines the checks below grep
for never reach your log. If you also want the report, run a second pass
with `-r` added rather than folding it into this one.

If the user already has a log (e.g. from a prior `run-operations` run),
skip this step and read their log directly.

## Step 2 — Check the basics

Most "operation does nothing" reports fall into one of three categories.
Check them in order before going deeper.

### 2a. Argument key mismatch

An unknown key in a `-c` config logs `[WARNING] Unknown argument '<key>' for
operation '<op>' -- ignoring it` and the operation runs with the default, so
grep the log for `Unknown argument` first. (Via `-a` it's a hard error instead:
`Error: invalid argument specified`, exit 1.) Verify every key in the user's
config matches the operation's `addArgument()` declarations:

```bash
# Check the C++ source for the canonical argument keys
rg 'addArgument' source/operations/<key>/ --glob '*.cpp' --glob '*.h'
```

Or check the argument list in `docs/operations/<key>.rst`. Common mistakes:

- Camel-case vs snake-case (`mergeVertices` not `merge_vertices`).
- Plural vs singular (`paths` not `path`, `meshPrimPaths` not `meshPrimPath`).
- Boolean as string (`true` not `"true"` in JSON).

### 2b. Wrong operation key

The operation key is the first string argument to the `Operation(...)` base
constructor, not the class name or the `USD_OPTIMIZE_PLUGIN_INIT` argument. Verify:

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
from usd_optimize.core import ExecutionContext, UsdOptimizeCore
from pxr import Usd, UsdUtils

stage = Usd.Stage.Open("<asset>")
context = ExecutionContext()
context.usdStageId = UsdUtils.StageCache.Get().Insert(stage).ToLongInt()
context.analysisMode = 1
context.verbose = 1

success, error, output = UsdOptimizeCore.getInstance().executeOperation(
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

For the full CLI flag reference (`-v`, `-s`, `-an`, `-r`, `-st`), see
`docs/cli.rst`.

## Step 4 — Diagnose by failure pattern

### Hard failures (operation returns error)

| Log pattern | Likely cause | Fix |
|---|---|---|
| `Stage not found` or `Invalid stage id` | `ExecutionContext.usdStageId` not set or stale. | Use `context.set_stage(stage)` or set `usdStageId` via `UsdUtils.StageCache`. |
| `Operation '<key>' not found` | Operation plugin didn't load — either not built or not on the plugin search path. | Rebuild (`./repo.sh build`). Check `UsdOptimizeCore.getInstance().getOperations()` for the registered list. |
| `libusd` mismatch / stage-cache miss (see `Operation.cpp` error) | Two copies of `libusd` loaded — Python's `pxr` resolves to a different one than the C++ core. | Use the build's bundled Python via the wrapper scripts. See `run-validators` § CLI invocation for the `libusd` alignment exports. |
| Crash / segfault during execution | Usually a real bug. | Isolate the failing prim path, write a minimal repro, and file against the operation. |

### Silent no-ops (operation returns success, nothing changed)

| Pattern | Likely cause | Fix |
|---|---|---|
| Operation produces **no log line at all** | `paths` matched nothing. No operation prints a "0 processed" line, so silence *is* the signal — and the exit code is still 0. | Check the `paths` argument (see the bare-path rule below). An empty `paths` array means "all prims" for most ops, but some interpret it as "nothing selected." |
| `paths` set to a bare non-Mesh prim path | A bare path matches **only that one prim**. `paths=/World` on an Xform therefore matches nothing, silently, with exit 0 and no output. | Use the SdfPathExpression descendant form `/World//*`, or list both (`["/World", "/World//*"]`) when the target may itself be the Mesh. |
| Operation ran but output is identical to input | Arguments are at defaults that produce no change (e.g. `reductionFactor: 100` keeps everything). | Review the parameter defaults in the guide or C++ source. |
| Log shows processing but the save failed | No `-w`/`--write` output path was given, or `stage.GetRootLayer().Save()` / `.Export()` wasn't called. | Check the runner output for the save step. (There is no `--no-save` flag — omitting `-w` is how you run without writing.) |

### Unexpected output (operation ran but result is wrong)

| Pattern | Likely cause | Fix |
|---|---|---|
| Only some prims were affected | Operation scoped by `paths` or a prim-type filter. | Widen `paths` or check `meshPrimPaths` / similar scope arguments. |
| Values look wrong (e.g. extreme decimation) | World-unit mismatch — parameter values are in stage units, and `metersPerUnit` doesn't match assumptions. | Check `UsdGeom.GetStageMetersPerUnit(stage)` and scale parameters accordingly. |
| Merge produced unexpected grouping | `mergePoint` / `rootPath` / `considerMaterials` settings. | See `docs/operations/merge.rst`. |

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

- `optimizeMaterialsMode: 1` ("Convert to color") replaces material networks with
  flat colors. Unintended if the user didn't ask for it. The mode is an enum, not a
  boolean: `0` Deduplicate (the default), `1` Convert to color, `2` Remove unbound,
  `3` Deduplicate with primvars.

### Hierarchy operations (`flattenHierarchy`, `pruneLeaves`)

- These operate on the prim hierarchy, so they run on mesh-less stages too.
- `flattenHierarchy` has params that control depth; defaults may not
  flatten enough.

---

## See also

- `docs/operations/<key>.rst` — per-operation parameter reference and tuning guidance.
- `docs/cli.rst` — CLI flags (`-v`, `-s`, `-an`, `-r`, ...).
- `.agents/skills/run-operations/SKILL.md` — errors table for driver failures.
- `.agents/skills/tune-parameters/SKILL.md` — when the operation runs but
  output quality needs iteration.

## Purpose

Provide a structured triage workflow for an Usd Optimize operation that fails,
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
  log first with `--verbose -s` and no `-w` (there is no `--no-save`
  or `--captureStats` flag; `-s`/`--stats` is the stats switch).

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

This whole skill *is* a troubleshooting guide for Usd Optimize operations. The
table below covers the meta-failure modes — when this skill itself
can't make progress.

| Symptom | Likely cause | Fix |
|---|---|---|
| Verbose log is empty | `executionContext` step missing or placed after the failing op. | Put `{"operation":"executionContext","verbose":1}` as the first config entry. **Use `1`, not `true`** — the parser reads these keys with `IsInt()`, so a JSON boolean is silently ignored. `captureStats` is not a recognised key here; use the CLI `-s` instead. |
| Analysis mode reports `not supported` | Operation doesn't implement `executeAnalysisImpl`. | Drop analysis mode; run on a copy of the stage and omit `-w` so nothing is written. |
| Op succeeds standalone but fails in chain | Earlier step in the chain mutated the stage in a way that invalidates the inputs. | Bisect the chain — run the failing op directly after the input is loaded. |
| `libusd` mismatch error from `Operation.cpp` | Two `libusd` builds loaded — typical with system `pxr` + dev-tree Usd Optimize. | Use the build's bundled Python via the wrapper scripts (`tools/perf_*/run.sh`). See `run-validators` § CLI invocation for the `libusd` alignment exports. |

