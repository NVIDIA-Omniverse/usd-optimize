---
name: skills-index
description: "Cross-skill index: when to use each skill, composition, and cross-references."
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Usd Optimize skills index

Each `<name>/SKILL.md` is a self-contained workflow doc. **Always read past
the first ~50 lines** — load-bearing details often live in later sections. Each
skill opens with a "What this skill covers" block listing every section, so a
single-block scan tells you what's where without reading the body.

Claude Code invokes a skill via the `/<name>` slash command or the `Skill`
tool. Other agents read `.agents/skills/<name>/SKILL.md` and follow it directly.

**Sources of truth (not skills — link to these, don't duplicate them):**

- `docs/operations/<key>.rst` — per-operation reference (overview, tuning
  guidance, argument table), generated from each operation's C++
  `getDocumentation()` and `addArgument()` declarations.
- `docs/cli.rst` — the `usdOptimize` CLI (how operations are run).
- `docs/choosing-operations.rst` — which operation addresses which goal.
- `docs/performance-validators.rst` — the validator rule list and what each checks.
- `config_presets/*.json` — ready-made operation stacks run via the CLI.

## When to use which skill

| Skill | Use when |
|---|---|
| [`build`](build/SKILL.md) | Building Usd Optimize from source via `repo.sh`. Required before running validators / operations against a dev tree. |
| [`prebuilt-package`](prebuilt-package/SKILL.md) | Installing a published binary drop (no source, no `repo.sh`). |
| [`testing`](testing/SKILL.md) | Running the `cpp` / `python` test suites. |
| [`run-validators`](run-validators/SKILL.md) | Validating a USD asset (read-only by default); pass `--fix` to auto-apply fixable issues — non-destructive, writes a new file (`--fix-in-place` to overwrite the source). Drives `tools/validators/run.{sh,bat}`, writes a per-issue CSV. Also covers the auto-fix model, programmatic API, `nvidia_usd_validate` CLI, and adding new validators. |
| [`interpret-validators`](interpret-validators/SKILL.md) | Triaging the issues `--fix` could **not** auto-resolve. Tier-classifies the remaining rules, lists affected prims, and recommends the operation + parameters that need a user decision. |
| [`config-presets`](config-presets/SKILL.md) | Choosing and running a ready-made preset operation stack from `config_presets/`. Use when you want a known-good starting point for a common optimization goal. |
| [`run-operations`](run-operations/SKILL.md) | Running operations on a USD asset with the `usdOptimize` CLI — inline ops, a preset config, or a custom JSON config. Closes the loop after `interpret-validators` flags a manual fix. |
| [`tune-parameters`](tune-parameters/SKILL.md) | **[EXPERIMENTAL]** Interactive parameter tuning for a single operation. Reads `docs/operations/<key>.rst` and iterates with the user; also has an improve-the-docs mode for extending an operation's `getDocumentation()`. |
| [`create-proxy`](create-proxy/SKILL.md) | Creating a USD proxy mesh sibling for a source prim hierarchy (LOD stand-in). |
| [`deduplicate-hierarchies`](deduplicate-hierarchies/SKILL.md) | Collapsing duplicate prim hierarchies (whole subtrees) into instanceable internal references. Drives the `deduplicateHierarchies` operation via the CLI. |
| [`inspect-asset`](inspect-asset/SKILL.md) | Quick non-destructive USD stage inspection — metadata, prim/mesh/vertex/material counts, bounding box, animation presence. Use before any optimization workflow. |
| [`compare-stages`](compare-stages/SKILL.md) | Structured diff between two USD stages (e.g. before/after optimization) — file size, prim/mesh/vertex/material deltas, optional validator-summary diffs. |
| [`debug-operation`](debug-operation/SKILL.md) | Troubleshooting a failing or no-op operation — CLI verbose/analysis flags, argument-key verification, failure patterns by family. |
| [`new-operation`](new-operation/SKILL.md) | Scaffolding a new operation plugin: C++ source, premake, test, `getDocumentation()` docs, optional validator wrapper. |
| [`new-validator`](new-validator/SKILL.md) | Adding a new performance-validator rule that wraps a Usd Optimize analysis-mode operation. |
| [`writing-skills`](writing-skills/SKILL.md) | **Meta-skill** for authoring or revising a skill so it matches house conventions and is registered in this index. |

## End-to-end optimization loop

The validator / operation skills compose. Validation is read-only; `--fix` is
opt-in and non-destructive — it writes fixes to a new file by default, so the
original `<asset>` stays intact for the before/after diff:

```
/inspect-asset <asset>                          — quick stage overview (optional)
   ↓
/run-validators <asset>                         — validate (read-only); writes a per-issue CSV
   ↓ opt into --fix to repair (writes a NEW file; source kept):
   ↓   /run-validators <asset> --fix          (default: <stem>.fixed<ext> beside <asset>)
   ↓   --fix-output must stay in <asset>'s directory: only the root layer is copied
   ↓   --fix-in-place overwrites <asset>; --fix-overwrite replaces an existing output
/interpret-validators <fixed.usd>               — triage ONLY the issues --fix could not resolve
   ↓ for each: the op + parameters that need a user decision
/run-operations <fixed.usd> -c config_presets/<name>.json   (or a custom JSON config / inline -o)
   ↓ writes optimized.usd via the usdOptimize CLI
/compare-stages <asset> optimized.usd           — structured before/after diff
   ↓
/run-validators optimized.usd                   — verify the targeted rules dropped
```

When the user opts into `--fix`, most issues are resolved by that step;
`interpret-validators` and `run-operations` only handle what needs human
judgment (which prims, how aggressive, an accepted trade-off). If an operation
fails or does nothing, use `/debug-operation`.

For the CLI flags, see `docs/cli.rst`. For preset stacks, see
`config_presets/`. For per-operation parameters and tuning guidance, see
`docs/operations/<key>.rst`.

## Cross-references at a glance

- **`run-validators` → `interpret-validators`** for the issues `--fix` left unresolved.
- **`interpret-validators` → `run-operations`** when an unfixed issue needs a manual operation.
- **`interpret-validators` / `run-operations` → `docs/operations/<key>.rst`** for op arguments and tuning guidance.
- **`run-operations` → `config-presets`** for ready-made operation stacks; **→ `docs/cli.rst`** for the CLI surface.
- **`config-presets` → `run-operations`** for CLI flags, error handling, and destructive-op confirmations; **→ `tune-parameters`** for iterating a single operation's parameters.
- **`tune-parameters` → `docs/operations/<key>.rst`** as the per-operation source of truth; **→ `run-operations`** to execute a tuned config.
- **`build` ↔ `prebuilt-package`** — pick exactly one source for the runtime.
- **`testing` → `build`** — tests run against a built tree.
- **`create-proxy` / `deduplicate-hierarchies` → `docs/operations/`** for the operations they compose.
- **`inspect-asset`** — standalone; used before `run-validators` / `run-operations` / `tune-parameters`.
- **`compare-stages`** — uses `run-validators` summaries for validator diffs; pairs with `run-operations`.
- **`debug-operation` → `run-operations`** (errors) and **→ `tune-parameters`** (output-quality iteration).
- **`new-operation` → `PLUGINS.md`** (plugin API), **→ `getDocumentation()`** for op docs, **→ `new-validator`** for the validator wrapper recipe.
- **`new-validator` → `new-operation`** (create the backing operation), **→ `run-validators`** (validator infrastructure and programmatic API).
- **`writing-skills` → this `README.md`** — every new skill must add a row here and any cross-reference bullets.

## Philosophy

- **Skills are workflows, not references.** A workflow tells you what to do
  step-by-step. The references are the `docs/` files and `config_presets/`
  listed at the top — link to them, don't copy them.
- **Operation facts live in the C++.** Per-operation docs are generated from
  `getDocumentation()` + `addArgument()` into `docs/operations/*.rst`. To change
  what the docs say, change the operation source and regenerate
  (`./repo.sh docs_gen --autogen_only`).
- **Repair is opt-in.** `run-validators` is read-only by default; add `--fix`
  when repairs are requested — it's non-destructive, writing a new file by
  default (`--fix-in-place` to overwrite the source). The manual skills handle
  only what needs a human decision.
- **Skills cite each other deliberately.** When one skill points at another (or
  at a `docs/` reference), that's because the canonical answer lives there.
