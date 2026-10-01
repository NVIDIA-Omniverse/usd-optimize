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
python3 tools/validators/summarize_csv.py "$CSV" \
    --rule "<RuleName>" --locations --limit 20
```
```powershell
# Windows (PowerShell)
py -3 tools\validators\summarize_csv.py $Csv `
    --rule "<RuleName>" --locations --limit 20
```

The output includes `total` (full count for the rule) and `by_severity` (full
counts) alongside the truncated `locations` array, so you can present
"showing 20 of 510 affected prims" without re-running. Add `--severity failure`
to filter to just failures, or omit `--limit` to get the full list.

## "How do I fix `<RuleName>`?"

Look up the rule in `rule-reference.md`. Then:

   Note: most fixes are already applied by `run-validators --fix`. This flow is
   for the issues that came back **unfixed** (or for a run done without `--fix`).

1. **T1** — Print the operation key and recommend running it. Example:
   > `<RuleName>` wraps `<op>`. To apply the fix, invoke the `run-operations`
   > skill (Claude alias: `/run-operations`), which runs the operation through
   > the `usdOptimize` CLI:
   > ```bash
   > usdOptimize -i <asset> -o <op> -a <arg>=<value> -w <output.usd>
   > ```
   > For a multi-op chain, write the ops to a JSON config and pass `-c <file>`
   > (see `docs/cli.rst`). To tune the parameters, use the `tune-parameters`
   > skill — it reads `docs/operations/<op>.rst` and walks through the knobs.

   Read `docs/operations/<op>.rst` for starting params before printing them.
   Don't duplicate the reference content here.

2. **T2** — Same as T1 but warn that defaults may not fully resolve the issue;
   the user should expect to tune. Recommend the `tune-parameters` skill.

3. **T3** — Explain that the rule is analysis-only (or the fix is a manual
   hierarchy/DCC edit). Point at the operation guide and any related fix-mode
   operations (e.g. `findOccludedMeshes` → use `removePrims` to remove the
   reported paths; `findFlatHierarchies` → use `flattenHierarchy`).

4. **Base rules** — Many wrap the same operation as a Usd Optimize
   equivalent (see `rule-reference.md`). For stage-metadata or
   external-reference rules, suggest the user fix via USD Python API directly
   and reference the usd-validation-nvidia suggestion text from the CSV `Suggestion`
   column.

## "Show all `<RuleName>` failures"

Re-summarize **without** `--max-failures-per-rule`, filtered to that rule:

```bash
# POSIX
python3 tools/validators/summarize_csv.py "$CSV" --rule "<RuleName>"
```
```powershell
# Windows (PowerShell)
py -3 tools\validators\summarize_csv.py $Csv --rule "<RuleName>"
```

Iterate the resulting `failures` array (every group, every location) and
print each.

## "Show me `<RuleName>` issues on `<prim_path>`"

Run `--rule X --locations` and filter the resulting `locations` array on the
prim path (substring match in-context after parsing the JSON). The full result
includes message + suggestion per row, so no follow-up call is needed.

## "Show me only base rules" / "only Usd Optimize rules"

Re-present the Step 4 table with the family filter applied (filter the
summarizer's `rules` array by `family == "base"` or `"Usd Optimize"`).

## "Re-run validation"

Invoke the `run-validators` skill on the asset (asset mode only — refuse for
direct CSV input since the original asset isn't known). After it finishes,
re-run Steps 3 + 4 of `SKILL.md`.

## "Only check `<RuleName>`" (before a run)

Pass `-r <RuleName>` to the driver (repeatable). It strips only its own `--fix*`
flags and forwards the rest to `nvidia_usd_validate`, which defines
`-r` / `--rule` / `--enable-rule`. The name must match a registered rule exactly:
argparse constrains it with `choices=`, so a typo is rejected rather than
silently ignored.
