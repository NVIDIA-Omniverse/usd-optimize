---
name: inspect-asset
description: Quick USD stage inspection — reports stage info, prim counts, mesh statistics, materials, animation, and scene scale. Use before optimizing.
version: "1.0.0"
allowed-tools: Shell, Read
metadata:
  author: NVIDIA Corporation
  tags: [usd, inspection, analysis]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Inspect Asset

Quick, non-destructive inspection of a USD stage. Produces a structured
summary useful before running operations, validators, or tuning
parameters.

## What this skill covers

- **Usage** — arguments.
- **Step 1** — probe for `pxr`.
- **Step 2** — run the inspection script.
- **Step 3** — present the report.

Companion skills: `run-validators` (validate the asset),
`run-operations` (optimize the asset), `tune-parameters` (uses
inspection data to pick starting configs), `compare-stages` (compare
two stages).

---

## Usage

| Argument | Meaning |
|---|---|
| `<path/to/asset.usd>` | Required. `.usd` / `.usda` / `.usdc` / `.usdz`. |
| `--detailed` | Include per-mesh vertex counts and top-level prim tree. |

**Workflow:**

1. Pass the asset path as a positional argument.
2. Probe for the `pxr` Python bindings (Step 1).
3. Run the inspection script with the resolved Python (Step 2).
4. Use the captured JSON to render the structured report (Step 3).
5. Pass `--detailed` to also emit per-mesh and top-level-prim sections.

If no path is provided, ask:
> "Which USD file should I inspect? Please provide the full path."

---

## Step 1 — Probe for pxr

Check whether the Python USD bindings are available:

```bash
# POSIX
python3 -c "from pxr import Usd; print('ok')" 2>/dev/null
```
```powershell
# Windows
python -c "from pxr import Usd; print('ok')" 2>$null
```

If the probe fails, try the build's bundled Python:

```bash
# POSIX
_build/target-deps/python/python3 -c "from pxr import Usd; print('ok')" 2>/dev/null
```

If neither works, tell the user:

> USD inspection requires `pxr` (`pip install usd-core`). Install it for
> the fastest experience, or build the repo (`./repo.sh build`) to get
> the bundled Python.

---

## Step 2 — Run the inspection script

Write a temp script and run it with whichever Python has `pxr`. The
script outputs a single JSON object.

```python
import json, os, sys
from pxr import Usd, UsdGeom, UsdShade, UsdSkel

path = sys.argv[1]
detailed = "--detailed" in sys.argv

stage = Usd.Stage.Open(path)
if not stage:
    print(json.dumps({"error": f"Failed to open: {path}"}))
    sys.exit(1)

mpu = UsdGeom.GetStageMetersPerUnit(stage)
up = UsdGeom.GetStageUpAxis(stage)
default_prim = stage.GetDefaultPrim()

all_prims = list(stage.TraverseAll())
type_counts = {}
for p in all_prims:
    t = p.GetTypeName() or "(untyped)"
    type_counts[t] = type_counts.get(t, 0) + 1

meshes = [p for p in all_prims if p.IsA(UsdGeom.Mesh)]
materials = [p for p in all_prims if p.IsA(UsdShade.Material)]
instances = [p for p in all_prims if p.IsInstance()]
skel_roots = [p for p in all_prims if p.IsA(UsdSkel.Root)]

total_verts = 0
total_faces = 0
per_mesh = []

for p in meshes:
    mesh = UsdGeom.Mesh(p)
    pts = mesh.GetPointsAttr().Get()
    fvc = mesh.GetFaceVertexCountsAttr().Get()
    nv = len(pts) if pts else 0
    nf = len(fvc) if fvc else 0
    total_verts += nv
    total_faces += nf
    if detailed:
        per_mesh.append({"path": str(p.GetPath()), "vertices": nv, "faces": nf})

# Sample-only probe: stops at the first time-sampled attribute and caps the
# scan at 500 prims so large stages don't pay for a full sweep.
has_animation = False
for p in all_prims[:500]:
    for attr in p.GetAttributes():
        if attr.GetNumTimeSamples() > 1:
            has_animation = True
            break
    if has_animation:
        break

bcache = UsdGeom.BBoxCache(Usd.TimeCode.Default(),
                           [UsdGeom.Tokens.default_, UsdGeom.Tokens.render])
root_bb = bcache.ComputeWorldBound(stage.GetPseudoRoot()).ComputeAlignedBox()
if not root_bb.IsEmpty():
    bb_size = root_bb.GetMax() - root_bb.GetMin()
    bb_diag = bb_size.GetLength()
    bbox = {"min": list(root_bb.GetMin()), "max": list(root_bb.GetMax()),
            "size": list(bb_size), "diagonal": bb_diag}
else:
    bbox = None

top_prims = []
if detailed:
    for p in stage.GetPseudoRoot().GetChildren():
        top_prims.append({"path": str(p.GetPath()), "type": p.GetTypeName()})

try:
    file_size = os.path.getsize(path) if os.path.isfile(path) else 0
except (FileNotFoundError, OSError):
    file_size = 0

result = {
    "path": path,
    "file_size_bytes": file_size,
    "metersPerUnit": mpu,
    "upAxis": str(up),
    "defaultPrim": str(default_prim.GetPath()) if default_prim else None,
    "total_prims": len(all_prims),
    "type_counts": dict(sorted(type_counts.items(), key=lambda x: -x[1])),
    "meshes": len(meshes),
    "total_vertices": total_verts,
    "total_faces": total_faces,
    "materials": len(materials),
    "instances": len(instances),
    "skel_roots": len(skel_roots),
    "has_animation": has_animation,
    "bbox": bbox,
}
if detailed:
    result["top_level_prims"] = top_prims
    result["per_mesh"] = sorted(per_mesh, key=lambda x: -x["vertices"])[:50]

print(json.dumps(result, indent=2))
```

