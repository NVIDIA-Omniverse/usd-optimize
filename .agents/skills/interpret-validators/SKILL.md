---
name: interpret-validators
description: Read saved validator artifacts and triage the issues that --fix could not auto-resolve, with per-rule prim lists and fix recommendations. Use after run-validators to decide what to do about remaining issues.
allowed-tools: Bash, Read
metadata:
  author: NVIDIA Corporation
  version: "1.0.1"
  tags: [validation, reporting, analysis]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# interpret-validators — Read, present, and answer

> **Invocation.** This is the `interpret-validators` skill. In Claude Code it's
> also available as the alias `/interpret-validators`. In Codex or other agents,
> invoke it by name.
>
> **Python invocation.** Examples below use `python3` (POSIX). On Windows use
> `py -3` (Python launcher) or the bundled interpreter at
> `_build\target-deps\python\python.exe`. The helper scripts referenced
> below (`resolve_artifacts.py`, `summarize_csv.py`) work under any Python 3
> — no agent- or shell-specific extensions, no third-party packages.
>
> **Windows shell.** Snippets target PowerShell (Claude Code's and Codex's
> default Windows shell). For cmd.exe, replace `$Var` with `%VAR%` and
> backtick line continuations with `^`.

Companion to `run-validators`. Reads the CSV/JSON/summary artifacts the
validator driver produces, presents a structured report, and answers follow-up
questions without re-running.

**Auto-fix happens first.** `run-validators --fix` already applies every fix
the validators can apply automatically. This skill's job is to triage what is
**left** — the issues `IssueFixer` could not resolve. Those are the ones that
need a human decision (which prims, how aggressive, an accepted trade-off)
before an operation or preset config can address them. If a run produced no
`--fix` pass, treat every reported issue as un-fixed and triage the whole set.

The CSV is the source of truth — it contains issues from **both** rule
families:

- **Base** — `usd_validation_nvidia`'s `DefaultPlugin` rules (Kind, DefaultPrim,
  OmniOrphanedPrim, etc.). The driver's stdout summary hides these; the CSV does not.
- **Usd Optimize** — `UsdOptimize*Checker` rules from this repo. Each wraps an analysis-mode
  operation in `source/operations/`.

For the CSV schema and other infrastructure details, see `run-validators`.
Operation arguments and tuning guidance for any fix op live in
`docs/operations/<key>.rst`.

---

## What this skill covers

Each section below is load-bearing — read past Step 4 before concluding info is missing. Search for keywords like `family`, `base`, `Usd Optimize`, `unfixed`, `Tier`, `T1`, `T2`, `T3`, `headline takeaway`, `Rule reference` to jump.

- **Usage** — what arguments are accepted, what follow-up questions it answers.
- **Step 1** — resolve the input (asset / CSV / JSON).
- **Step 2** — fresh / stale / missing decision for replay vs re-run.
- **Step 3** — summarize the report via `summarize_csv.py`, which reads either the
  CSV or the JSON. Summarizer already lowercases severity,
  classifies family, normalizes locations, groups failures, sorts rules.
- **Step 4** — present the report (header + summary table with `family` column showing both base and Usd Optimize rules, failure details, headline takeaway).
- **Step 5** — follow-up questions, including "Show me only base rules / only Usd Optimize rules" and the "How do I fix `<RuleName>`?" answer flow.
- **Rule reference** — full Rule → backing op → tier table for **both** UsdOptimize rules **and** base usd-validation-nvidia rules. Base rules are not an afterthought — they get equivalent Usd Optimize-op mappings where one exists.
- **Error handling** — what to say when artifacts are missing/corrupt.

Companion skills:
- `run-validators` — produces the artifacts this skill reads; also covers the auto-fix model and validator infrastructure.
- `run-operations` — runs the fix ops this skill recommends in Step 5.
- `tune-parameters` — interactive parameter iteration when defaults don't fully resolve a T2 rule.

For ready-made multi-op stacks see `config_presets/` (run via the CLI per
`run-operations`); for which operation addresses which goal see
`docs/choosing-operations.rst`.

---

## Usage

The skill takes one positional argument — the path to either an asset or a
saved artifact:

