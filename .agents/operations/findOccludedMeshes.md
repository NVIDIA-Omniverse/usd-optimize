<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Find Occluded Meshes

**Key:** `findOccludedMeshes`
**Source:** `source/operations/findOccludedMeshes/FindOccludedMeshes.cpp`

## Overview

Find Occluded Meshes detects geometry that is completely hidden inside other geometry and therefore never visible. Removing these occluded meshes can significantly improve rendering performance without any visual change.

The operation performs a **voxel flood-fill** algorithm: the scene is rasterized onto a 3D grid, then air cells are identified by flood-filling from the grid boundary inward. Meshes that have no flood-reachable air cells adjacent to their faces are classified as occluded. Both CPU and GPU implementations are available.

**`maximumGridResolution`** controls the maximum number of cells along the longest axis of the grid. **`minimumGapSize`** corresponds to the grid spacing — gaps smaller than this voxel spacing are treated as sealed, preventing the flood-fill from reaching interior cavities through small cracks. A value of 500 is suitable for powerful GPUs; use smaller values for less powerful hardware or CPU mode.

**`clustered`** mode first splits the stage into clusters of meshes with overlapping bounding boxes, then checks visibility per cluster. This enables finer-grained grid resolution per cluster because each cluster gets its own grid rather than one global grid for the entire stage.

Visibility is inherently fuzzy — real-world scenes like cars and engines have small gaps between parts. With coarse voxels a jet engine's interior is mostly invisible, but with fine voxels the camera can "see through" tiny gaps, marking more interior geometry as visible.

**Key insight:** start with a moderate grid resolution and increase only if you suspect occluded meshes are being missed. The check is conservative — it would rather keep a mesh than incorrectly remove one.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `clustered` | bool | `true` | Group nearby meshes for collective occlusion testing. |
| `minimumGapSize` | float | `0.01` | Minimum opening size to consider. Gaps smaller than this are treated as sealed. In world units. |
| `maximumGridResolution` | float | `500.0` | Maximum voxel grid resolution per axis. Higher = more accurate, slower. |
| `checkTransparency` | bool | `false` | Consider material transparency when testing occlusion. |
| `action` | enum | `Hide` (2) | What to do with occluded meshes: `Delete` (0), `Deactivate` (1), or `Hide` (2). |
| `useGpu` | bool | `true` | Use GPU for occlusion testing. Hidden in UI. Falls through to the CPU path silently when CUDA is unavailable (no log line — see `FindOccludedMeshes.cpp:211`). |

## Tuning Order

1. **`maximumGridResolution` first** — Start at 500. Increase for scenes with fine occluders; decrease for faster iteration.
2. **`minimumGapSize` second** — Increase to treat small gaps as sealed (catches more occlusions). Keep at 0 for strict accuracy.
3. **`clustered` third** — Enable to detect collectively-occluded groups of small meshes.
4. **`checkTransparency` fourth** — Disable if you know there are no transparent materials occluding geometry.
5. **`action` last** — Default is `Hide` (2), which is safer for initial testing. Switch to `Delete` (0) for permanent removal.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Known hidden meshes not detected | `maximumGridResolution` | Increase | Grid too coarse to detect enclosure |
| Small gaps causing false negatives | `minimumGapSize` | Increase | Gaps treated as openings |
| Visible meshes incorrectly removed | `action` | Use Deactivate/Hide | Safer; review results before deleting |
| Slow processing | `maximumGridResolution` | Decrease | Trade accuracy for speed |

## Starting Configs

**Standard detection**:
```json
[{"operation": "findOccludedMeshes", "maximumGridResolution": 500.0, "action": 0}]
```

**Conservative detection** (review before acting):
```json
[{"operation": "findOccludedMeshes", "maximumGridResolution": 256.0, "action": 2}]
```

**Aggressive detection** (close small gaps):
```json
[{"operation": "findOccludedMeshes", "maximumGridResolution": 1024.0, "minimumGapSize": 1.0, "action": 0}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Most effective on architectural/industrial scenes where interior objects are hidden by walls/enclosures.
- Common pipeline: `findOccludedMeshes` → `removePrims` → `pruneLeaves`.

## Known Limitations

- Voxel-based detection may miss very thin occluders.
- Transparency checking requires materials to be resolvable.
- GPU mode is hidden and requires CUDA availability.

## Performance — CPU vs GPU

Measured on an NVIDIA L40:

| Stage size | CPU | GPU | Speedup | Notes |
|---|---|---|---|---|
| 8 MB | 60.31 s | 0.52 s | ~116× | 76 issues either side |
| 438 MB | 459.33 s | 61.67 s | ~7.4× | CPU 150,354 hidden vs GPU 149,678 — ~0.5% drift, presumably FP precision in the voxel rasterisation |

GPU is the default and the right choice on any CUDA host. Disable only if
debugging a CPU/GPU result discrepancy.