---

## Step 3 — Present the report

Parse the JSON and present a structured summary:

```text
Asset: <basename>
Path:  <full path>
Size:  <human-readable>

Stage metadata:
  metersPerUnit: <value> (<unit name>)
  upAxis:        <value>
  defaultPrim:   <path or "(none)")>

Geometry:
  Prims:    <total>
  Meshes:   <count>  (<vertices> vertices, <faces> faces)
  Materials: <count>
  Instances: <count>
  SkelRoots: <count>
  Animation: <yes/no>

Bounding box (stage units):
  Size: <X> × <Y> × <Z>
  Diagonal: <D>

Prim types:
  Mesh:     <count>
  Xform:    <count>
  Material: <count>
  Shader:   <count>
  ...
```

### Unit interpretation

Map `metersPerUnit` to a human-readable name:

| Value | Unit |
|---|---|
| 0.001 | millimeters |
| 0.01 | centimeters |
| 0.0254 | inches |
| 0.3048 | feet |
| 1.0 | meters |

### Flags and warnings

- **No default prim**: warn that some operations (e.g.
  `deduplicateHierarchies`) require a default prim.
- **No meshes**: note that mesh-only operations and validators will
  skip this stage (see `REQUIRES_MESH` in the `validators` skill).
- **Zero-area bounding box**: the stage may contain only non-renderable
  prims.
- **Animation detected**: note that time-sampled attributes are present;
  `optimizeTimeSamples` may be relevant.
- **High instance count**: the stage already uses instancing; warn before
  running `deduplicateGeometry` (which adds more instances) or `merge`
  (which can't merge instanced prims without deinstancing first).

### Detailed mode

If `--detailed` was requested, also show:

**Top-level prims:**

```text
  /World (Xform)
  /World/Asset (Xform)
  /World/Lights (Xform)
```

**Largest meshes (top 10 by vertex count):**

```text
  | Prim path                  | Vertices |  Faces |
  |----------------------------|----------|--------|
  | /World/Asset/Body/Mesh     |  245,000 | 163,000|
  | /World/Asset/Wheels/Mesh   |   82,000 |  54,000|
  | ...                        |          |        |
```

---

## See also

- `.agents/skills/run-validators/SKILL.md` — validate the asset.
- `.agents/skills/run-operations/SKILL.md` — optimize the asset.
- `.agents/skills/compare-stages/SKILL.md` — compare before/after.
- `.agents/skills/tune-parameters/SKILL.md` — Step 3 uses similar
  inspection data to pick starting configs.

## Purpose

Produce a quick, non-destructive structured summary of a USD stage —
metadata, prim/mesh/material counts, animation flag, bounding box,
scene scale — so the agent (and the user) have a baseline before any
optimization, validation, or tuning workflow. Useful as the very first
step when the user opens a new asset.

## Prerequisites

- A USD asset path (`.usd` / `.usda` / `.usdc` / `.usdz`).
- A Python interpreter with the `pxr` bindings — either standalone
  (`pip install usd-core`) or the build's bundled
  `_build/target-deps/python/`. The skill probes both.

## Limitations

- This skill is read-only — it never edits the input.
- Animation detection is a sample-only probe (caps at 500 prims and
  stops at the first time-sampled attribute) to keep large stages
  fast. The flag is a hint, not an exhaustive scan.
- Bounding box is computed for `default` + `render` purposes; assets
  whose visual content lives under `proxy` or `guide` purposes will
  report a degenerate bbox.
- This skill does **not** edit, optimize, or rewrite stages — for
  those, use `run-operations` or the operation-specific skills.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `pxr` probe fails for both interpreters | USD bindings not installed and the repo isn't built. | Install with `pip install usd-core`, or run `./repo.sh build` to populate `_build/target-deps/python/`. |
| `Failed to open: <path>` in the JSON output | Path is wrong, layer is corrupt, or it's a non-USD file with a `.usd` extension. | Verify the path; try `usdcat <path>` or open in `usdview` to confirm the file is a valid USD layer. |
| `total_prims = 0` | Asset is essentially empty, or the open call hit a payload that didn't load. | Check whether the stage uses payloads — `Stage.OpenMasked` or `Usd.Stage.Open(path, Usd.Stage.LoadAll)` may surface content. |
| `bbox = null` | All visible prims are non-renderable, or the stage uses non-`default`/`render` purpose. | Inspect with `--detailed` and check `purpose` on top-level prims. |
| `has_animation: false` on an obviously animated stage | Animated attributes live deeper than 500 prims. | Re-run with the cap raised (edit the script's `[:500]` slice). |

