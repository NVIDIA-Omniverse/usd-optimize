---
name: deduplicate-hierarchies
description: Collapse duplicate prim hierarchies into instanceable internal references. Use when deduplicating subtrees or folding repeated prims into prototypes.
version: "1.0.0"
allowed-tools: Bash, Read
metadata:
  author: NVIDIA Corporation
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
> **Python invocation.** Examples below use `python3` (POSIX). On Windows use
> `py -3` (the Python launcher) or the build's bundled interpreter at
> `_build\target-deps\python\python.exe`.
>
> **Windows shell.** Snippets target PowerShell. cmd.exe equivalents are
> obvious — `$Var` → `%VAR%`, backtick line continuation → `^`.
> `repo.bat` is cmd-style internally but can be invoked from any shell.

> **Safety note.** Duplicates are identified by subtree shape, prim
> types, and authored property names, then refined by verifying all
> property **values** match (excluding xformOps on the root prim, which
> represent placement). Descendant transforms must match exactly
> (tolerance only applies to float arrays like points/normals/UVs and
> scalar float/double values, not to matrices, scalar vectors, or
> quaternions). It will only merge prims that are truly identical. This
> is safe on any asset. The `paths` argument can scope the run to a
> known-safe subtree.

Find duplicate prim hierarchies (level by level under the default prim)
and replace duplicate subtrees with **internal instanceable references** —
duplicates become refs to the first instance (the prototype). All geometry
stays in the main stage file.

This is **not** the same as Scene Optimizer's `deduplicateGeometry`
operation, which only handles individual meshes. This skill works on
entire prim hierarchies matched by structure.

## What this skill covers

Sections below are ordered for execution — read past **Step 3** before
concluding saving or pipeline details are missing. Search for keywords like
`tolerance`, `paths`, `deduplicateGeometry`, `analysisMode`, `ignoreShaderOutputs`,
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
- **See also** — operation guides (`deduplicateHierarchies`, `deduplicateGeometry`).
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

The operation is invoked via Scene Optimizer's standalone runner:
`omni.scene.optimizer.core.scripts.standalone.execute_commands_from_json`.
No Kit dependency.

Confirm the operation is registered in the build:

```python
from omni.scene.optimizer.core import SceneOptimizerCore
print("deduplicateHierarchies" in SceneOptimizerCore.getInstance().getOperations())
```

(POSIX) `python3 -c "..."`, (Windows PowerShell) `py -3 -c "..."` or
`_build\target-deps\python\python.exe -c "..."`.

## Step 3 — Run the operation

Assemble a config for `execute_commands_from_json`. The **canonical pipeline
is two steps**: our hierarchy-level dedup, then `deduplicateGeometry` for any
remaining per-mesh duplicates that didn't fall out of the hierarchy pass.
Pair them in a single config so each step sees the previous step's edits.

Path strings below use forward slashes — Python accepts those on Windows and
POSIX alike. Replace the placeholders with absolute or repo-relative paths
that fit the user's environment; do not embed Windows drive letters or raw
strings unless the user has supplied them explicitly.

```python
import json
from pxr import Usd
from omni.scene.optimizer.core.scripts.standalone import execute_commands_from_json

INPUT_USD  = "path/to/asset.usd"
OUTPUT_USD = "path/to/asset_deduped.usd"

config = [
    # 1. Hierarchy-level dedup: replace duplicate prim subtrees with
    #    instanceable internal references to the first instance.
    {
        "operation": "deduplicateHierarchies",
        # "tolerance": 0.001,                    # float tolerance for vertex drift
        # "paths": ["/World/MySubtree"],         # optional subtree restriction
        # "ignoreShaderOutputs": True,           # skip outputs:* during value
        #                                        # comparison; default True
    },
    # 2. Per-mesh dedup: catch identical mesh duplicates that the
    #    hierarchy pass didn't fold (different parents, same geometry).
    #    duplicateMethod=2 -> Instanceable Reference.
    {
        "operation": "deduplicateGeometry",
        "duplicateMethod": 2,
        "tolerance": 0.001,
    },
]

# Opt in to per-level / per-group logging by prepending:
#   {"operation": "executionContext", "verbose": True}
# (context flag, not an op argument)

stage = Usd.Stage.Open(INPUT_USD)
ok = execute_commands_from_json(stage, json.dumps(config))
if not ok:
    raise RuntimeError("deduplicateHierarchies pipeline failed — check SO log")

# IMPORTANT: save via the root layer, NOT stage.Export().
# stage.Export() flattens the composed stage and rewrites Usd-instance
# prototype prims to synthetic root-level names like /Flattened_Prototype_N,
# which is technically equivalent but loses the authored prototype paths.
# Root-layer export preserves them.
stage.GetRootLayer().Export(OUTPUT_USD)
```

For a hierarchy-only run (skip per-mesh dedup, faster, less aggressive),
drop the second config entry. Use this when the user has already run
`deduplicateGeometry` upstream, or only wants the assembly-level rollup.

Environment setup (SO on `PYTHONPATH`, native libs on `PATH`/`LD_LIBRARY_PATH`)
is the same as for any other Scene Optimizer pipeline — defer to
`.agents/skills/build/SKILL.md` for a source-tree build or to the
`prebuilt-package` skill for a packaged runtime. Don't duplicate environment
setup here.

## Step 4 — Report

After the run completes, summarise for the user:

- **Number of prototype groups found** and **total duplicate prims replaced**.

If the user asks follow-ups about which prims were affected, run the
operation in analysis mode to retrieve the `{prototype: [duplicates]}` map.

## Pre-flight checks

1. **Default prim exists.** The operation traverses from the stage's default
   prim; no default prim → no duplicates → no work done. Open the file
   briefly and confirm `stage.GetDefaultPrim()` is valid.

## Common pitfalls

- **Material-related prims are skipped.** `Material`, `Shader`, `NodeGraph`
  prims, the `Looks` / `Materials` / `Mesh` scopes, and prims with
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

- `.agents/operations/deduplicateHierarchies.md` — the C++ operation guide
  (parameter reference, starting configs).
- `.agents/operations/deduplicateGeometry.md` — per-mesh deduplication, the
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

- Material-related prims (`Material`, `Shader`, `NodeGraph`, `Looks`,
  `Materials` scopes, and `Diffuse*` / `Specular*`-prefixed prims) are
  intentionally skipped from the duplicate scan.
- Prims with already-authored references or payloads are excluded
  from grouping — they count as "matched" so they don't reappear at
  deeper levels but are not themselves replaced.
- This skill does **not** flatten composition arcs. Run an upstream
  flatten if you need a self-contained output.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Operation runs but reports 0 prototype groups | Stage has no default prim, or all subtrees are unique. | Set a default prim (`stage.SetDefaultPrim(...)`) and re-run. Confirm with `analysisMode: 1` to see the candidate map. |
| Output uses synthetic `/Flattened_Prototype_N` paths | Saved via `stage.Export()` instead of `stage.GetRootLayer().Export()`. | Use root-layer export — see Step 3. |
| Fewer duplicates found than expected | Floating-point drift from re-export or tessellation may push otherwise-identical subtrees out of bitwise match. | Increase `tolerance` (only affects float arrays and scalar float/double; integer topology always requires exact match). |
| `RuntimeError: deduplicateHierarchies pipeline failed` | Operation rejected the config (bad arg key) or hit a USD I/O error. | Check the SO log; verify argument keys against `.agents/operations/deduplicateHierarchies.md`. |
| Per-mesh duplicates remain after the run | This op only handles whole hierarchies. | Pair with `deduplicateGeometry` (already in the canonical pipeline shown in Step 3). |

