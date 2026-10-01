---
name: deduplicate-hierarchies
description: Collapse duplicate prim hierarchies into instanceable internal references. Use when deduplicating subtrees or folding repeated prims into prototypes.
allowed-tools: Bash, Read
metadata:
  author: NVIDIA Corporation
  version: "1.0.1"
  tags: [usd, deduplication, hierarchy, instancing]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Deduplicate Hierarchies

> **Invocation.** This is the `deduplicate-hierarchies` skill.
> Claude Code also exposes it as the alias
> `/deduplicate-hierarchies`. Codex and other agents invoke
> it by name. Don't reference the alias-only form when writing for a
> non-Claude agent.
>
> **Platform paths.** Examples use the POSIX CLI path
> `_build/linux-x86_64/release/bin/usdOptimize`. On Windows use
> `_build\windows-x86_64\release\bin\usdOptimize.bat` (the `.bat`, not the
> `.exe`, which cannot resolve its DLLs); `$Var` → `%VAR%`.

> **Safety note.** Duplicates are identified by subtree shape, prim
> types, and authored property names, then refined by verifying all
> property **values** match (excluding xformOps on the root prim, which
> represent placement). Floating-point values — scalars, vectors,
> matrices (including descendant `xformOp:transform`), quaternions, and
> arrays of them — are compared within `tolerance`; integer/topology,
> string, token and bool values always require an exact match. Descendant
> transforms that differ by more than `tolerance` still block the merge.
> Set `tolerance=0` for a bitwise-exact compare. It will only merge prims
> that are truly identical (within tolerance). This is safe on any asset.
> The `paths` argument can scope the run to a known-safe subtree.

Find duplicate prim hierarchies (level by level under the default prim)
and replace duplicate subtrees with **internal instanceable references** —
duplicates become refs to the first instance (the prototype). All geometry
stays in the main stage file.

This is **not** the same as Usd Optimize's `deduplicateGeometry`
operation, which only handles individual meshes. This skill works on
entire prim hierarchies matched by structure.

## What this skill covers

Sections below are ordered for execution — read past **Step 3** before
concluding saving or pipeline details are missing. Search for keywords like
`tolerance`, `paths`, `deduplicateGeometry`, `analysis mode`, `ignoreShaderOutputs`,
or `GetRootLayer().Export` to jump.

- **Inputs** — asset path and optional `--paths` scoping.
- **Step 1** — validate the USD path exists and has a `.usd*` suffix.
- **Step 2** — verify `deduplicateHierarchies` is registered in the build.
- **Step 3** — canonical two-op pipeline (`deduplicateHierarchies` then
  `deduplicateGeometry`), root-layer export, environment pointers.
- **Step 4** — what to report after a successful run (and analysis mode for detail).
- **Pre-flight checks** — default prim requirement.
- **Common pitfalls** — skipped material-ish prims, refs/payloads behavior.
- **Verification** — instanceable flags, prototype paths, flat export pitfalls.
- **See also** — operation references (`docs/operations/deduplicateHierarchies.rst`, `deduplicateGeometry.rst`).
- **Purpose** — one-paragraph rationale.
- **Prerequisites** — build, default prim, output path conventions.
- **Limitations** — what the op does not merge or flatten.
- **Troubleshooting** — symptom / cause / fix table.

Companion skills: `run-operations` (general op driver), `prebuilt-package` /
`.agents/skills/build/SKILL.md` (runtime setup), `debug-operation` (failing ops).

## Inputs

| Input | Required | Default | Meaning |
|---|---|---|---|
| `<asset>` | yes | — | Path to a `.usd` / `.usda` / `.usdc` / `.usdz` stage file. |
| `--paths <prim path,...>` | no | (whole stage) | Restrict the search to one or more subtree roots. |

If no asset path is provided, ask:

> "Which USD file should I deduplicate? Please provide the full path."

## Step 1 — Validate the input

A path that doesn't exist or doesn't end in `.usd*` should be rejected
before any work.

## Step 2 — Verify the operation is available

Operations run through the `usdOptimize` CLI (see `run-operations` and
`docs/cli.rst`). Confirm the operation is built — it appears in the CLI's
"Available Operations" list:

```bash
_build/<platform>/<config>/bin/usdOptimize --help | grep deduplicateHierarchies
```

## Step 3 — Run the operation

The **canonical pipeline is two steps**: hierarchy-level dedup, then
`deduplicateGeometry` for any remaining per-mesh duplicates that didn't fall
out of the hierarchy pass. Put both in one JSON config so the second step sees
the first step's edits. This is exactly the shipped
`config_presets/hierarchy-dedup.json` preset:

```json
[
    {"operation": "deduplicateHierarchies"},
    {"operation": "deduplicateGeometry", "duplicateMethod": 2, "tolerance": 0.001}
]
```

Run it (the binary is self-contained — no `LD_LIBRARY_PATH` / `PYTHONPATH`
setup needed):

```bash
BIN=_build/linux-x86_64/release/bin/usdOptimize
"$BIN" -i path/to/asset.usd -c config_presets/hierarchy-dedup.json -w path/to/asset_deduped.usd
```

