---
name: run-operations
description: Run Usd Optimize operations on a USD asset with the usdOptimize CLI, using inline ops or a preset/JSON config. Use when applying optimizations or fixing issues interpret-validators flagged.
allowed-tools: Bash
metadata:
  author: NVIDIA Corporation
  version: "2.0.1"
  tags: [usd, optimization, operations, cli]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# run-operations — Run optimizations with the usdOptimize CLI

Drives the official command line tool to apply one or more operations to a USD
stage and write the result. The CLI is documented in
[`docs/cli.rst`](../../../docs/cli.rst); per-operation arguments and tuning
guidance are in [`docs/operations/`](../../../docs/operations/) (one `.rst` per
operation key); guidance on *which* operations to use is in
[`docs/choosing-operations.rst`](../../../docs/choosing-operations.rst).

## What this skill covers

- **The binary** — locating `usdOptimize` for the platform/config.
- **Usage** — single op, chained ops, and config files.
- **Preset configs** — the ready-made stacks in `config_presets/`.
- **Step 1** — validate the input path.
- **Step 2** — assemble the operation source (inline, preset, or custom config).
- **Step 3** — run the CLI and capture the log.
- **Step 4** — summarize and offer to re-validate.
- **Confirm before destructive ops** — when to pause and ask.
- **Build a config from validator findings** — for issues `run-validators --fix` could not auto-resolve.
- **Errors to handle** — failure modes.

Companion skills: `run-validators` (validate + auto-fix), `interpret-validators`
(decide what to do about non-auto-fixable issues), `tune-parameters` (iterate
one operation's parameters), `inspect-asset` (confirm the stage has the prims
an op targets).

---

## The binary

```
_build/<platform>/<config>/bin/usdOptimize        # POSIX
_build\<platform>\<config>\bin\usdOptimize.bat     # Windows
```

Use the `.bat` on Windows, not `usdOptimize.exe`: it sets up the library path
the executable needs. The bare `.exe` exits `-1073741515`
(`STATUS_DLL_NOT_FOUND`), and `PATHEXT` picks it first, so name the `.bat`.

`<platform>` is `linux-x86_64`, `linux-aarch64`, or `windows-x86_64`;
`<config>` is `release` (default) or `debug`. List `_build/` to pick the right
one. The binary is self-contained (its rpath resolves the bundled USD and
operation-plugin libraries), so **no `LD_LIBRARY_PATH` / `PYTHONPATH` setup is
needed** — run it directly. If `_build/` is missing, point at the `build` skill
and stop; do not build it yourself.

## Usage

Key flags (full list: `usdOptimize --help` and `docs/cli.rst`):

| Flag | Meaning |
|---|---|
| `-i <stage>` | Input stage (required). |
| `-w <file>` | Output stage to write. Exports the root layer; add `-fl` to flatten instead. Omit to run without writing (e.g. analysis). |
| `-o <operation>` | Add an operation. Repeatable; ops run in the order given. |
| `-a key=value` | An argument for the **most recent** `-o`. Arrays are comma-separated. |
| `-c <file.json>` | Run a JSON config file (an array of operation objects). Use this for chains and presets. |
| `-an` | Analysis mode (read-only; unsupported ops are skipped). |
| `-s` | Capture before/after stage stats. |
| `-r` | Emit a report of what was done. |
| `-v` | Verbose. |
| `-j <file>` | Write the assembled operation config to JSON (handy to capture a chain you built with `-o`/`-a`). |

Single operation:

```bash
usdOptimize -i input.usd -o meshCleanup -a mergeVertices=true -a removeDegenerateFaces=true -w output.usd
```

Operation chain via a JSON config (the array shape used by `-c`):

```json
[
  {"operation": "meshCleanup", "mergeVertices": true},
  {"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": true}
]
```

```bash
usdOptimize -i input.usd -c chain.json -w output.usd
```

## Preset configs

`config_presets/*.json` are ready-made stacks (see
[`config-presets`](../config-presets/SKILL.md) for the preset table and when
to use each): `safe-cleanup`, `memory-reduction`, `load-time-reduction`,
`hierarchy-dedup` (all lossless), and `data-quality-baseline`,
`mesh-count-reduction` (bounded loss). Run one directly:

```bash
usdOptimize -i input.usd -c config_presets/safe-cleanup.json -w output.usd
```

To customize, copy a preset and edit the operation list. **If the user is
unsure what to run, `safe-cleanup` is the conservative, all-lossless default.**

## Step 1 — Validate the input

Reject a path that does not exist or is not a `.usd*` file before running. If
no path was given, ask which USD file to optimize.

## Step 2 — Assemble the operation source

Pick exactly one: inline `-o`/`-a` (one or two ops), a preset
`-c config_presets/<name>.json`, or a custom config file you write. For
argument names, types, and defaults, read the operation's
`docs/operations/<key>.rst`. Empty `paths` (or `meshPrimPaths`) means "process
the whole stage"; pass explicit prim paths to scope an op.

## Step 3 — Run the CLI

Choose an output path (a temp dir or next to the input, per the user) and
redirect to a log file rather than piping through `tail` (pipes buffer until
exit). Heavy ops (decimation, occlusion removal, dedup at scale) can take
minutes — launch as a long-running command (`run_in_background: true` in Claude
Code; `nohup`/`Start-Process` elsewhere) and return control.

```bash
BIN=_build/linux-x86_64/release/bin/usdOptimize
OUT=<output path>
"$BIN" -i "<asset>" -c config_presets/memory-reduction.json -s -w "$OUT" > "$OUT.log" 2>&1
```