| Argument | Behavior |
|---|---|
| `<path/to/asset.usd>` | Asset mode. Looks up saved artifacts for this asset. |
| `<path/to/issues.csv>` | Direct CSV mode. |
| `<path/to/results.json>` | JSON mode (`--json-output`). Pass it to `summarize_csv.py` exactly like a CSV — see below. |

### CSV or JSON — both work

The driver can emit either `--csv-output` (a CSV) or `--json-output` (a JSON
whose per-issue rows live under `rules[].issues[]`). Both carry the same issues
from **both** rule families, and `summarize_csv.py` reads either one — it picks
the reader by file extension and emits the identical `totals` / `rules` /
`failures` summary. So Step 3 and Step 4 are the same regardless of which the
user has; there is no separate derivation path.

Prefer the CSV when both exist, purely because it is the format the rest of this
skill's examples use.

Follow-up questions (no re-run needed):

- "Which prims are affected by `<RuleName>`?"
- "How do I fix `<RuleName>`?"
- "Show all `<RuleName>` failures" (when truncated in the initial report)
- "Show me only base rules" / "Show me only Usd Optimize rules"
- "Show me `<RuleName>` issues on `<prim_path>`"
- "Re-run validation"

---

## Step 1 — Resolve the input

Determine what kind of path the user gave:

