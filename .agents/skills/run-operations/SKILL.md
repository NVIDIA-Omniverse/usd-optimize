---
name: run-operations
description: Run Scene Optimizer operations against a USD asset and save the optimized result. Use when applying ops or after interpret-validators recommends fixes.
version: "1.0.0"
allowed-tools: Bash
metadata:
  author: NVIDIA Corporation
  tags: [usd, optimization, operations]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# run-operations — Execute optimization operations

> **Invocation.** This is the `run-operations` skill. In Claude Code it's
> also available as the alias `/run-operations`. In Codex or other agents,
> invoke it by name.
>
> **Python invocation.** Examples below use `python3` (POSIX). On Windows
> use `py -3` (Python launcher) or the bundled interpreter at
> `_build\target-deps\python\python.exe`. Optional helper scripts mentioned
> below may be used only when the selected SO environment or build checkout
> provides them. Do not assume Kit, standalone package, or wheel installs ship
> these repo helper wrappers.
>
> **Windows shell.** Snippets target PowerShell. The cmd.exe equivalents
> are obvious — `$Var` → `%VAR%`, backtick line continuation → `^`,
> `New-Item -ItemType Directory -Force` → `if not exist ... mkdir`.

## What this skill covers

Each section below is load-bearing — read past Step 4 before concluding info is missing. Search for keywords like `pipeline`, `output`, `executionContext`, `analysisMode`, `verbose`, or `save` to jump.

- **Usage** — flags and positional args.
- **Step 0** — confirm every planned op is registered by the selected runtime.
- **Step 1** — validate the input USD path.
- **Step 2** — resolve the output path.
- **Step 3** — invoke the optional driver wrapper when present (POSIX + Windows).
- **Step 4** — print a terse summary and offer to re-validate.
- **Confirm before running destructive ops** — when to pause and ask the user; safe-cleanup as the conservative fallback.
- **Build a config from validator findings** — turn an interpret-validators report into an op chain.
- **Common parameter patterns** — quick reference for `analysisMode`, `verbose`, `executionContext` overrides.
- **Errors to handle** — failure modes table.

Companion skills: `run-validators` (diagnosis), `interpret-validators`
(report + fix recommendations), `tune-parameters` (interactive parameter
iteration). For the canonical invocation reference (Python API, JSON
helper, output saving) see `.agents/operations/INVOCATION.md`. For curated
multi-op chains by bottleneck, see `.agents/operations/PIPELINES.md`.

---

## Usage

The skill takes one positional argument — the path to a USD asset — plus
exactly one source for the operation chain, plus optional flags:

| Argument / Flag | Meaning |
|---|---|
| `<path/to/asset.usd>` | Required. `.usd` / `.usda` / `.usdc` / `.usdz`. The driver rejects other extensions before opening the stage. |
| `--config '<json>'` | **Inline JSON only.** A list of operation dicts. Friendliest for ad-hoc fixes from validator findings. For a path, use `--config-file`. |
| `--config-file <path>` | Path to a JSON file containing the operation list. Use for reusable pipelines. |
| `--pipeline <name>` | Named pipeline from `tools/perf_operations/pipelines.json` (e.g., `memory-reduction`). Run `list-pipelines` to see options. |
| `--output <path>` | Output USD path. **If omitted, defaults to `<tmp>/scene-optimizer-operations/<sha1>/<asset_stem>.optimized.usdc`** — same default as `resolve_output.py`. |
| `--no-save` | Run operations without saving. Useful for timing or dry-runs. Mutually exclusive with `--output` — passing both is rejected with a clear error. |
| `--summary <path>` | Per-operation timing + success summary JSON. |
| `--verbose` | Set `ExecutionContext.verbose = 1`. |
| `--capture-stats` | Set `ExecutionContext.captureStats = 1`. |

If no path is provided, ask:
> "Which USD file should I optimize? Please provide the full path."

If no operation source is provided, ask:
> "What operations should I run? You can pass `--config '<json>'`, `--config-file <path>`, or `--pipeline <name>` (run `list-pipelines` to see named pipelines)."

