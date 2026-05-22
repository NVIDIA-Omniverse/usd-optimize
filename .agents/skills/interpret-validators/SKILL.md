---
name: interpret-validators
description: Read saved validator artifacts and present a tier-classified report with per-rule prim lists and fix recommendations. Use when interpreting a saved run.
version: "1.0.0"
allowed-tools: Bash, Read
metadata:
  author: NVIDIA Corporation
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

Companion to `run-validators`. Reads the CSV/JSON artifacts that the validator
driver produces, presents a structured report, and answers follow-up questions
without re-running.

The CSV is the source of truth — it contains issues from **both** rule
families:

- **Base** — `omni.asset_validator`'s `DefaultPlugin` rules (Kind, DefaultPrim,
  OmniOrphanedPrim, etc.). The driver's stdout summary hides these; the CSV does not.
- **SO** — `SceneOptimizer*Checker` rules from this repo. Each wraps an analysis-mode
  operation in `source/operations/`.

For the CSV schema, the entry-point allow-list, and other infrastructure details,
see the `validators` skill.

---

## What this skill covers

Each section below is load-bearing — read past Step 4 before concluding info is missing. Search for keywords like `family`, `base`, `SO`, `REQUIRES_MESH`, `Tier`, `T1`, `T2`, `T3`, `headline takeaway`, `findings`, `Rule reference` to jump.

- **Usage** — what arguments are accepted, what follow-up questions it answers.
- **Step 1** — resolve the input (asset / CSV / summary JSON), including branching
  to partial-report derivation when summary mode lacks a sibling CSV but carries
  `findings`.
- **Step 2** — fresh / stale / missing decision for replay vs re-run.
- **Step 3** — summarize the CSV via `summarize_csv.py` when a CSV exists
  (**bypass:** project embedded `findings` analysis payloads when CSV is
  missing — § Partial-report mode). Summarizer already lowercases severity,
  classifies family, normalizes locations, groups failures, sorts rules.
- **Step 4** — present the report (header + summary table with `family` column showing both base and SO rules, failure details, headline takeaway).
- **Step 5** — follow-up questions, including "Show me only base rules / only SO rules" and the "How do I fix `<RuleName>`?" answer flow.
- **Rule reference** — full Rule → backing op → tier table for **both** SceneOptimizer rules **and** base asset-validator rules. Base rules are not an afterthought — they get equivalent SO-op mappings where one exists.
- **Error handling** — what to say when artifacts are missing/corrupt.

Companion skills:
- `run-validators` — produces the artifacts this skill reads.
- `validators` — reference doc for the underlying infrastructure.
- `run-operations` — runs the fix ops this skill recommends in Step 5.
- `tune-parameters` — interactive parameter iteration when defaults don't fully resolve a T2 rule.

For multi-op chains organized by bottleneck (memory, load time, mesh count, data quality), see `.agents/operations/PIPELINES.md`. For the canonical Python invocation reference, see `.agents/operations/INVOCATION.md`.

---

## Usage

The skill takes one positional argument — the path to either an asset or a
saved artifact:

| Argument | Behavior |
|---|---|
| `<path/to/asset.usd>` | Asset mode. Looks up saved artifacts for this asset. |
| `<path/to/issues.csv>` | Direct CSV mode. |
| `<path/to/summary.json>` | Summary mode. Prefer sibling `issues.csv`: when present, run Step 3 on the CSV. When the CSV sibling is missing (or unreadable), use **partial-report derivation** below if `findings` is populated. |

### Partial-report summary mode (missing `issues.csv`)

The CSV carries base + Scene Optimizer rows and is the authoritative source when
present. Sometimes only `summary.json` exists beside the artifact directory —
for example standalone analysis-mode output from `run-validators`, or a
driver-written summary augmented with serialized operation analysis payloads.

Treat all of these as one **analysis-derived partial report** path — one set of
rules for building Step 4. Do **not** fork separate presentation templates for
envelope quirks; only fingerprinting differs.

#### When partial-report derivation applies

- **Triggered by:** summary mode (`<path/to/summary.json>`) **and**
  sibling `issues.csv` absent or unreadable.
- **Prerequisite:** the JSON exposes a non-empty mapping at top-level `findings`
  with per-operation payloads shaped like `{…, "output": {"analysis": {…}}}`
  beneath each `findings[<operation_key>]`. If the trigger fires but there is no
  usable `findings`, Step 4 cannot reconstruct failure groups — explain that the
  artifact lacks row-level CSV **and** analysis payloads and ask the user to
  re-run `run-validators` (Kit path) rather than improvising totals from bare
  `total`/`by_rule` alone.