- Ends in `.usd` / `.usda` / `.usdc` / `.usdz` → asset mode (Step 2).
- Ends in `.csv` → direct CSV mode (jump to Step 3 with the user's CSV).
- Ends in `.json` → JSON mode. Prefer a sibling `issues.csv` **in the same
  directory** if one exists, purely for consistency with the examples here;
  otherwise jump to Step 3 with the JSON itself. The summarizer handles both.

If no path is given, ask which asset / artifact to interpret.

## Step 2 — Run vs. replay decision (asset mode)

If the selected Usd Optimize environment provides the optional artifact resolver, use it:

```bash
# POSIX
python3 tools/validators/resolve_artifacts.py "<asset>"
```
```powershell
# Windows (PowerShell)
py -3 tools\validators\resolve_artifacts.py "<asset>"
```

When used, it returns the same JSON shape on all OSes. Parse the `state` field:

- **`fresh`** — Saved CSV is newer than the asset. Tell the user:
  > Found a saved validation run from `<csv_mtime>`. Replaying is much faster
  > than re-running. Should I replay, or re-run validation?

  Default to replay if the user doesn't specify — jump to Step 3 with the
  reported `csv` path.

- **`stale`** — Asset has been edited since the saved run. Tell the user:
  > The asset has been modified since the last validation run (saved
  > `<csv_mtime>`). I recommend re-running. Should I re-run, or replay the
  > older results?

  If they ask to re-run, invoke the `run-validators` skill. If they pick
  replay, proceed to Step 3 but flag the staleness in the report header.

- **`missing`** — No saved run. Tell the user we need to run first; offer
  the `run-validators` skill (don't run it inline without confirmation,
  since validation can take minutes on large assets).

## Step 3 — Summarize the report

Pass either the CSV or the JSON — `summarize_csv.py` picks the reader by
extension and emits the same summary from both.

**Don't read the report into context directly.** A real validator output is
thousands of rows and pulling it inline wastes tokens and is fragile across
quoting / encoding edge cases. Prefer the packaged summarizer when it is
present, then parse its compact JSON. **For the initial report, always pass
`--max-failures-per-rule 10`** so a pathological asset (e.g. one rule with
hundreds of unique-message failures) doesn't flood context. Re-run uncapped
for the "show all <Rule> failures" follow-up.

If `tools/validators/summarize_csv.py` is missing, build a temporary
stdlib-only fallback summarizer beside the artifact (for example
`<artifact_dir>/_summarize_validator_csv.py`) and run that instead of reading
the CSV. The fallback script is a local run artifact, not repository content,
unless the user explicitly asks to add tooling. It must:

- Stream rows with Python's `csv.DictReader`; do not load the whole CSV into
  memory or print raw rows.
- Emit compact JSON matching the packaged summarizer (**required**
  top-level keys: `totals`, sorted `rules`, and capped grouped `failures`; see
  shape below).
- Normalize severity, rule, message, suggestion, and location fields
  defensively because usd-validation-nvidia column names vary by version.
- Optionally add sibling metadata keys `report_path`, `report_bytes`, and
  `truncated` (boolean) — these are **not** emitted by repo
  `tools/validators/summarize_csv.py`, only useful for the ephemeral
  fallback when you want explicit provenance/size/truncation in the JSON blob.
- Cap examples to 10 locations per failure group for the initial report.

If the fallback cannot parse the CSV, report `blocked_large_artifact` with the
CSV path, byte size, detected columns, and at most the first 10 lines. Do not
paste the full artifact.

```bash
# POSIX — initial report
python3 tools/validators/summarize_csv.py "<csv_path>" --max-failures-per-rule 10
```
```powershell
# Windows (PowerShell) — initial report
py -3 tools\validators\summarize_csv.py "<csv_path>" --max-failures-per-rule 10
```

The output is a single JSON object. **Always** emit the three core sections below
(required for both the packaged script and any fallback summarizer):

```json
{
  "totals": {
    "rows": 3690,
    "rules": 25,
    "by_severity": {"failure": 147, "warning": 3543},
    "by_family":   {"Usd Optimize": 3251, "base": 439},
    "failures_by_rule": {"MissingReferenceChecker": 80, ...}
  },
  "rules": [
    {"rule": "<Name>", "family": "Usd Optimize|base",
     "by_severity": {"failure": N, "warning": N, "error": N, "info": N},
     "affected_prims": <distinct location count>},
    ...
  ],
  "failures": [
    {"rule": "<Name>", "message": "<text>",
     "suggestion": "<text or empty>",
     "locations": ["<bare path or '(stage)'>", ...]},
    ...
  ]
}
```

Optional **fallback-only** top-level extensions (omit if redundant): `report_path`
(normalized CSV path summarized), `report_bytes` (source size in bytes), and
`truncated` (boolean — whether capped output omitted rows). Add these when they
help explain a locally generated `<artifact_dir>/_summarize_validator_csv.py`; the
packaged `summarize_csv.py` does not emit them by default.

Notes on what the summarizer does for you:

- **Severity casing** — already lowercased (the CSV uses title case
  `Warning`/`Failure`, the JSON upper case `WARNING`/`FAILURE`; the summarizer
  collapses both to lowercase keys: `warning`, `failure`, `error`, `info`).
- **Family classification** — `family` is `"Usd Optimize"` for `UsdOptimize*` rules
  and `"base"` for everything else.
- **Location normalization** — strips `Prim </…>` / `Stage </…>` /
  `Attribute (…) Prim </…>` wrappers so `locations` contains bare paths.
  Empty / `None` cells become the literal string `"(stage)"`. Lines that don't
  fit the wrapper format (e.g. layer-spec `Sdf.Find('a', 'b')`) are kept as-is.
- **Failure grouping** — `failures` is a flat list grouped by
  `(rule, message, suggestion)`. Locations sharing the same message group
  collapse into one entry's `locations` array.
- **Sort order** — `rules` is sorted by max severity weight (failure > error
  > warning > info) then by issue count descending.

Always derive the real totals from the summarizer's `totals` section, not
from any companion JSON output file.

## Step 4 — Present the report

The report has three sections: header, summary table, and failure details.
Always print all three (when failures exist). Use full column names — never
abbreviate to single letters in user-facing output.

### Header

```
File: <asset (basename)>
Source: replayed from <report_path> (saved <mtime>)         # or "fresh run"

Summary: <N> failures, <N> warnings, <N> errors across <N> rules
         (base: <count>, UsdOptimize: <count>)
```

Cite whichever report you summarized — `issues.csv` or `results.json`.

### Summary table

Iterate the summarizer's `rules` array (already sorted). Omit rules with zero
total issues (the summarizer doesn't emit them, so this is automatic). Use
full column names:

```
| Rule | Family | Failures | Warnings | Errors | Affected prims | Fix tier | Operation |
|------|--------|----------|----------|--------|----------------|----------|-----------|
| ...  | Usd Optimize/base|    N     |    N     |   N    |       N        | T1/T2/T3 | <op> or — |
```

- **Affected prims** = `affected_prims` from the summarizer (distinct
  `Location` values; a single prim with multiple issues counts once).
- **Fix tier** and **Operation** come from the *Rule reference* below. For
  unknown rules, leave `Fix tier` blank and `Operation` `?` — don't guess.

### Failure details

Failures are the actionable signal — always expand them in the initial report.
Iterate the (already-capped) `failures` array from the summarizer. For each
rule with at least one failure, print:

```
**<RuleName>** — <N> failures

- <Message>
  Suggestion: <Suggestion>      # only if non-empty
    • `<location_or_(stage)>`
    • `<location_or_(stage)>`
    ...
```

The `--max-failures-per-rule 10` flag in Step 3 already caps each rule at 10
total failure rows, so no further truncation is needed in this step. To
detect when the cap dropped rows, sum the `locations` array lengths across
the rule's groups (each group can contain multiple locations sharing one
message) and compare to the unfiltered row count in
`totals.failures_by_rule[<rule>]`. **Don't compare group count vs row count**
— a single message affecting 3 prims is one group / 3 rows and would always
falsely trigger.

```python
shown_locs = sum(len(g["locations"]) for g in failures if g["rule"] == rule)
total_locs = totals["failures_by_rule"].get(rule, 0)
if shown_locs < total_locs:
    print(f'(+ {total_locs - shown_locs} more failures — '
          f'ask "show all {rule} failures" to see them)')
```

Do **not** expand warnings in the initial report; they're often in the
hundreds-to-thousands and would bury the failures. Warnings are surfaced via
the "Which prims are affected by …?" follow-up.

### Headline takeaway

After the failure details, add a 1–2 sentence synthesis identifying the
dominant pattern in the failures and the action that would resolve the most
issues. This converts the long table into a clear next step. Examples:

> The 147 failures are dominated by 138 missing-reference paths from a Windows
> export — fixable by re-flattening on a machine with the textures or rewriting
> absolute paths to relative ones.

> 86% of warnings come from `UsdOptimizeEmptyLeafChecker` and
> `UsdOptimizeUnusedUVsChecker` — `pruneLeaves` + `removeUnusedUVs` would
> clear most of them.

> All 198 issues are base usd-validation-nvidia rules; 0 Usd Optimize issues
> fired because the asset has no `UsdGeomMesh` prims (mesh-only rules find
> nothing on a references-only stage). The Usd Optimize hierarchy / materials /
> animation rules still ran and passed. The fix path is upstream (CAD export,
> references) rather than Usd Optimize.

### Footer

```
You can ask follow-up questions like:
  - "Which prims are affected by <RuleName>?"
  - "How do I fix <RuleName>?" — I'll print concrete commands then.
  - "Show all <RuleName> failures" — expands the truncated list.
  - "Show me only base rules" / "only Usd Optimize rules"
  - "Re-run validation"
```

Don't print fix commands eagerly. Wait for the user to ask.

## Step 5 — Follow-up questions

Use the parsed JSON in context. Don't re-run the validator unless asked.

The detailed playbook for each follow-up — exact summarizer invocations,
T1/T2/T3/base fix-question response templates, "show all", per-prim
filtering, family filters, "re-run", and "only check `<Rule>`" — lives
in **`references/follow-ups.md`**. Read that file when answering any
of these:

- "Which prims are affected by `<RuleName>`?" — `--locations` mode.
- "How do I fix `<RuleName>`?" — tier-aware response template.
- "Show all `<RuleName>` failures" — re-summarize uncapped.
- "Show me `<RuleName>` issues on `<prim_path>`" — `--locations` + substring filter.
- "Show me only base rules" / "only Usd Optimize rules" — family filter on Step 4.
- "Re-run validation" — hand off to the `run-validators` skill.
- "Only check `<RuleName>`" — pass `-r <RuleName>` to the driver (repeatable).

---

## Rule reference

The full Rule → backing op → tier table — for both UsdOptimize rules
and base usd-validation-nvidia rules — lives in **`references/rule-reference.md`**.
Read that file when populating the `Fix tier` and `Operation` columns of the
Step 4 summary table, and when answering "How do I fix `<RuleName>`?" follow-ups
in Step 5.

The reference covers:

- **UsdOptimize rules** — every `UsdOptimize*Checker` registered with its backing op and tier.
- **Base usd-validation-nvidia rules** — stage / metadata / external-reference
  rules with no Usd Optimize equivalent (T3 / manual), plus geometry rules that
  *do* map cleanly onto an Usd Optimize op (labelled `T1-equiv` / `T2-equiv`).

For rules not listed in the reference, treat as **T3 / manual** and
surface the CSV `Suggestion` column verbatim. Don't invent fix
commands.

---

## Error handling

| Symptom | Response |
|---|---|
| `summarize_csv.py` reports `not a usd-validation-nvidia report` | The JSON isn't a `--json-output` report (wrong file, or produced by an engine older than 1.21.0). Ask the user to re-run `run-validators` with `--csv-output` or `--json-output`. |
| User passes an asset with no saved run | "No saved validation found at `<artifact_dir>`. Run the run-validators skill on this asset first." |
| `summarize_csv.py` reports `report not found` | The artifact dir is empty or the path is wrong. Re-run the run-validators skill, or check `<artifact_dir>/` contents. |
| `summarize_csv.py` reports `CSV missing required columns` | The file is from a different tool. Show the first 10 lines and ask the user to confirm. |
| Summarizer succeeds but `totals.rows == 0` | "The validation completed with no issues — the asset passed every rule that ran." |
| User asks about a rule not in the summarizer output | "No issues were emitted for `<RuleName>` in this run." |
| User asks "how do I fix" a rule we don't recognise | Treat as base rule (T3 / manual); surface the `Suggestion` column from the CSV. |

## Purpose

Read the CSV / summary JSON artifacts produced by `run-validators` and
present a structured, tier-classified report — header, summary table
(per-rule severity counts + affected prims + fix tier + backing op),
failure details, and a headline takeaway — without re-running the
validator. Then answer follow-up questions ("which prims are affected
by X?", "how do I fix Y?", "show all <Rule> failures", base-only /
Usd Optimize-only filters) from the parsed JSON in context.

## Prerequisites

- A USD asset that has already been run through `run-validators`
  (or a CSV / summary JSON path the user supplies directly).
- A Python interpreter for the helper scripts (`resolve_artifacts.py`,
  `summarize_csv.py`) — pure stdlib, so any Python 3 works (no `pxr`
  required).
- The repo's `tools/validators/` directory accessible from the
  current working directory.

## Limitations

- This skill is **read-only**. It never re-runs the validator; if the
  user asks for a fresh run, hand off to `run-validators`.
- It never executes fix operations. Fix commands are *recommended* via
  the Rule reference table; the user invokes `run-operations` to apply
  them.
- Always derive totals from the summarizer's `totals` section rather than from
  counts printed elsewhere in the driver's output.
- The initial report caps each rule at 10 failure rows
  (`--max-failures-per-rule 10`) to keep context manageable. The
  "show all <Rule>" follow-up re-runs uncapped.
- Warnings are not expanded in the initial report (often hundreds-to-thousands).
  Surface them via the "Which prims are affected by …?" follow-up.

## Troubleshooting

The *Error handling* section above already covers the artifact-shape
failure modes. Additional meta-troubleshooting:

| Symptom | Likely cause | Fix |
|---|---|---|
| Summary numbers don't match JSON output totals | JSON output may be filtered or structured differently than the CSV. | Always derive totals from `summarize_csv.py` against the CSV. |
| "Show all <Rule>" output truncates again | Forgot to drop `--max-failures-per-rule` on the re-run. | Omit the flag; alternatively pass `--limit 0`. |
| Rule appears in CSV but not in the Step 4 table | Rule emitted only `info` / `warning` rows (no `failure`) and the user asked for failures only. | Re-render the table without severity filter; or use `--locations` to enumerate. |
| Fix tier shows `?` for a rule | Rule isn't in `references/rule-reference.md`. | Treat as T3 / manual and surface the CSV `Suggestion` column verbatim. Don't guess. |
| User asks "fix everything" | Some rules are T3 / analysis-only and have no automated fix path. | Filter the recommended chain to T1 + T2; explain that T3 rules need DCC/manual review. |