## Step 0 — Confirm operation availability

Before invoking a chain, compare every planned operation key against the
selected runtime's registered operations when that surface is available. Common
sources include the runtime's API, a helper `list-operations` command, package
metadata, or a setup probe written by a higher-level workflow. If a planned op
is not registered, do not run the chain. Surface the missing op key, the
runtime/version being used, and a nearest supported fallback if one is obvious.

This check is especially important across source-tree builds, Kit extensions,
standalone packages, and older Scene Optimizer drops. Missing operations may
otherwise fail late or produce misleading no-op reports.

## Step 1 — Validate the input

A path that doesn't exist or isn't a USD file should be rejected before
doing any other work. If the selected checkout provides `resolve_output.py`,
it handles **both** checks and returns an `error` field for either failure
(non-existent path or non-`.usd*` extension). The driver itself enforces the
same checks before opening the stage, so invalid paths fail fast at either
layer.

## Step 2 — Resolve the output path

Choose an output USD path, output directory, and log path. If the selected SO
environment or build checkout provides the cross-platform helper, you may use
it; otherwise choose explicit paths before invoking the driver.

```bash
# POSIX (bash, zsh)
python3 tools/perf_operations/resolve_output.py "<asset>" [--output <path>]
```
```powershell
# Windows (PowerShell)
py -3 tools\perf_operations\resolve_output.py "<asset>" [--output <path>]
```

When used, the helper prints a single JSON object on stdout with keys: `output_dir`,
`output`, `log`, `asset_abs`. Parse it (or shell out to `jq`) for the paths
used in Step 3.

The optional helper's output dir defaults to
`<temp>/scene-optimizer-operations/<sha1(asset_abs)>/` where `<temp>` is
`$TMPDIR` / `%TEMP%` / `/tmp` depending on platform. The default output
filename is `<asset_stem>.optimized.usdc` regardless of input format — `.usdc`
(binary) is preferred for downstream consumption.

If the user wants the optimized stage saved next to the input (or to a
specific path), pass `--output <path>` to the helper and to the driver.

## Step 3 — Invoke the driver

When present in the selected SO environment or build checkout, both wrappers
set up the build's bundled Python, `LD_LIBRARY_PATH` / `PATH`, and
`PYTHONPATH`, then invoke `run_operations.py`. They take the same arguments.
**When using this path, go through the wrapper** — never invoke
`_build\target-deps\python\python.exe run_operations.py` directly. The wrapper
prepends the selected USD/Python paths so they win over any ambient USD
install. Calling the bundled interpreter raw inherits the user's `PYTHONPATH`
and will fail with errors like `<dll> conflicts with this version of Python`
or USD-version mismatches. Optional pure-stdlib helpers such as
`resolve_output.py` are fine without the wrapper because they import nothing
from `omni` or `pxr`.

**Always redirect stdout/stderr to `<output_dir>/run.log`** rather than
piping through `tail` — pipes buffer until the upstream process exits, so a
`tail -N` filter hides all in-progress output. A log file is tailable in real
time during long runs and is persistent for follow-up review.

If using `resolve_output.py`, remember it is a pure resolve — it does **not**
create the output directory. Make it before the driver invocation here.

POSIX:

```bash
OUTPUT_DIR="<from Step 2>"
OUTPUT="<from Step 2>"
ASSET_ABS="<from Step 2>"
mkdir -p "$OUTPUT_DIR"

# Pick exactly one of: --config / --config-file / --pipeline
tools/perf_operations/run.sh run "$ASSET_ABS" \
    --pipeline memory-reduction \
    --output "$OUTPUT" \
    --summary "$OUTPUT_DIR/summary.json" > "$OUTPUT_DIR/run.log" 2>&1
```

Windows (PowerShell):

```powershell
$OutputDir = "<from Step 2>"
$Output    = "<from Step 2>"
$AssetAbs  = "<from Step 2>"
New-Item -ItemType Directory -Force -Path $OutputDir | Out-Null

& tools\perf_operations\run.bat run $AssetAbs `
    --pipeline memory-reduction `
    --output "$Output" `
    --summary "$OutputDir\summary.json" *> "$OutputDir\run.log"