#### Fingerprint envelopes (shared downstream)

Use this only for logging/context; both feed the **same derivation** afterwards.

**Envelope A — Standalone analysis-mode (`run-validators` Python/API fallback):**

- Identified by `validator_path`, typically `"standalone-analysis-mode"`,
  alongside top-level `findings`. This artifact usually omits Kit timing fields.

**Envelope B — Kit/driver summary envelope with embedded `findings`:**

- Fingerprint Kit-style `--summary` output (for example JSON from the repo
  driver's `perf_validators.py`): top-level keys such as **`asset`**,
  **`validate_secs`**, **`open_secs`**, **`total`**, **`by_rule`** (SO-filtered
  counts — see §Step 3 note on `summary.json`), and **`by_severity`**.
- **Combined case:** envelope B keys **plus** a top-level **`findings`** object
  (same layout expected by `interpret-validators` as standalone: derive from
  `findings[<op>].output.analysis`).
- **`issues.csv` missing:** even though `total`/`by_rule` summarize SO rules,
  you still take this partial-report path so prim-level failure narratives match
  the analysis payloads instead of pretending CSV-backed detail exists.

#### Shared derivation (`findings` → Step 4)

For envelopes A and B, perform **identical** processing:

1. For each `<operation_key>` in `findings`, read **`findings[op].output.analysis`**
   and map emitted rule-like entries onto the summarizer-aligned columns using
   the corresponding `.agents/operations/<key>.md` **Analysis Mode** section as
   the schema reference (same field paths as standalone — do not diverge logic
   per envelope).

2. If two shapes appear in one file (`validator_path` **and** Kit timing keys),
   still unify on this single derivation over `findings`; use Kit fields only
   for the Step 4 **header's** timing lines (`validate_secs` / `open_secs`) when
   present.

3. Render **Step 4** — header + summary table + failure details — in the normal
   format (`family`, severity columns, expandable failure blocks). Omit rules with
   zero issues as usual once derived.

4. **`Fix tier` / `Operation`:** leave **`Fix tier` blank** and use `—` / `?` per
   Step 4 rules when **no Rule reference mapping** exists (same allowance as CSV
   path).

5. **Headline takeaway:** must include **exactly** this line verbatim (characters
   and hyphen length as shown):

   > `(standalone fallback — base omni.asset_validator rules not covered)`

   — for **either** envelope when CSV-derived detail is unavailable, because
   row-level/base rule coverage depends on CSV + full asset-validator emission
   and this branch does not recreate base plugin issues.

Adapt the Step 4 **Header** source line when CSV is absent (e.g. cite
`summary.json` / artifact dir replay instead of CSV path).

#### When sibling `issues.csv` exists

Prefer **Step 3** (`summarize_csv.py` / ephemeral **temporary stdlib-only fallback
summarizer** beside the CSV) and ordinary Step 4 — do not substitute
`findings`-only interpretation when the CSV is present.

Follow-up questions (no re-run needed):

- "Which prims are affected by `<RuleName>`?"
- "How do I fix `<RuleName>`?"
- "Show all `<RuleName>` failures" (when truncated in the initial report)
- "Show me only base rules" / "Show me only SO rules"
- "Show me `<RuleName>` issues on `<prim_path>`"
- "Re-run validation"

---

## Step 1 — Resolve the input

Determine what kind of path the user gave:

- Ends in `.usd` / `.usda` / `.usdc` / `.usdz` → asset mode (Step 2).
- Ends in `.csv` → direct CSV mode (jump to Step 3 with the user's CSV).
- Ends in `.json` → summary mode. Read the JSON; look for sibling
  `issues.csv` **in the same directory**. When the CSV exists, continue with
  Step 3 using that CSV. When the sibling CSV is **missing**, follow **§
  Partial-report summary mode (missing `issues.csv`)** in Usage above:
  fingerprint standalone vs Kit envelopes, require `findings`, then skip CSV
  summarization and synthesize Step 4 from `findings[op].output.analysis` instead
  of dumping raw summaries.

If no path is given, ask which asset / artifact to interpret.

## Step 2 — Run vs. replay decision (asset mode)

If the selected SO environment provides the optional artifact resolver, use it:

```bash
# POSIX
python3 tools/perf_validators/resolve_artifacts.py "<asset>"
```
```powershell
# Windows (PowerShell)
py -3 tools\perf_validators\resolve_artifacts.py "<asset>"
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

## Step 3 — Summarize the CSV

**Bypass:** When **§ Partial-report summary mode (missing `issues.csv`)** in
Usage applies — sibling CSV absent but `findings` contains operation analysis
bundles (`findings[<op>].output.analysis`) — skip CSV summarization here.
Instead project those analysis objects into the same compact `totals` /
`rules` / `failures` intermediate representation Step 4 expects (mirror the cap
spirit of `--max-failures-per-rule 10`; do not replay thousands of primitives),
then proceed directly to Step 4 using the unified partial-report playbook.

**Don't read the CSV into context directly.** A real validator output is
thousands of rows and pulling it inline wastes tokens and is fragile across
quoting / encoding edge cases. Prefer the packaged summarizer when it is
present, then parse its compact JSON. **For the initial report, always pass
`--max-failures-per-rule 10`** so a pathological asset (e.g. one rule with
hundreds of unique-message failures) doesn't flood context. Re-run uncapped
for the "show all <Rule> failures" follow-up.

If `tools/perf_validators/summarize_csv.py` is missing, build a temporary
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
  defensively because Asset Validator column names vary by version.
- Optionally add sibling metadata keys `report_path`, `report_bytes`, and
  `truncated` (boolean) — these are **not** emitted by repo
  `tools/perf_validators/summarize_csv.py`, only useful for the ephemeral
  fallback when you want explicit provenance/size/truncation in the JSON blob.
- Cap examples to 10 locations per failure group for the initial report.

If the fallback cannot parse the CSV, report `blocked_large_artifact` with the
CSV path, byte size, detected columns, and at most the first 10 lines. Do not
paste the full artifact.

```bash
# POSIX — initial report
python3 tools/perf_validators/summarize_csv.py "<csv_path>" --max-failures-per-rule 10
```
```powershell
# Windows (PowerShell) — initial report
py -3 tools\perf_validators\summarize_csv.py "<csv_path>" --max-failures-per-rule 10
```

The output is a single JSON object. **Always** emit the three core sections below
(required for both the packaged script and any fallback summarizer):

```json
{
  "totals": {
    "rows": 3690,
    "rules": 25,
    "by_severity": {"failure": 147, "warning": 3543},
    "by_family":   {"SO": 3251, "base": 439},
    "failures_by_rule": {"MissingReferenceChecker": 80, ...}
  },
  "rules": [
    {"rule": "<Name>", "family": "SO|base",
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

- **Severity casing** — already lowercased (CSV uses title case `Warning`/`Failure`,
  `summary.json` uses upper case `WARNING`/`FAILED_CHECK`; the summarizer
  collapses both to lowercase keys: `warning`, `failure`, `error`, `info`).
- **Family classification** — `family` is `"SO"` for `SceneOptimizer*` rules
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

If a `summary.json` is also available alongside the CSV, read its
`validate_secs` / `open_secs` for the report header. Note: `summary.json`'s
`total` / `by_rule` are filtered to SO rules only (`perf_validators.py` filters
before writing summary). Always derive the real totals from the summarizer's
`totals` section, not from `summary.json`.

## Step 4 — Present the report

The report has three sections: header, summary table, and failure details.
Always print all three (when failures exist). Use full column names — never
abbreviate to single letters in user-facing output.

### Header

```
File: <asset (basename)>
Source: replayed from <csv_path> (saved <csv_mtime>)        # or "fresh run"
Validate time: X.Xs (open Y.Ys)                             # if summary.json present

Summary: <N> failures, <N> warnings, <N> errors across <N> rules
         (base: <count>, SceneOptimizer: <count>)
```

**Partial-report / missing CSV:** omit the CSV replay line unless a CSV existed;
instead use `Source:` text that cites `summary.json` / artifact-directory replay
(or `asset.txt`), e.g. *analysis-derived from `<summary.json>` (sibling CSV
missing; `findings[op].output.analysis` payload)* — keep timing lines when the
summary envelope exposes `validate_secs` / `open_secs` (Envelope B).

### Summary table

Iterate the summarizer's `rules` array (already sorted). Omit rules with zero
total issues (the summarizer doesn't emit them, so this is automatic). Use
full column names:

```
| Rule | Family | Failures | Warnings | Errors | Affected prims | Fix tier | Operation |
|------|--------|----------|----------|--------|----------------|----------|-----------|
| ...  | SO/base|    N     |    N     |   N    |       N        | T1/T2/T3 | <op> or — |
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

> 86% of warnings come from `SceneOptimizerEmptyLeafChecker` and
> `SceneOptimizerUnusedUVsChecker` — `pruneLeaves` + `removeUnusedUVs` would
> clear most of them.

> All 198 issues are base asset-validator rules; 0 Scene Optimizer issues
> fired because the asset has no `UsdGeomMesh` prims (mesh-only SO rules
> short-circuit via `REQUIRES_MESH` — see `validators/SKILL.md`
> §Performance behavior). Six SO hierarchy / materials / animation rules
> still ran and passed. The fix path is upstream (CAD export, references)
> rather than Scene Optimizer.

### Footer

```
You can ask follow-up questions like:
  - "Which prims are affected by <RuleName>?"
  - "How do I fix <RuleName>?" — I'll print concrete commands then.
  - "Show all <RuleName> failures" — expands the truncated list.
  - "Show me only base rules" / "only SO rules"
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
- "Show me only base rules" / "only SO rules" — family filter on Step 4.
- "Re-run validation" — hand off to the `run-validators` skill.
- "Only check `<RuleName>`" — explain there's no `--rule` flag; filter post-hoc.

---

## Rule reference

The full Rule → backing op → tier table — for both SceneOptimizer rules
and base asset-validator rules — lives in **`references/rule-reference.md`**.
Read that file when populating the `Fix tier` and `Operation` columns of the
Step 4 summary table, and when answering "How do I fix `<RuleName>`?" follow-ups
in Step 5.

The reference covers:

- **SceneOptimizer rules (default)** — every `SceneOptimizer*Checker`
  registered with its backing op and tier.
- **SceneOptimizer rules (expensive)** — slower Scene Optimizer checks
  listed separately from the default rule table.
- **Base asset-validator rules** — stage / metadata / external-reference
  rules with no SO equivalent (T3 / manual), plus geometry rules that
  *do* map cleanly onto an SO op (labelled `T1-equiv` / `T2-equiv`).

For rules not listed in the reference, treat as **T3 / manual** and
surface the CSV `Suggestion` column verbatim. Don't invent fix
commands.

---

## Error handling

| Symptom | Response |
|---|---|
| Summary JSON without sibling CSV **and** no usable top-level `findings` payloads | Artifact has rollups only (`total` / `by_rule`) — cannot rebuild Step 4 failure groups or prim lists. Ask the user to re-run `run-validators` (emit `issues.csv` or a summary that embeds `findings[<op>].output.analysis`). |
| User passes an asset with no saved run | "No saved validation found at `<artifact_dir>`. Run the run-validators skill on this asset first." |
| `summarize_csv.py` reports `csv not found` | The artifact dir is empty or the path is wrong. Re-run the run-validators skill, or check `<artifact_dir>/` contents. |
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
SO-only filters) from the parsed JSON in context.

## Prerequisites

- A USD asset that has already been run through `run-validators`
  (or a CSV / summary JSON path the user supplies directly).
- A Python interpreter for the helper scripts (`resolve_artifacts.py`,
  `summarize_csv.py`) — pure stdlib, so any Python 3 works (no `pxr`
  required).
- The repo's `tools/perf_validators/` directory accessible from the
  current working directory.

## Limitations

- This skill is **read-only**. It never re-runs the validator; if the
  user asks for a fresh run, hand off to `run-validators`.
- It never executes fix operations. Fix commands are *recommended* via
  the Rule reference table; the user invokes `run-operations` to apply
  them.
- The CSV is the source of truth — `summary.json`'s `total` /
  `by_rule` are filtered to SO rules only. The skill always derives
  totals from the summarizer's `totals` section.
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
| Summary numbers don't match `summary.json` totals | `summary.json` is SO-filtered; the CSV is unfiltered. | Always derive totals from `summarize_csv.py` against the CSV — never read `summary.json.total` directly. |
| "Show all <Rule>" output truncates again | Forgot to drop `--max-failures-per-rule` on the re-run. | Omit the flag; alternatively pass `--limit 0`. |
| Rule appears in CSV but not in the Step 4 table | Rule emitted only `info` / `warning` rows (no `failure`) and the user asked for failures only. | Re-render the table without severity filter; or use `--locations` to enumerate. |
| Fix tier shows `?` for a rule | Rule isn't in `references/rule-reference.md`. | Treat as T3 / manual and surface the CSV `Suggestion` column verbatim. Don't guess. |
| User asks "fix everything" | Some rules are T3 / analysis-only and have no automated fix path. | Filter the recommended chain to T1 + T2; explain that T3 rules need DCC/manual review. |