Optional `deduplicateHierarchies` arguments (full reference:
`docs/operations/deduplicateHierarchies.rst`): `tolerance` (float value
tolerance; `0` = bitwise-exact), `paths` (restrict to subtree roots),
`ignoreShaderOutputs` (default `true`), `maxDepth`. Pass them with a custom
config file, or inline with `-o deduplicateHierarchies -a tolerance=0`.

**Do not pass `-fl` / `--flatten`.** The CLI's default `-w` exports the root
layer, which preserves the authored prototype prim names. Flattening rewrites
Usd-instance prototypes to synthetic `/Flattened_Prototype_N` names —
technically equivalent, but it loses the authored paths.

For a hierarchy-only run (faster, less aggressive — use when
`deduplicateGeometry` already ran upstream, or you only want the assembly-level
rollup), use a one-entry config or `-o deduplicateHierarchies` directly.

## Step 4 — Report

After the run completes, summarise for the user:

- **Number of prototype groups found** and **total duplicate prims replaced**
  (use `-r` for a report, or `-s` for before/after stats).

If the user asks which prims were affected, re-run with `-an` (analysis mode)
to retrieve the `{prototype: [duplicates]}` map without mutating the stage.

## Pre-flight checks

1. **Default prim exists.** The operation traverses from the stage's default
   prim; no default prim → no duplicates → no work done. Open the file
   briefly and confirm `stage.GetDefaultPrim()` is valid.

## Common pitfalls

- **Material-related prims are skipped.** `Material`, `Shader`, `NodeGraph`,
  `GeomSubset` prims, the `Looks` / `Materials` scopes, and prims with
  texture-name prefixes (`Diffuse`, `Specular`, etc.) are intentionally
  excluded from the duplicate scan. If a hierarchy you expected to be
  deduped is being skipped, check it doesn't fall under one of those
  predicates.
- **Existing references / payloads.** Prims that already have authored
  references or payloads are excluded from the duplicate group. They count
  against the "matched" set so they don't re-appear at deeper levels.

## Verification

- **Hierarchy reduced.** Count subtree-rooted prim groups before and
  after. The post-run count of distinct subtree shapes should be lower.
- **Instanceable flag set.** Every duplicate prim should report
  `prim.IsInstanceable() == True` and have one internal reference whose
  target is the prototype path.
- **Prototype names preserved.** Inspect the saved layer (e.g. `usdcat`
  or open the file in a text-friendly viewer for `.usda`/`.usd` ASCII).
  Prototype prims should keep their authored names. Synthetic root-level
  names like `/Flattened_Prototype_N` indicate the file was saved with
  `stage.Export()` instead of `stage.GetRootLayer().Export()` — see Step 3.
- **Analysis mode.** Run the operation in analysis mode to get the
  `{prototype: [duplicates]}` map without mutating the stage. Spot-check
  a couple of the reported paths in `usdview`.

## See also

- `docs/operations/deduplicateHierarchies.rst` — the operation reference
  (parameter list, tuning guidance, starting configs).
- `docs/operations/deduplicateGeometry.rst` — per-mesh deduplication, the
  finer-grained sibling.

## Purpose

Reduce stage size and load time by collapsing duplicate prim hierarchies
(whole subtrees) into instanceable internal references. The first
occurrence becomes the prototype; subsequent identical subtrees are
replaced with references to it. Matching is by structural hash plus a
property-value comparison — safe on any asset.

## Prerequisites

- A built repo (`./repo.sh build` or `repo.bat build`) so the
  `deduplicateHierarchies` operation is registered.
- A USD asset with a **default prim** set — the operation traverses
  from the default prim, so a stage without one is a silent no-op.
- A target output path. The skill never overwrites the input file
  unless the user explicitly asks.

## Limitations

- Material-related prims (`Material`, `Shader`, `NodeGraph`, `GeomSubset`,
  the `Looks` / `Materials` scopes, and `Diffuse*` / `Specular*`-prefixed
  prims) are intentionally skipped from the duplicate scan.
- Prims with already-authored references or payloads are excluded
  from grouping — they count as "matched" so they don't reappear at
  deeper levels but are not themselves replaced.
- This skill does **not** flatten composition arcs. Run an upstream
  flatten if you need a self-contained output.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Operation runs but reports 0 prototype groups | Stage has no default prim, or all subtrees are unique. | Set a default prim (`stage.SetDefaultPrim(...)`) and re-run. Confirm with the CLI's `-an` analysis mode to see the candidate map (`analysisMode` is a context flag, not an operation argument -- passing it in a config JSON is silently ignored). |
| Output uses synthetic `/Flattened_Prototype_N` paths | Ran with `-fl` / `--flatten`. | Drop `-fl`; the default `-w` root-layer export preserves prototype names — see Step 3. |
| Fewer duplicates found than expected | Floating-point drift from re-export or tessellation may push otherwise-identical subtrees out of bitwise match. | Increase `tolerance` (only affects float arrays and scalar float/double; integer topology always requires exact match). |
| CLI exits non-zero on the config | A USD I/O error, or the operation itself reported failure. | Check the CLI log. An unknown argument key is **not** a cause — it logs a warning and the operation runs with that argument's default, at exit 0. Verify keys against `docs/operations/deduplicateHierarchies.rst`. |
| Per-mesh duplicates remain after the run | This op only handles whole hierarchies. | Pair with `deduplicateGeometry` (already in the canonical pipeline shown in Step 3). |

