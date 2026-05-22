<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shrinkwrap

**Key:** `shrinkwrap`
**Source:** `source/operations/shrinkwrap/Shrinkwrap.cpp`

## Overview

Shrinkwrap converts a polygon soup into a bounding watertight mesh, with controllable mechanisms to generate loose and tight surface proxies. It broadly operates via a three-step process:

1. **Scan the mesh onto a 3D grid** — like filling a mold with tiny cubes. **`voxelSize`** controls cube size: smaller = more detail, more memory. This is the resolution of the entire operation — if cubes are too big, nothing else matters because the grid can't see the detail.

2. **Close gaps** — inflates the surface outward to bridge holes, then deflates back. **`threshold`** controls the largest gap to fill (in world units). Keep it as small as possible — too high and it fills things you want open (e.g., a teapot's lid-body gap).

3. **Snap back to the original surface** — after closing, the surface may be puffy. **`erode`** controls how aggressively it shrinks back to hug the original. Default of 8 works well; higher recovers more detail, lower leaves the surface rounder. Increase or decrease values in powers of 2.

**`adaptivity`** (0–1) only affects triangulation density, not shape.

**Key insight:** get `voxelSize` right first. If the grid is too coarse, tweaking threshold and erode will have no visible effect.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths or SdfPathExpression expressions to process. Empty = all meshes. When using merge + shrinkwrap, set to target merged prims (e.g., `["//*merged*"]`) so shrinkwrap only runs on the merge output, not the original meshes. |
| `dim` | int | `512` | Grid dimension upper bound. Set to `0` to control purely via voxelSize. Hidden. |
| `voxelSize` | float | `0.1` | Voxel size in world units. Smaller = finer detail, more memory. |
| `erode` | float | `8.0` | How aggressively the surface snaps back to the original after gap closing. |
| `threshold` | float | `0.0` | Size of the largest gap to close, in world units. |
| `adaptivity` | float | `0.0` | Mesh simplification (0–1). 0 = uniform, 1 = most adaptive. |
| `extractLodPyramid` | bool | `false` | Extract all LOD levels. Hidden. |

**voxelSize vs dim:** when both are set, `effectiveVoxelSize = max(userVoxelSize, bboxMaxLength / (dim - 8))`. Set `dim=0` to control resolution purely via voxelSize.

All world-unit parameters (`voxelSize`, `threshold`) scale with scene units — a centimeter scene needs values ~100x larger than a meter scene.

## Tuning Order

1. **`voxelSize` first** — set fine enough to resolve the features you care about. If results look identical across parameter sweeps, this is too coarse.
2. **`threshold` second** — start at `0` and increase to close gaps. Keep as small as possible — excess threshold causes bloating and closes features you want to keep.
3. **`erode` third** — default 8.0 is usually fine. Increase if surface looks bloated; decrease to intentionally round concavities. Erode does *not* need to scale with threshold.
4. **`adaptivity` last** — only for reducing polygon count.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Gaps/holes still visible | `threshold` | Increase | Set to approximate world-space size of gaps |
| Thin features disappeared or rounded | `threshold` | Decrease | Threshold is closing features you want to keep |
| Surface looks bloated or puffy | `erode` | Increase | Surface hasn't snapped back enough |
| Concavities not rounded enough | `erode` | Decrease | Lower erode leaves surface smoother |
| Parameter changes have no effect | `voxelSize` | Decrease | Grid too coarse to resolve changes |
| Surface is blocky or faceted | `voxelSize` | Decrease | Voxels too large for detail |
| Too many polygons | `adaptivity` | Increase | Try 0.05–0.1 for moderate reduction |
| Slow or out of memory | `voxelSize` | Increase | Or decrease `dim` |

## Starting Configs

**Conservative first pass** (fast iteration, preserves shape):
```json
[{"operation": "shrinkwrap", "dim": 0, "voxelSize": 0.5, "erode": 8.0, "threshold": 0.0, "adaptivity": 0.0}]
```

**Gap closing** (close holes up to 2.0 world units):
```json
[{"operation": "shrinkwrap", "dim": 0, "voxelSize": 0.2, "erode": 12.0, "threshold": 2.0, "adaptivity": 0.0}]
```

**High fidelity** (fine detail, slow):
```json
[{"operation": "shrinkwrap", "dim": 0, "voxelSize": 0.05, "erode": 8.0, "threshold": 0.0, "adaptivity": 0.0}]
```

**LOD generation** (reduced polygon count):
```json
[{"operation": "shrinkwrap", "dim": 0, "voxelSize": 0.5, "erode": 8.0, "threshold": 0.0, "adaptivity": 0.1}]
```

**Multi-mesh LOD** (merge first, then shrinkwrap the merged result):
```json
[
  {"operation": "merge"},
  {"operation": "shrinkwrap", "dim": 0, "voxelSize": 0.5, "erode": 8.0, "threshold": 0.0, "adaptivity": 0.05, "paths": ["//*merged*"]}
]
```


## Real-World Session Logs

See `.agents/docs/sessions/shrinkwrap.md` for completed tuning sessions including a teapot spout hole closure and a hidden mesh removal case. Useful for anticipating common pitfalls and seeing how parameters interact in practice.

## Known Limitations

- **Instance proxies are skipped** — meshes under instance proxies are not processed.
- **Output is a sibling prim** — written as `_shrinkwrap` next to the original, not in-place.
- **World-space transform** — vertices are transformed to world space before voxelization; inverse transform is written on the output prim.