---
name: compare-stages
description: Diff two USD stages by prim/mesh/vertex/material count, file size, and validator summary. Use for before/after optimization comparisons.
version: "1.0.0"
allowed-tools: Shell, Read
metadata:
  author: NVIDIA Corporation
  tags: [usd, diff, comparison, validation]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compare Stages

Produce a structured diff between two USD stages — typically the original
asset and the optimized output. Reports what changed at multiple levels:
file size, prim/mesh/vertex/material counts, and (optionally) validator
findings.

## What this skill covers

- **Usage** — arguments and modes.
- **Step 1** — collect metrics from both stages.
- **Step 2** — present the comparison table.
- **Step 3** — (optional) diff validator summaries.
- **Step 4** — (optional) prim-level diff.

Companion skills: `run-operations` (produces the optimized stage),
`run-validators` / `interpret-validators` (validates before and after),
`inspect-asset` (single-stage inspection).

---

## Usage

| Argument | Meaning |
|---|---|
| `<before.usd>` | Path to the original stage. |
| `<after.usd>` | Path to the optimized / modified stage. |
| `--validators` | Also diff validator findings (requires saved artifacts for both). |
| `--prims` | Show prim-level adds/removes (can be verbose on large stages). |

**Workflow:**

1. Pass `<before.usd>` and `<after.usd>` as positional arguments.
2. Run the metric-collection script (Step 1) under a Python that can import `pxr`.
3. Use the captured JSON to render the comparison table (Step 2).
4. Optionally pass `--validators` to call `tools/perf_validators/run.sh compare` (Step 3).
5. Optionally pass `--prims` to execute the prim-diff helper (Step 4).

If either path is missing, ask:
> "Please provide the paths to the two USD files you'd like to compare."

---

## Step 1 — Collect metrics from both stages

Write a temp script and run it under the build's bundled Python (or
standalone `pxr` if available). The script collects the same metrics for
both stages in one pass.

```python
import json, os, sys
from pxr import Usd, UsdGeom, UsdShade

def safe_getsize(path):
    try:
        return os.path.getsize(path) if os.path.isfile(path) else 0
    except (FileNotFoundError, OSError):
        return 0

def collect(path):
    stage = Usd.Stage.Open(path)
    if not stage:
        return {"error": f"Failed to open: {path}"}

    mpu = UsdGeom.GetStageMetersPerUnit(stage)
    up = UsdGeom.GetStageUpAxis(stage)

    prims = list(stage.TraverseAll())
    meshes = [p for p in prims if p.IsA(UsdGeom.Mesh)]
    materials = [p for p in prims if p.IsA(UsdShade.Material)]
    instances = [p for p in prims if p.IsInstance()]

    total_verts = 0
    total_faces = 0
    for m in meshes:
        mesh = UsdGeom.Mesh(m)
        pts = mesh.GetPointsAttr().Get()
        fvc = mesh.GetFaceVertexCountsAttr().Get()
        if pts:
            total_verts += len(pts)
        if fvc:
            total_faces += len(fvc)

    file_size = safe_getsize(path)

    return {
        "path": path,
        "file_size_bytes": file_size,
        "metersPerUnit": mpu,
        "upAxis": str(up),
        "total_prims": len(prims),
        "meshes": len(meshes),
        "materials": len(materials),
        "instances": len(instances),
        "total_vertices": total_verts,
        "total_faces": total_faces,
    }

before = collect(sys.argv[1])
after = collect(sys.argv[2])
print(json.dumps({"before": before, "after": after}, indent=2))
```

Save the script somewhere writable (the OS temp dir works for ad-hoc runs; use a project scratch dir if you need to keep the artifact). Run it under whichever Python has `pxr`:

```bash
# POSIX
tmpdir="${TMPDIR:-/tmp}"
python3 "$tmpdir/_compare_stages.py" "<before.usd>" "<after.usd>"
# or, if pxr is only available through the build's bundled Python:
_build/target-deps/python/python3 "$tmpdir/_compare_stages.py" "<before.usd>" "<after.usd>"
```
```powershell
# Windows (PowerShell)
py -3 "$env:TEMP\_compare_stages.py" "<before.usd>" "<after.usd>"
# or, if pxr is only available through the build's bundled Python:
& _build\target-deps\python\python.exe "$env:TEMP\_compare_stages.py" "<before.usd>" "<after.usd>"
```

## Step 2 — Present the comparison table

Parse the JSON output and present a table with deltas:

```text
Comparison: <before_basename> → <after_basename>

| Metric          |    Before |     After |   Delta |     % |
|-----------------|-----------|-----------|---------|-------|
| File size       |   12.3 MB |    8.1 MB |  -4.2 MB | -34% |
| Prims           |    12,450 |     8,230 |  -4,220 | -34% |
| Meshes          |     3,200 |     1,100 |  -2,100 | -66% |
| Vertices        | 1,245,000 |   620,000 | -625,000 | -50% |
| Faces           |   830,000 |   415,000 | -415,000 | -50% |
| Materials       |        45 |        12 |     -33 | -73% |
| Instances       |         0 |       800 |    +800 |    — |
| metersPerUnit   |      0.01 |      0.01 |       — |    — |
| upAxis          |         Y |         Y |       — |    — |
```

