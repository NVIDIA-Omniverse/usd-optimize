---
name: skills-index
description: "Cross-skill index: when to use each skill, composition, and cross-references."
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Scene Optimizer skills index

Each `<name>/SKILL.md` is a self-contained workflow doc. **Always read past
the first ~50 lines** — load-bearing details (REQUIRES_MESH, base-rule
handling, pipeline order, etc.) often live in later sections. Each skill
opens with a "What this skill covers" block listing every section, so a
single-block scan tells you what's where without reading the body.

Claude Code invokes a skill via the `/<name>` slash command or the `Skill`
tool. Other agents read `.agents/skills/<name>/SKILL.md` and follow it
directly.

## When to use which skill

| Skill | Use when |
|---|---|
| [`build`](build/SKILL.md) | Building Scene Optimizer from source via `repo.sh`. Required before running validators / operations against a dev tree. |
| [`prebuilt-package`](prebuilt-package/SKILL.md) | Installing a published binary drop (no source, no `repo.sh`). |
| [`testing`](testing/SKILL.md) | Running the `cpp` / `python` test suites. |
| [`validators`](validators/SKILL.md) | **Reference doc** for validator infrastructure — programmatic API, REQUIRES_MESH cache, entry-point allow-list, CLI gotchas, `libusd` alignment. Read when the day-to-day skills (`run-validators` / `interpret-validators`) point you here. |
| [`run-validators`](run-validators/SKILL.md) | Validating a USD asset. Drives `tools/perf_validators/run.{sh,bat}`, writes artifacts to a stable per-asset dir. |
| [`interpret-validators`](interpret-validators/SKILL.md) | Reading saved validator artifacts and presenting a structured report. Tier-classifies every rule (T1 = run op, T2 = run + tune, T3 = manual) and answers follow-up questions ("Which prims are affected by X?", "How do I fix Y?", "Show me only base rules"). |
| [`run-operations`](run-operations/SKILL.md) | Running a chain of optimization operations on a USD asset and saving the result. Drives `tools/perf_operations/run.{sh,bat}`. Closes the loop after `interpret-validators` recommends fixes. |
| [`tune-parameters`](tune-parameters/SKILL.md) | Interactive parameter tuning for a single operation. Loads `.agents/operations/<key>.md` and iterates with the user. Also has a guide-authoring mode for developers. |
| [`create-proxy`](create-proxy/SKILL.md) | Creating a USD proxy mesh sibling for a source prim hierarchy (LOD stand-in). |
| [`deduplicate-hierarchies`](deduplicate-hierarchies/SKILL.md) | Collapsing duplicate prim hierarchies (whole subtrees) into instanceable internal references. Drives the `deduplicateHierarchies` C++ operation, which defaults to `Structural Hash` matching (safe on any asset) and also supports a faster `Display Name` mode for CAD-imported scenes with reliable names. |
| [`inspect-asset`](inspect-asset/SKILL.md) | Quick non-destructive USD stage inspection — stage metadata, prim/mesh/vertex/material counts, bounding box, animation presence. Use before any optimization workflow or when the user asks "what's in this file?". |
| [`compare-stages`](compare-stages/SKILL.md) | Structured diff between two USD stages (e.g. before/after optimization). Reports file size, prim count, mesh count, vertex count, material count deltas, and optionally validator summary diffs. |
| [`debug-operation`](debug-operation/SKILL.md) | Troubleshoot a failing or unexpected operation — log reading, argument key verification, verbose/analysis mode, and common failure patterns by operation family. |
| [`new-operation`](new-operation/SKILL.md) | Scaffold a new operation plugin end-to-end: C++ source, premake, test file, operation guide, INDEX.md update, and optional validator wrapper. |

## End-to-end optimization loop

The validator / operation skills are designed to compose:

```
/inspect-asset <asset>                       — quick stage overview (optional)
   ↓
/run-validators <asset>
   ↓ writes CSV/JSON/log artifacts
/interpret-validators <asset>
   ↓ tier-classified report + per-rule fix recommendations
[pick a pipeline from .agents/operations/PIPELINES.md
 or build a custom op list per the report]
   ↓
/run-operations <asset> --pipeline <name>   (or --config '<json>')
   ↓ saves <asset>.optimized.usdc
/compare-stages <asset> <asset>.optimized.usdc  — structured before/after diff
   ↓
/run-validators <asset>.optimized.usdc       — verify the rules dropped
```

If an operation fails or does nothing, use `/debug-operation` to triage.

For the canonical Python invocation reference (used inside several skill
prompts), see [`.agents/operations/INVOCATION.md`](../operations/INVOCATION.md).
For curated op chains by bottleneck, see [`.agents/operations/PIPELINES.md`](../operations/PIPELINES.md).
For per-operation parameter guides, see [`.agents/operations/`](../operations/).

## Cross-references at a glance

- **`run-validators` → `validators`** for REQUIRES_MESH details, programmatic API, CLI gotchas.
- **`interpret-validators` → `validators`** for CSV schema, family classification.
- **`interpret-validators` → `tune-parameters`** when a T2 rule needs iteration.
- **`interpret-validators` → `run-operations`** when the user asks "apply the recommended fixes".
- **`interpret-validators` → `.agents/operations/INVOCATION.md`** for the canonical Python invocation snippet (replaces the older hallucinated `SceneOptimizer.run_operation` signature).
- **`run-operations` → `.agents/operations/PIPELINES.md`** for named-pipeline definitions.
- **`run-operations` → `.agents/operations/INVOCATION.md`** for the Python API surface.
- **`tune-parameters` → `.agents/operations/<key>.md`** as the per-operation source of truth.
- **`tune-parameters` → `run-operations`** to execute a tuned config.
- **`build` ↔ `prebuilt-package`** — pick exactly one source for the runtime.
- **`testing` → `build`** — tests run against a built tree.
- **`create-proxy` → `.agents/operations/`** for the underlying operations it composes (`merge`, `decimateMeshes`, `pivot`, etc.).
- **`deduplicate-hierarchies` → `.agents/operations/deduplicateHierarchies.md`** for the C++ operation's parameters, correctness assumption, and starting configs.
- **`deduplicate-hierarchies` → `.agents/operations/PIPELINES.md` (`hierarchy-dedup` pipeline)** for the recommended two-step pairing with `deduplicateGeometry`.
- **`inspect-asset`** — standalone; used before `run-validators`, `run-operations`, or `tune-parameters` to gather stage context.
- **`compare-stages`** — uses `run-validators` artifacts for validator diffs; pairs with `run-operations` for before/after comparison.
- **`debug-operation` → `run-operations`** for the errors table; → `tune-parameters` when the op runs but output quality needs iteration.
- **`new-operation` → `PLUGINS.md`** for the full plugin API; → `validators` for the validator wrapper recipe; → `tune-parameters` (guide authoring mode) for auto-generating the operation guide.

## Philosophy

- **Skills are workflows, not references.** A workflow tells you what to do step-by-step. The reference docs are `.agents/operations/<key>.md` (per-op tuning) and `.agents/operations/INVOCATION.md` / `PIPELINES.md` (cross-cutting).
- **Skills cite each other deliberately.** When `run-validators` says "see `validators/SKILL.md` for X", that's because the canonical answer is there. Follow the pointer.
- **Long skills are long for a reason.** `interpret-validators` is ~480 lines because it covers asset mode + CSV mode + summary mode + 7 follow-up questions + a 30-rule reference table for both rule families. The "What this skill covers" block at the top tells you where each topic lives.
