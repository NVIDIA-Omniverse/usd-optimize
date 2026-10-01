---
name: compare-stages
description: Diff two USD stages by prim/mesh/vertex/material count, file size, and validator summary. Use for before/after optimization comparisons.
allowed-tools: Shell, Read
metadata:
  author: NVIDIA Corporation
  version: "1.0.0"
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
4. Optionally pass `--validators` to diff validator issue counts between the two stages (Step 3).
5. Optionally pass `--prims` to execute the prim-diff helper (Step 4).

If either path is missing, ask:
> "Please provide the paths to the two USD files you'd like to compare."

---

## Step 1 — Collect metrics from both stages

Write a temp script and run it under the interpreter resolved below. The
script collects the same metrics for both stages in one pass.

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

    # Expanded counts include instance proxies, so instancing never reads as deletion;
    # distinct counts take each prototype prim once, however many instances share it.
    prims = list(Usd.PrimRange.Stage(stage, Usd.TraverseInstanceProxies(Usd.PrimAllPrimsPredicate)))
    distinct = list(dict.fromkeys(p.GetPrimInPrototype() if p.IsInstanceProxy() else p for p in prims))
    meshes = [p for p in prims if p.IsA(UsdGeom.Mesh)]
    materials = [p for p in prims if p.IsA(UsdShade.Material)]
    instances = [p for p in prims if p.IsInstance()]

    total_verts = 0
    total_faces = 0
    total_triangles = 0
    for m in meshes:
        mesh = UsdGeom.Mesh(m)
        pts = mesh.GetPointsAttr().Get()
        fvc = mesh.GetFaceVertexCountsAttr().Get()
        if pts:
            total_verts += len(pts)
        if fvc:
            total_faces += len(fvc)
            # A quad is 1 face but 2 triangles. Clamped at 0 so a malformed face
            # (fewer than 3 vertices) on an unvalidated asset can't subtract.
            total_triangles += sum(max(n - 2, 0) for n in fvc)

    file_size = safe_getsize(path)

    return {
        "path": path,
        "file_size_bytes": file_size,
        "metersPerUnit": mpu,
        "upAxis": str(up),
        "total_prims": len(prims),
        "distinct_prims": len(distinct),
        "meshes": len(meshes),
        "distinct_meshes": sum(1 for p in distinct if p.IsA(UsdGeom.Mesh)),
        "materials": len(materials),
        "instances": len(instances),
        "total_vertices": total_verts,
        "total_faces": total_faces,
        "total_triangles": total_triangles,
    }

before = collect(sys.argv[1])
after = collect(sys.argv[2])
print(json.dumps({"before": before, "after": after}, indent=2))
```

Save the script somewhere writable (the OS temp dir works for ad-hoc runs; use a project scratch dir if you need to keep the artifact). Resolve `pxr` deterministically — prefer a built repo's bindings (export the build env, mirroring `tools/validators/run.sh:20-21` / `run.bat:25-26`), else use whatever `pxr` the interpreter already has (wheel/Kit/conda). Don't bare-probe first — a stray `PYTHONPATH` can shadow the build and fail with a confusing ABI error:

```bash
# POSIX
CONFIG="${USD_OPTIMIZE_CONFIG:-release}"; PLATFORM="${USD_OPTIMIZE_PLATFORM:-linux-x86_64}"
BUILD_DIR="_build/$PLATFORM/$CONFIG"; USD_DIR="_build/target-deps/usd/$CONFIG"
if [ -d "$BUILD_DIR" ]; then
    export LD_LIBRARY_PATH="$BUILD_DIR/lib:$BUILD_DIR/extraLibs:$USD_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
    export PYTHONPATH="$BUILD_DIR/python:$USD_DIR/lib/python${PYTHONPATH:+:$PYTHONPATH}"
    PYBIN="_build/target-deps/python/bin/python3.12"
else PYBIN="python3"; fi
tmpdir="${TMPDIR:-/tmp}"
"$PYBIN" "$tmpdir/_compare_stages.py" "<before.usd>" "<after.usd>"
```
```powershell
# Windows (PowerShell)
$Config = if ($env:USD_OPTIMIZE_CONFIG) { $env:USD_OPTIMIZE_CONFIG } else { "release" }
$Platform = if ($env:USD_OPTIMIZE_PLATFORM) { $env:USD_OPTIMIZE_PLATFORM } else { "windows-x86_64" }
$BuildDir = "_build\$Platform\$Config"; $UsdDir = "_build\target-deps\usd\$Config"
if (Test-Path $BuildDir) {
    $env:PATH = "$BuildDir\bin;$BuildDir\lib;$BuildDir\extraLibs;$UsdDir\bin;$UsdDir\lib;$env:PATH"
    $env:PYTHONPATH = "$BuildDir\python;$UsdDir\lib\python;$env:PYTHONPATH"
    $PyBin = "_build\target-deps\python\python.exe"
} else { $PyBin = "python" }
& $PyBin "$env:TEMP\_compare_stages.py" "<before.usd>" "<after.usd>"
```

For a build-vs-build comparison, use the build's own USD (path 1) so both stages read through the same USD the operations wrote. If you fall back to an installed `pxr`, pin it to the build: `pip install usd-core==26.08` (match the USD version pinned in `deps/usd_flavors.json` / `deps/usd-lib-deps.json`; a bare `pip install usd-core` may not match).

## Step 2 — Present the comparison table

Parse the JSON output and present a table with deltas:

```text
Comparison: <before_basename> → <after_basename>

