<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# interpret-validators — Step 5 follow-up question playbook

Use the parsed JSON from Step 3 in context. Don't re-run the validator unless asked.

## "Which prims are affected by `<RuleName>`?"

Use the summarizer's `--locations` mode — it emits a flat list of every row
for one rule with severity, message, suggestion, and the normalized prim path.
Default to `--limit 20` for readable initial answers; expand on request.

```bash
# POSIX
python3 tools/perf_validators/summarize_csv.py "$CSV" \
    --rule "<RuleName>" --locations --limit 20
```
```powershell
# Windows (PowerShell)
py -3 tools\perf_validators\summarize_csv.py $Csv `
    --rule "<RuleName>" --locations --limit 20
```

The output includes `total` (full count for the rule) and `by_severity` (full
counts) alongside the truncated `locations` array, so you can present
"showing 20 of 510 affected prims" without re-running. Add `--severity failure`
to filter to just failures, or omit `--limit` to get the full list.

## "How do I fix `<RuleName>`?"

Look up the rule in `rule-reference.md`. Then:

1. **T1** — Print the operation key and recommend running it. Example:
   > `<RuleName>` wraps `<op>`. To apply the fix, invoke the `run-operations`
   > skill (Claude alias: `/run-operations <asset> --config '[{"operation":"<op>", ...}]'`)
   > or call the operation directly via the Python bindings:
   > ```python
   > from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
   > from pxr import Usd
   >
   > stage = Usd.Stage.Open("<asset>")
   > context = ExecutionContext()
   > context.set_stage(stage)
   > success, error, output = SceneOptimizerCore.getInstance().executeOperation(
   >     "<op>", context, {"<arg>": <value>, ...}
   > )
   > stage.GetRootLayer().Save()
   > ```
   > For the full invocation reference (chains via `executeConfig`, JSON
   > pipelines via `standalone.execute_commands_from_json`), see
   > `.agents/operations/INVOCATION.md`. For curated multi-op pipelines
   > organized by bottleneck, see `.agents/operations/PIPELINES.md`.
   > For tuning the parameters, use the `tune-parameters` skill — it loads
   > `.agents/operations/<op>.md` and walks through the knobs.

   Read the relevant section of `.agents/operations/<op>.md` for starting params
   before printing them. Don't duplicate the guide content here.

2. **T2** — Same as T1 but warn that defaults may not fully resolve the issue;
   the user should expect to tune. Recommend the `tune-parameters` skill.

3. **T3** — Explain that the rule is analysis-only (or the fix is a manual
   hierarchy/DCC edit). Point at the operation guide and any related fix-mode
   operations (e.g. `findOccludedMeshes` → use `removePrims` to remove the
   reported paths; `findFlatHierarchies` → use `flattenHierarchy`).

4. **Base rules** — Many wrap the same operation as a Scene Optimizer
   equivalent (see `rule-reference.md`). For stage-metadata or
   external-reference rules, suggest the user fix via USD Python API directly
   and reference the asset-validator suggestion text from the CSV `Suggestion`
   column.

## "Show all `<RuleName>` failures"

Re-summarize **without** `--max-failures-per-rule`, filtered to that rule:

```bash
# POSIX
python3 tools/perf_validators/summarize_csv.py "$CSV" --rule "<RuleName>"
```
```powershell
# Windows (PowerShell)
py -3 tools\perf_validators\summarize_csv.py $Csv --rule "<RuleName>"
```

Iterate the resulting `failures` array (every group, every location) and
print each.

## "Show me `<RuleName>` issues on `<prim_path>`"

Run `--rule X --locations` and filter the resulting `locations` array on the
prim path (substring match in-context after parsing the JSON). The full result
includes message + suggestion per row, so no follow-up call is needed.

## "Show me only base rules" / "only SO rules"

Re-present the Step 4 table with the family filter applied (filter the
summarizer's `rules` array by `family == "base"` or `"SO"`).

## "Re-run validation"

Invoke the `run-validators` skill on the asset (asset mode only — refuse for
direct CSV input since the original asset isn't known). After it finishes,
re-run Steps 3 + 4 of `SKILL.md`.

## "Only check `<RuleName>`" (before a run)

The driver doesn't expose a `--rule` flag. Tell the user we'll run the full
default set and filter the CSV / summarizer output to that rule before
presenting.