Format file sizes in human-readable units (KB/MB/GB). Use commas for
large numbers. Flag any change in `metersPerUnit` or `upAxis` with a
warning — those shouldn't change during optimization.

### Headline summary

After the table, add a 1–2 sentence synthesis:

> Optimization reduced vertex count by 50% and mesh count by 66% (mostly
> from `deduplicateGeometry` creating 800 instances). File size dropped
> 34%. Stage metadata unchanged.

If you know which operations were run (e.g. from a prior `run-operations`
session), attribute the changes to specific ops.

## Step 3 — (Optional) Diff validator summaries

When the user passes `--validators` or asks "did the validator issues
improve?", use the `tools/perf_validators/run.sh compare` command if
both stages have saved summary JSON artifacts:

```bash
tools/perf_validators/run.sh compare \
    "<before_artifact_dir>/summary.json" \
    "<after_artifact_dir>/summary.json"
```

This prints per-rule count deltas and total delta. Present the output as a
table. If saved artifacts don't exist for one or both stages, tell the
user to run `/run-validators` on each stage first.

Alternatively, if both stages have saved CSVs, use the summarizer on each
and diff the `totals` sections:

```bash
python3 tools/perf_validators/summarize_csv.py "<before_csv>"
python3 tools/perf_validators/summarize_csv.py "<after_csv>"
```

Present the per-rule delta:

```text
| Rule                              | Before | After | Delta |
|-----------------------------------|--------|-------|-------|
| SceneOptimizerDuplicateGeometry   |    120 |     0 |  -120 |
| SceneOptimizerColocatedVertices   |     45 |     0 |   -45 |
| ...                               |        |       |       |
```

Rules that dropped to 0 are resolved. Rules that didn't change may need
different operations or parameter tuning.

## Step 4 — (Optional) Prim-level diff

When the user passes `--prims` or asks "which prims were added/removed?",
add this helper to the Step 1 script. It reopens the two input paths instead
of trying to reuse `collect()`'s local `stage` variable:

```python
def collect_prim_paths(path):
    stage = Usd.Stage.Open(path)
    if not stage:
        return set()
    return {str(p.GetPath()) for p in stage.TraverseAll()}

before_paths = collect_prim_paths(sys.argv[1])
after_paths = collect_prim_paths(sys.argv[2])

added = sorted(after_paths - before_paths)
removed = sorted(before_paths - after_paths)
```

For large stages this can produce thousands of lines. Default to showing
the first 20 adds and 20 removes, with a count of how many more exist.
Expand on request.

Prims that exist in both stages but changed (e.g. vertex count dropped)
require per-prim attribute comparison — only do this if the user asks
about a specific prim path.

---

## See also

- `.agents/skills/run-operations/SKILL.md` — producing the optimized stage.
- `.agents/skills/run-validators/SKILL.md` — generating validator artifacts.
- `.agents/skills/inspect-asset/SKILL.md` — single-stage inspection.
- `.agents/operations/PIPELINES.md` — what each pipeline is expected to change.

## Purpose

Quantify what an optimization (or any stage edit) changed. Produces a
structured before/after report at three levels — file size, geometry
counts (prims, meshes, vertices, faces, materials, instances), and
optionally validator-finding deltas. Use when the user asks "what
changed?", wants to confirm a pipeline did the expected work, or needs
to attribute a regression to a specific operation.

## Prerequisites

- Two USD files (`.usd` / `.usda` / `.usdc` / `.usdz`) on disk.
- A Python interpreter with the USD bindings (`from pxr import Usd`),
  via either `pip install usd-core` or the build's bundled
  `_build/target-deps/python/`.
- For `--validators`: saved `summary.json` artifacts from
  `run-validators` for both stages, **or** their CSVs.

## Limitations

- The skill is read-only — it never modifies either input.
- Per-prim attribute diffing (e.g. "did vertex positions change?") is
  out of scope; ask the user to point at a specific prim if they want
  attribute-level comparison.
- Validator diff requires both stages to have been run through the
  same validator set (mismatched rule sets produce noisy deltas).
- Prim-level diff (`--prims`) can be slow and verbose on multi-million-prim
  stages — defaults to first 20 adds/removes.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Failed to open: <path>` in the JSON output | The path doesn't exist or isn't a valid USD layer. | Verify the path; check the file extension is `.usd*`. |
| Validator diff says "no saved artifacts" | One or both stages haven't been validated yet. | Run `run-validators` on each stage first to produce `summary.json`. |
| Counts identical despite an edit | Edit was authored on a sublayer the open call doesn't see. | Open the asset with the same layer-stack / session that produced the edit. |
| `metersPerUnit` or `upAxis` flagged as changed | An operation rewrote stage metadata it shouldn't have. | Check the pipeline — most SO ops never touch stage metadata. Surface as a warning. |
| Prim-diff returns thousands of entries | Edit was a wholesale tree rebuild (e.g. flatten). | Switch to summary-level reporting; ask the user if they want a specific subtree compared. |