| Metric          |    Before |     After |   Delta |     % |
|-----------------|-----------|-----------|---------|-------|
| File size       |   12.3 MB |    8.1 MB |  -4.2 MB | -34% |
| Prims           |    12,450 |     8,230 |  -4,220 | -34% |
| Distinct prims  |    12,450 |     7,530 |  -4,920 | -40% |
| Meshes          |     3,200 |     3,200 |       0 |   0% |
| Distinct meshes |     3,200 |     2,500 |    -700 | -22% |
| Vertices        | 1,245,000 |   620,000 | -625,000 | -50% |
| Faces           |   830,000 |   415,000 | -415,000 | -50% |
| Triangles       | 1,200,000 |   415,000 | -785,000 | -65% |
| Materials       |        45 |        12 |     -33 | -73% |
| Instances       |         0 |       800 |    +800 |    — |
| metersPerUnit   |      0.01 |      0.01 |       — |    — |
| upAxis          |         Y |         Y |       — |    — |
```

Counts include every instance's copy, so instancing never lowers `meshes`
or the geometry totals; it shows up in `instances` and the distinct counts.
A quad is one face but two triangles, so compare `total_triangles`
across a triangulating op such as decimation.

Format file sizes in human-readable units (KB/MB/GB). Use commas for
large numbers. Flag any change in `metersPerUnit` or `upAxis` with a
warning — those shouldn't change during optimization.

### Headline summary

After the table, add a 1–2 sentence synthesis:

> Decimation cut triangles by 65% and vertices by 50%; `deduplicateGeometry`
> shared meshes across 800 instances (distinct meshes -22%, none deleted).
> File size dropped 34%. Stage metadata unchanged.

If you know which operations were run (e.g. from a prior `run-operations`
session), attribute the changes to specific ops.

## Step 3 — (Optional) Diff validator summaries

When the user passes `--validators` or asks "did the validator issues
improve?", use the summarizer on each stage's saved CSV and diff the
`totals` sections. If saved CSVs don't exist for one or both stages, tell
the user to run `/run-validators` on each stage first. Can check with `--verbose`
flag for running validation for more prim paths.

```bash
python3 tools/validators/summarize_csv.py "<before_csv>"
python3 tools/validators/summarize_csv.py "<after_csv>"
```

Present the per-rule delta:

```text
| Rule                              | Before | After | Delta |
|-----------------------------------|--------|-------|-------|
| UsdOptimizeDuplicateGeometry   |    120 |     0 |  -120 |
| UsdOptimizeColocatedVertices   |     45 |     0 |   -45 |
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
    return {str(p.GetPath())
            for p in Usd.PrimRange.Stage(stage, Usd.TraverseInstanceProxies(Usd.PrimAllPrimsPredicate))}

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
- `.agents/skills/config-presets/SKILL.md` — what each preset config is expected to change.

## Purpose

Quantify what an optimization (or any stage edit) changed. Produces a
structured before/after report at three levels — file size, geometry
counts (prims, meshes, vertices, faces, materials, instances), and
optionally validator-finding deltas. Use when the user asks "what
changed?", wants to confirm a pipeline did the expected work, or needs
to attribute a regression to a specific operation.

## Prerequisites

- Two USD files (`.usd` / `.usda` / `.usdc` / `.usdz`) on disk.
- A Python interpreter with the USD bindings (`from pxr import Usd`) —
  a built repo (`_build/target-deps/`, env exported per Step 1), an
  existing pxr (wheel/Kit/conda), or a pinned `pip install usd-core==26.08`.
- For `--validators`: saved `issues.csv` artifacts from
  `run-validators` for both stages.

## Limitations

- The skill is read-only — it never modifies either input.
- Per-prim attribute diffing (e.g. "did vertex positions change?") is
  out of scope; ask the user to point at a specific prim if they want
  attribute-level comparison.
- Validator diff requires both stages to have been run through the
  same validator set (mismatched rule sets produce noisy deltas).
- Prim-level diff (`--prims`) can be slow and verbose on multi-million-prim
  stages — defaults to first 20 adds/removes.
- Counts expand instance proxies, so a heavily instanced stage
  takes far longer to scan than its `distinct_prims` count suggests.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Failed to open: <path>` in the JSON output | The path doesn't exist or isn't a valid USD layer. | Verify the path; check the file extension is `.usd*`. |
| Validator diff says "no saved artifacts" | One or both stages haven't been validated yet. | Run `run-validators` on each stage first to produce `issues.csv`. |
| Counts identical despite an edit | Edit was authored on a sublayer the open call doesn't see. | Open the asset with the same layer-stack / session that produced the edit. |
| `metersPerUnit` or `upAxis` flagged as changed | An operation rewrote stage metadata it shouldn't have. | Check the pipeline — most Usd Optimize ops never touch stage metadata. Surface as a warning. |
| Prim-diff returns thousands of entries | Edit was a wholesale tree rebuild (e.g. flatten). | Switch to summary-level reporting; ask the user if they want a specific subtree compared. |