```

### Long-running execution

Heavy ops on real assets can take minutes (decimation, hidden-mesh
removal, dedup at scale). Default to launching the driver as a long-running
command and let the agent return control to the user immediately:

- **Claude Code**: set `run_in_background: true` on the Bash tool call.
- **Codex / generic shell agents**: backgrounded shell session plus bounded
  log snapshots for status unless the user explicitly asks for a live stream.
  PowerShell:
  `Start-Process -FilePath tools\perf_operations\run.bat -ArgumentList @('run', $AssetAbs, '--pipeline', 'memory-reduction', '--output', $Output) -RedirectStandardOutput "$OutputDir\run.log" -RedirectStandardError "$OutputDir\run.log" -NoNewWindow`.
  POSIX: `nohup ... &`.
- **Foreground**: just run synchronously. Allow a generous timeout.

After launching as a long-running command, immediately tell the user:

> Optimization is running in the background for `<filename>`. I'll present
> the per-operation results when it finishes. You can ask me to tail
> `<output_dir>/run.log` if you want a status update.

For status checks during a backgrounded run, show a bounded log snapshot:

```bash
tail -n 80 "$OUTPUT_DIR/run.log"   # POSIX
```
```powershell
Get-Content "$OutputDir\run.log" -Tail 80   # Windows (PowerShell)
```

## Step 4 — Print a terse summary and offer to re-validate

After the driver finishes, show the last ~40 lines of `run.log` (per-op
status + total) and append:

```
Optimization complete.
  Output: <OUTPUT> (<size>)
  Log:    <OUTPUT_DIR>/run.log
  Summary: <OUTPUT_DIR>/summary.json

To verify the fix worked, run validation on the optimized stage:
  /run-validators <OUTPUT>
  /interpret-validators <OUTPUT>

