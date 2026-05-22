<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Remesh Meshes

**Key:** `remeshMeshes`
**Source:** `source/operations/remeshMeshes/Remesh.cpp`

## Overview

Remesh Meshes regenerates mesh topology to create a more uniform triangle distribution while preserving the original shape. It produces a new triangulation with controlled edge-length gradation while staying within a specified error bound from the original surface. Both CPU and GPU paths are available.

**`gradation`** controls the rate of growth of triangle sizes where low surface curvature allows it without exceeding the permitted error. A gradation of 0 forces all edges to be nearly the same length (as controlled by `maxError`), producing uniform but potentially inefficient coverage — flat or low-curvature regions get the same density as high-curvature areas. Higher gradation allows edge lengths to grow gradually across the surface, so fewer, larger triangles cover low-curvature regions while small triangles remain where needed. The trade-off: larger gradation means faster edge-length growth but more deviation from equilateral triangles. **`maxError`** caps the maximum geometric deviation from the original surface (0 = no limit).

**Key insight:** remeshing is most useful for meshes with highly irregular triangulation (e.g., from Boolean operations or poor CAD tessellation). The algorithm always produces triangle meshes regardless of input polygon type.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `gradation` | float | `0.0` | Controls the rate of edge-length growth across low-curvature regions. 0 = uniform edge lengths (no growth), higher = faster growth and fewer triangles in flat areas, but more deviation from equilateral triangles. |
| `maxError` | float | `0.1` | Maximum geometric error allowed. 0 = no limit. |
| `gpuVertexCountThreshold` | int | `500000` | Vertex count above which GPU is used. Hidden. |

## Tuning Order

1. **`gradation` first** — Start at default (0.0) for uniform edge lengths. Increase to allow larger triangles in flat regions, reducing triangle count.
2. **`maxError` second** — Set a non-zero value to limit surface deviation.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Surface visibly drifts from original | `maxError` | Decrease | Lower the cap; `0` removes the cap entirely. |
| Triangles too uniform; flat regions wastefully dense | `gradation` | Increase | Allows edge length to grow over low-curvature areas. |
| Triangles too non-uniform / sliver triangles | `gradation` | Decrease | Closer to 0 forces near-equilateral triangles. |

## Starting Configs

**Standard remesh**:
```json
[{"operation": "remeshMeshes"}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Useful after Boolean operations or CAD import that produces irregular triangulation.

## Known Limitations

- Remeshing may not preserve UV coordinates or other per-vertex attributes.
- GPU threshold is hidden; GPU acceleration requires CUDA.