```powershell
$Bin = "_build\windows-x86_64\release\bin\usdOptimize.bat"
$Out = "<output path>"
& $Bin -i "<asset>" -c config_presets\memory-reduction.json -s -w $Out *> "$Out.log"
```

Don't add `-r`, which moves the `-s` stats out of the log,
into a `usdOptimize.*` report in the system temp directory,
and prints the path of a second report that lacks them.
Run `-r` as a separate pass if you need a report.

Tell the user it is running and that you will report results when it finishes.

## Step 4 — Summarize and offer to re-validate

Show the last ~40 lines of the log (the `-s` stats, per-op
timings, the final "UsdOptimize finished" line). Then:

```
Optimization complete.
  Output: <OUT> (<size>)
  Log:    <OUT>.log

To confirm the result, validate the optimized stage:
  /run-validators <OUT>
```

If the CLI exits non-zero, surface the failing line from the log; don't
auto-retry. A common cause is an argument key mismatch, and the two entry points
differ: `-a key=value` **rejects** an unknown key (`Error: invalid argument
specified: <key>`, exit 1), while a `-c` config file **warns and continues**,
running the operation with the default. Verify keys against
`docs/operations/<key>.rst`.

**Exit 0 does not prove every operation ran.** With `-c`, an operation name the
build cannot resolve is logged as `Could not find operation <name>`, skipped, and
the run continues to a written output and exit 0. That is deliberate — one config
can target several deployments, and the native CLI does not load the
Python-plugin operations (`pythonScript`, `deleteHiddenPrims`,
`removeUntypedPrims`, `moveMaterials`) that the wheel provides. The warning is
logged where that operation would have run, so it may not appear in the last ~40
lines above: search the whole log for `Could not find operation` before reporting
success, and name any skipped operations to the user.

---

## Confirm before destructive ops

Some operations permanently alter geometry or remove prims. When the requested
chain contains any of these, list them, explain what each does, and confirm
before running:

| Op | Why destructive | What to confirm |
|---|---|---|
| `decimateMeshes` | Permanently drops vertices. `reductionFactor` is a **percentage (0-100), not a fraction** — `0.5` means "keep 0.5%". | Whether the goal is preserving silhouette (use `maxMeanError`, `reductionFactor: 0.0`) or hitting a target rate (use `reductionFactor`). See `docs/operations/decimateMeshes.rst`. |
| `removeSmallGeometry` | Removes meshes below a size threshold. | The threshold is appropriate for the target output size. |
| `meshCleanup` with `makeManifold: true` | Repairs topology; can rearrange faces. | The user wants topology repair, not just welding/degenerate removal. |
| `optimizeMaterials` with `optimizeMaterialsMode: 1` ("Convert to color") | Replaces material networks with constant colors; loses shading. | Only enable if the user explicitly asked to flatten to colors. |
| `merge` on instanced meshes | Expanding instances **increases** memory. | The meshes aren't scenegraph instances (or the trade-off is intended). |

If the user is uncertain, fall back to `config_presets/safe-cleanup.json`.

## Build a config from validator findings

`run-validators --fix` already applies every fix the validators can apply
automatically. Reach for this section only for the issues it reported as
**unfixed** (which `interpret-validators` will have triaged), because they need
a decision:

1. From the interpret-validators report, take the operation each unfixed issue
   maps to and the parameters the user chose.
2. Group ops by family — one `meshCleanup` with the union of needed flags
   rather than several.
3. Order the chain sensibly (clean topology before decimation; dedup before
   decimation). `docs/choosing-operations.rst` covers ordering by goal.
4. Show the final JSON and confirm before running, calling out any destructive
   op from the table above.

## Errors to handle

| Symptom | Cause | What to tell the user |
|---|---|---|
| Binary missing under `_build/.../bin/` | Repo not built | Point at the `build` skill; build first. |
| `Failed to open stage` | Bad/unsupported input path | Verify the path and that it's a real USD stage (`inspect-asset`). |
| CLI exits non-zero mid-chain | An operation failed | Surface the failing op line from the log; check args against `docs/operations/<key>.rst`. |
| Output written but stage looks unchanged | Op ran on an empty selection (wrong `paths`/prim type) | Use `inspect-asset` to confirm the stage has the targeted prims. |
| Argument has no effect | Unknown key in a `-c` config — the op warns (`Unknown argument '<key>' ... ignoring it`) and runs with the default | Grep the log for `Unknown argument`; verify the key in `docs/operations/<key>.rst`. |
| An operation in the chain never ran, at exit 0 | The build cannot resolve that operation name — `-c` logs `Could not find operation <name>`, skips it, and finishes normally | Grep the whole log for `Could not find operation`. If it is a Python-plugin op (`pythonScript`, `deleteHiddenPrims`, `removeUntypedPrims`, `moveMaterials`), run the config through the Python wheel — the native CLI cannot load them. |

## Purpose

Apply a chain of Usd Optimize operations to a USD stage with the `usdOptimize`
CLI — inline, from a preset config, or from a custom JSON config — and write the
result. Closes the validate → fix → re-validate loop for issues that need a
manual operation.

## Prerequisites

- A built repo so `_build/<platform>/<config>/bin/usdOptimize` exists.
- A USD asset (`.usd` / `.usda` / `.usdc` / `.usdz`).
- An operation source: inline `-o`/`-a`, a preset, or a JSON config file.

## Limitations

- Executes operations; it does not author new ones — use `new-operation`.
- For read-only "what would this do" analysis, prefer `run-validators` (or
  add `-an` to a CLI run); the validators already aggregate analysis results.
- Won't trigger a build — if `_build/` is missing, use the `build` skill.