The two reports' totals.failures_by_rule should show the targeted rules
dropping to 0 (or close to it). Use `tools/perf_validators/run.sh compare
before-summary.json after-summary.json` for a per-rule diff.
```

For `--no-save` runs, drop the `Output:` line and skip the validation
suggestion — there's no optimized stage on disk to validate. Surface the
log + summary paths and the per-op timing only.

If any operation in the chain failed (the driver exits non-zero or the
summary's `results` array contains `success: false` entries), surface that
explicitly:

> Operation `<name>` at index `<i>` failed: `<error>`. Subsequent
> operations were skipped/run anyway depending on the chain — see
> `<OUTPUT_DIR>/run.log` for details. Common causes:
> - Argument key mismatch (check `.agents/operations/<name>.md` Parameters table).
> - Stage doesn't contain prims the operation needs (e.g. `meshCleanup`
>   on a stage with no `UsdGeomMesh`).

Don't auto-retry failed ops — surface them and let the user decide.

---

## Confirm before running destructive ops

Some operations alter geometry or remove prims permanently. When the
requested chain (whether `--config`, `--config-file`, or `--pipeline`)
contains any of the following, **list them out for the user, explain
what each does, and ask for confirmation before invoking the driver.**
Don't just run silently — even an "obvious" pipeline can surprise a
user who didn't realize their poly count would drop.

| Op | Why it's destructive | What to confirm |
|---|---|---|
| `decimateMeshes` | Drops vertices permanently. Default `reductionFactor=50.0` keeps half the vertices; values below 10 destroy the silhouette. **`reductionFactor` is a percentage in 0–100, NOT a fraction** — `0.5` means "drop 99.5%". | Ask whether the goal is preserving silhouette (use `maxMeanError`, e.g., `0.01` in meter-scale scenes) or hitting a target reduction rate (use `reductionFactor`). See `.agents/operations/PIPELINES.md` *Decimation* section and `.agents/operations/decimateMeshes.md` for the full tuning table. |
| `removeSmallGeometry` | Removes meshes below a screen-space threshold. Bounded loss but the mesh is gone. | Confirm the screen-size threshold is appropriate (defaults are usually fine, but very small target outputs might cull too aggressively). |
| `meshCleanup` with `makeManifold: true` | Repairs non-manifold edges by changing topology — can rearrange faces in unexpected ways. | Confirm the user wants topology repair vs. just welding / degenerate removal. |
| `optimizeMaterials` with `convertToColor: true` | Replaces material networks with constant-color materials. Loses all shading detail. | Don't enable unless the user explicitly asked for "remove all shaders" / "go to flat colors". |

**If the user is uncertain, fall back to `--pipeline safe-cleanup`** —
it's all-lossless (computeExtents + pruneLeaves + deduplicateGeometry +
optimizeMaterials + optimizeTimeSamples). You can always run a destructive
pipeline as a second pass after the user has reviewed the safe-cleanup
result.

For named pipelines, only `mesh-count-reduction` and `data-quality-baseline`
contain destructive ops today; `safe-cleanup`, `memory-reduction`,
`load-time-reduction`, and `hierarchy-dedup` are all lossless.

---

## Build a config from validator findings

When the user has just run `/run-validators` + `/interpret-validators` and
asked "run the recommended fixes":

1. **Read the interpret-validators report.** The Operation column lists
   the op key for each firing rule. Tier-1 (T1) rules: run as-is with
   defaults. Tier-2 (T2) rules: run, but warn that defaults may not
   resolve the issue and the user may need `tune-parameters` to iterate.
   Tier-3 (T3) rules: do NOT include in the config — they're analysis-only
   or require manual review.
2. **Group ops by family.** `meshCleanup` typically wraps multiple
   findings (Colocated/DuplicateFaces/Isolated/NonManifold/Windings/ZeroAreaFaces)
   — emit it once with the union of relevant flags rather than five
   times.
3. **Order the chain.** Use `.agents/operations/PIPELINES.md` as the
   reference — within each bottleneck section the order is deliberate
   (e.g., `meshCleanup` before `decimateMeshes`, `deduplicateGeometry`
   before `decimateMeshes`).
4. **Don't auto-add `decimateMeshes` for high-vertex-count rules.**
   `MeshDensity` / `RtxMeshCount` / `SmallMesh` etc. are addressable by
   `deduplicateGeometry` + `removeSmallGeometry` first (lossless or
   bounded-loss). Decimation should be added only after the user
   confirms the goal — preserve silhouette (use `maxMeanError`) vs
   hit a target reduction rate (use `reductionFactor`). See
   `.agents/operations/PIPELINES.md` *Decimation* section.
5. **Confirm with the user** before running. Show the final JSON, list
   the validator findings each step addresses, and call out any
   destructive ops per the table above.

## Common parameter patterns

Inline `executionContext` entries set context flags mid-chain. Recognized
keys: `verbose`, `captureStats`, `generateReport`, `singleThreaded`,
`debug`. Anything else is silently ignored.

```json
[
  {"operation": "executionContext", "verbose": true},
  {"operation": "meshCleanup", "mergeVertices": true},
  {"operation": "executionContext", "captureStats": true},
  {"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": true}
]
```

For **analysis-only** runs (read-only "what would this do?" checks), use
`/run-validators` instead — every Scene Optimizer validator rule wraps an
analysis-mode operation, and the validator framework already handles
result aggregation. The `run-operations` skill is for executing
optimizations that mutate the stage; it does **not** expose `analysisMode`
through the JSON config (it's not in the recognized executionContext keys
above). If you need direct access to `analysisMode` from Python, see
`.agents/operations/INVOCATION.md`.

For the full Python-API surface (`SceneOptimizerCore.executeOperation`,
`executeConfig`, `analysisMode`, output inspection), see
`.agents/operations/INVOCATION.md`.

---

## Errors to handle

| Symptom | Cause | What to tell the user |
|---|---|---|
| `Build not found at _build/...` from run wrapper | `./repo.sh build` (POSIX) or `repo.bat build` (Windows) hasn't run | Point at the `build` skill: build the repo first. |
| `error: --config is not valid JSON: ...` | Inline JSON quoting issue (especially in Windows shells) | Suggest `--config-file <path>` instead and write the JSON to a file. |
| `error: unknown pipeline 'X'` | Typo or missing entry in `pipelines.json` | Run `list-pipelines` to see available names. To add a new pipeline, edit `tools/perf_operations/pipelines.json` and document it in `.agents/operations/PIPELINES.md`. |
| Driver exits non-zero, summary shows `success: false` | One or more operations failed | Surface the failed op's `error` from the summary; check `run.log` for the C++ stack/warning. Don't try to interpret partial output. |
| Argument silently has no effect | Argument key mismatch (operations don't error on unknown keys) | Verify the key against the operation's `addArgument()` calls in `source/operations/<key>/<OperationClass>.cpp` or the Parameters table in `.agents/operations/<key>.md`. |
| PowerShell `& tools\perf_operations\run.bat ...` errors with `NativeCommandError` | PowerShell wraps stderr lines from native executables in error records. The driver may have succeeded — `*>` redirects all streams to the log; check `$LASTEXITCODE` to confirm. | Don't add `2>&1` separately; `*>` already merges. |

If the user hasn't built the repo, **don't attempt the build yourself** —
the build takes minutes and may need flags. Point at the `build` skill and
stop.

## Purpose

Apply a chain of Scene Optimizer operations to a USD stage and save the
optimized result. Use checkout-provided helper wrappers when they are present,
or the selected runtime's equivalent operation API/path when they are not.
Closes the validate → optimize → re-validate loop. Handles input validation,
operation availability checks, output-path resolution, long-running execution,
post-run summary, and re-validation hand-off.

## Prerequisites

- A selected SO runtime. Checkout-provided helper wrappers require a built
  repo (`./repo.sh build` or `repo.bat build`); Kit, standalone package, and
  wheel installs may not provide those wrappers.
- A USD asset (`.usd` / `.usda` / `.usdc` / `.usdz`).
- Exactly one operation source: an inline JSON `--config`, a JSON file
  via `--config-file`, or a named `--pipeline`.
- A writable output location — defaults to a temp dir keyed by SHA1 of
  the asset path; override with `--output`.

## Limitations

- This skill **executes** operations; it does not author new ones —
  for that, use `new-operation`.
- It does not expose `analysisMode` through the JSON config (that key
  is not a recognized `executionContext` flag). Use `run-validators`
  for analysis-only inspection.
- Long-running execution depends on the agent harness — Claude Code
  uses `run_in_background: true`; generic shells use `nohup` /
  `Start-Process`. Foreground `--no-save` works everywhere but blocks.
- Destructive ops (`decimateMeshes`, `removeSmallGeometry`,
  `meshCleanup makeManifold`, `optimizeMaterials convertToColor`)
  require explicit user confirmation before invocation — see the
  *Confirm before running destructive ops* table.
- This skill won't trigger a build. If `_build/` is missing, point at
  the `build` skill and stop.

## Troubleshooting

The *Errors to handle* table above already covers the main failure
modes. Additional meta-troubleshooting:

| Symptom | Likely cause | Fix |
|---|---|---|
| Driver appears to hang for >5 min on a "small" asset | A destructive op (e.g. `decimateMeshes` with cut-and-glue) is dominating runtime. | Tail `<output_dir>/run.log`; if a single op is mid-flight, wait. To bound runtime, switch to `--pipeline safe-cleanup` or remove the heavy op. |
| Output exists but `interpret-validators` reports the same failures | Operations didn't address the targeted rules — argument key mismatch or wrong op family. | Check `summary.json` for per-op results; cross-reference with `.agents/operations/<key>.md`. |
| `summary.json` shows `success: true` but the stage looks unchanged | Op ran on an empty selection (`paths` filter / wrong prim type). | Use `inspect-asset` to confirm the stage has the prims the op targets. |
| Re-validation lift comparison says "no improvement" | Ran the wrong pipeline for the bottleneck. | Check `.agents/operations/PIPELINES.md` to pick the bucket that matches the failing rules. |
