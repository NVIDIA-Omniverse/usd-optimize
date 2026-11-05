<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# interpret-validators — Step 3 fallback summarizer

Use this only when `tools/validators/summarize_csv.py` is missing. Build a temporary
stdlib-only fallback summarizer beside the artifact (for example
`<artifact_dir>/_summarize_validator_csv.py`) and run that instead of reading
the CSV. The fallback script is a local run artifact, not repository content,
unless the user explicitly asks to add tooling. It must:

- Stream rows with Python's `csv.DictReader`; do not load the whole CSV into
  memory or print raw rows.
- Emit compact JSON matching the packaged summarizer (**required**
  top-level keys: `totals`, sorted `rules`, and capped grouped `failures`; see
  the shape in SKILL.md Step 3).
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

Optional **fallback-only** top-level extensions (omit if redundant): `report_path`
(normalized CSV path summarized), `report_bytes` (source size in bytes), and
`truncated` (boolean — whether capped output omitted rows). Add these when they
help explain a locally generated `<artifact_dir>/_summarize_validator_csv.py`; the
packaged `summarize_csv.py` does not emit them by default.

What the packaged summarizer does, which the fallback must match:

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
