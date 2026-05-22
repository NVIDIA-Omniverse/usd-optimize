<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Subdivide Meshes

**Key:** `subdivideMeshes`
**Source:** `source/operations/subdivideMeshes/Subdivide.cpp`

## Overview

Subdivide Meshes increases mesh polygon density by subdividing faces. This is useful for adding detail to coarse meshes before displacement mapping or for improving mesh quality.

The operation supports Catmull-Clark and Loop subdivision with crease and spike sharpness preservation. **`method`** selects the subdivision algorithm. **`iterationCount`** controls how many subdivision passes to apply — each iteration roughly quadruples the face count, so use sparingly.

**`faceCountLimit`** provides a safety cap: meshes exceeding this face count after subdivision are skipped. **`gpuFaceCountThreshold`** controls when GPU acceleration is used.

**Key insight:** subdivision is expensive — each iteration quadruples the face count. Start with 1 iteration and increase only if needed.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `gpuFaceCountThreshold` | int | `4000` | Face count above which GPU is used. |
| `faceCountLimit` | int | `2000000` | Maximum face count after subdivision. Meshes exceeding this are skipped. |
| `method` | enum | `Catmull-Clark` | Subdivision method: `Catmull-Clark`, `Loop`, `Bilinear`, etc. |
| `iterationCount` | int | `1` | Number of subdivision iterations. |

## Tuning Order

1. **`iterationCount` first** — Start at 1. Each additional iteration 4x's face count.
2. **`method` second** — `Catmull-Clark` is the most common. `Loop` is for triangle meshes. `Bilinear` for simple refinement.
3. **`faceCountLimit` third** — Set a safety limit to prevent memory issues on large meshes.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Mesh still too coarse | `iterationCount` | Increase | Add another subdivision pass |
| Extremely slow or out of memory | `iterationCount` | Decrease | Fewer passes, or increase faceCountLimit only if needed |
| Wrong surface curvature | `method` | Change | Catmull-Clark for quads, Loop for triangles |

## Starting Configs

**Single subdivision pass**:
```json
[{"operation": "subdivideMeshes", "iterationCount": 1, "method": 0}]
```

**Dense subdivision** (use with caution):
```json
[{"operation": "subdivideMeshes", "iterationCount": 2, "faceCountLimit": 50000000}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Often used before displacement mapping or physics simulation that requires higher mesh density.

## Known Limitations

- Face count grows geometrically (4^n) — 3 iterations on a 10K face mesh produces 640K faces.
- GPU acceleration requires CUDA.
- Large meshes may hit the `faceCountLimit` safety cap.