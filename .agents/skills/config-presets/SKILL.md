---
name: config-presets
description: Choose and run a ready-made preset operation stack from config_presets/. Use when you want a known-good starting point for a common optimization goal rather than assembling a custom config.
allowed-tools: Shell, Read
metadata:
  author: NVIDIA Corporation
  version: "1.0.1"
  tags: [usd, optimization, presets, config]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# config-presets — Ready-made operation stacks

Each `config_presets/*.json` file is a ready-to-run operation stack: a JSON
array of operations applied to a stage in order. Use one to get a known-good
baseline before considering custom tuning.

## What this skill covers

- **Usage** — the one-line invocation.
- **Preset table** — the six presets, their effect, and whether they are lossless.
- **Step 1** — pick a preset for the user's goal.
- **Step 2** — run the preset.
- **Step 3** — customize if needed.
- **Troubleshooting** — symptoms, causes, and what to tell the user.
- **Purpose / Prerequisites / Limitations** — scope of the skill.

Companion skills: [`run-operations`](../run-operations/SKILL.md) (CLI usage,
flags, error handling), [`tune-parameters`](../tune-parameters/SKILL.md)
(iterate a single operation's parameters after running a preset).

---

## Usage

```bash
# POSIX
_build/linux-x86_64/release/bin/usdOptimize -i in.usd -c config_presets/safe-cleanup.json -s -w out.usd
```

```powershell
# Windows: use the .bat launcher, not usdOptimize.exe
_build\windows-x86_64\release\bin\usdOptimize.bat -i in.usd -c config_presets\safe-cleanup.json -s -w out.usd
```

Presets live in `config_presets/` at the repo root, and ship in the same
directory in a packaged drop. List them with `ls config_presets/`.

## Preset table

| Preset | Effect | Lossless? |
|---|---|---|
| `safe-cleanup.json` | Extents, prune empty leaves, dedup geometry, dedup materials, drop redundant time samples. | Yes |
| `memory-reduction.json` | Dedup geometry into instances, dedup materials, prune leaves. | Yes |
| `load-time-reduction.json` | Author extents, prune leaves, drop redundant time samples, dedup materials. | Yes |
| `hierarchy-dedup.json` | Collapse duplicate hierarchies, then dedup remaining geometry. | Yes |
| `data-quality-baseline.json` | Generate normals, mesh cleanup (merge verts, manifold, coorient), extents. | Bounded loss |
| `mesh-count-reduction.json` | Mesh cleanup, dedup geometry, remove small geometry, conservative decimation (error-bounded by `maxMeanError`, not `reductionFactor`). | Bounded loss |

When the user is unsure, recommend `safe-cleanup` — it is conservative and
fully lossless.

## Step 1 — pick a preset

Match the user's goal to a preset:

- General cleanup with no risk: `safe-cleanup`
- Reduce runtime memory: `memory-reduction`
- Reduce load time: `load-time-reduction`
- Duplicate subtrees dominate the scene: `hierarchy-dedup`
- Fix mesh quality issues: `data-quality-baseline`
- Reduce draw-call or polygon count: `mesh-count-reduction`

If the goal spans multiple presets (e.g. memory AND load time), run them in
sequence or combine their operation lists into a custom config.

## Step 2 — run the preset

```bash
_build/<platform>/<config>/bin/usdOptimize \
    -i input.usd \
    -c config_presets/<preset>.json \
    -s \
    -w output.usd
```

`<platform>` is e.g. `linux-x86_64`; `<config>` is `release` or `debug`.
`-s` captures before/after stats; `-r` emits a per-operation report. See
[`docs/cli.rst`](../../../docs/cli.rst) for the full flag list.

## Step 3 — customize if needed

To adjust a preset, copy the JSON file and edit the operation list. Per-operation
arguments and defaults are in [`docs/operations/<key>.rst`](../../../docs/operations/);
guidance on which operations help which goal is in
[`docs/choosing-operations.rst`](../../../docs/choosing-operations.rst).

For interactive parameter tuning on a single operation, use
[`/tune-parameters`](../tune-parameters/SKILL.md).

To verify a preset did what you expected, re-run with `-an` for analysis only,
or compare before/after with [`compare-stages`](../compare-stages/SKILL.md).
The `-s` stats block in the log reports prim and mesh counts either side.
Don't pair it with `-r`, which moves the block out of the log,
into a `usdOptimize.*` report in the system temp directory,
and prints the path of a second report that lacks it.

## Troubleshooting

| Symptom | Cause | What to tell the user |
|---|---|---|
| Binary missing under `_build/.../bin/` | Repo not built | Point at the `build` skill; build first. |
| `config_presets/<name>.json` not found | Run from the wrong directory, or a drop older than 1.2.0 (presets were not packaged before then) | Run from the repo/drop root; check `ls config_presets/`. |
| Windows: exit `-1073741515` | Ran `usdOptimize.exe` directly; it cannot resolve its DLLs | Use `bin\usdOptimize.bat`. `PATHEXT` picks the `.exe` first, so name the `.bat`. |
| `Failed to open stage` | Bad or unsupported input path | Verify the path is a real USD stage (`inspect-asset`). |
| CLI exits non-zero mid-chain | One operation in the stack failed | Surface the failing op line from the log; the rest of the stack did not run. |
| Ran clean but the stage looks unchanged | The preset's operations found nothing to do on this asset | Confirm with `run-validators` that the issues the preset targets are actually present. |
| `Unknown argument '<key>' ... ignoring it` | A hand-edited preset has a typo'd argument name | Check the key against `docs/operations/<key>.rst`; the op ran with its default. |
| `Could not find operation <name>` | The build cannot resolve that operation name; `-c` skips it, runs the rest and still exits 0 | Grep the whole log for `Could not find operation`. No shipped preset names a Python-plugin op, so this normally means a hand-edited preset or a drop predating the operation. |

## Purpose

Pick and run a ready-made operation stack from `config_presets/`, so a common
optimization goal can be met without assembling a config by hand. Use it as the
known-good baseline before reaching for custom tuning.

## Prerequisites

- A built repo (or a drop) so `bin/usdOptimize` exists — see the `build` skill.
- A USD asset (`.usd` / `.usda` / `.usdc` / `.usdz`).
- A goal that maps to one of the six presets; otherwise use `run-operations`.

## Limitations

- Presets are fixed stacks: they expose no per-operation arguments. To change a
  value, copy the JSON and edit it (Step 3), then run it via `run-operations`.
- Two presets are not lossless (`data-quality-baseline`, `mesh-count-reduction`).
  Never run those in place on a source asset without a backup.
- Running two presets in sequence repeats operations they share, which is
  wasteful but not harmful. Combining their operation lists is usually better.
