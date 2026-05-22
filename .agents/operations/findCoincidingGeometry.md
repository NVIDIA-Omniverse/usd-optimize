<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Find Coinciding Geometry

**Key:** `findCoincidingGeometry`
**Source:** `source/operations/findCoincidingGeometry/FindCoincidingGeometry.cpp`

## Overview

Find Coinciding Geometry detects meshes that occupy the same space — overlapping or near-identical geometry that causes z-fighting and wasted rendering. This is common in scenes assembled from multiple sources where duplicate geometry wasn't cleaned up.

The operation uses the same engine behind `deduplicateGeometry`'s fuzzy mode. It computes OBBs via PCA for each mesh and identifies groups of geometrically similar meshes that occupy the same spatial location.

**`tolerance`** controls how close meshes must be to be considered coinciding. **`offset`** adds a spatial offset before comparison (useful for detecting near-misses). **`fuzzy`** enables the OBB-based approximate matching that handles vertex reordering.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `primPaths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `tolerance` | float | `0.0001` | Distance tolerance for coincidence detection. |
| `offset` | float | `0.0` | Spatial offset added before comparison. |
| `fuzzy` | bool | `false` | Enable approximate matching (handles vertex reordering). |

## Tuning Order

1. **`tolerance` first** — Start with default. Increase if known coincident meshes aren't detected.
2. **`fuzzy` second** — Enable if meshes have different vertex ordering but same shape.

## Visual Diagnosis

_Not applicable — analysis-only; results are reported as JSON groups, not visual changes. To diagnose missed/false detections, adjust `tolerance` and re-run._

## Starting Configs

**Standard detection**:
```json
[{"operation": "findCoincidingGeometry", "tolerance": 0.0001}]
```

**Lenient detection**:
```json
[{"operation": "findCoincidingGeometry", "tolerance": 0.01, "fuzzy": true}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Use as a diagnostic before `deduplicateGeometry` or manual cleanup.

## Known Limitations

- Detection is pairwise and may be slow on scenes with many meshes.
- Fuzzy matching is slower than exact comparison.