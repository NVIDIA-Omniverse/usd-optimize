<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Generate Scene

**Key:** `generateScene`
**Source:** `source/operations/generateScene/GenerateScene.cpp`

## Overview

Generate Scene creates synthetic test scenes by procedurally placing meshes in a layout. This is a developer/testing utility for generating scenes with controllable complexity — useful for benchmarking Scene Optimizer operations or stress-testing renderers.

The operation places copies of reference meshes according to a layout pattern (uniform grid or random). Meshes can be instances or unique copies, with optional scale variation and clustering.

**Key insight:** this is a hidden operation intended for internal testing and benchmarking, not for production scene optimization.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `seed` | int | `123456789` | Random seed for reproducible generation. |
| `referenceMeshPaths` | string[] | `[]` | Paths to source meshes to place in the scene. |
| `generatedMeshPath` | string | `""` | Root path where generated meshes are placed. |
| `meshCount` | int | `32` | Total number of meshes to generate. |
| `uniformLayout` | bool | `false` | Use a uniform grid layout. |
| `2DLayout` | bool | `false` | Constrain layout to 2D (XZ plane). |
| `layoutSpacing` | float | `200.0` | Spacing between meshes in the layout. |
| `uniqueMeshPercentage` | float | `0.5` | Fraction of meshes that are unique copies (0.0–1.0). |
| `scaleUniqueMeshes` | bool | `true` | Apply random scale to unique meshes. |
| `clusteredPercent` | float | `0.75` | Fraction of meshes placed in clusters (0.0–1.0). |
| `numClusters` | int | `16` | Number of clusters when clusteredPercent > 0. |
| `materialPaths` | string[] | `[]` | Paths to materials to assign to generated meshes. |

## Tuning Order

1. **`meshCount` first** — Controls scene complexity.
2. **`referenceMeshPaths` second** — Provide source meshes.
3. **`uniformLayout` / `layoutSpacing` third** — Control placement pattern.
4. **`uniqueMeshPercentage` / `scaleUniqueMeshes` fourth** — Control instance vs unique ratio.

## Visual Diagnosis

_Not applicable — synthetic scene generator. Verify by opening the generated scene; layout/density issues indicate adjusting `meshCount`, `layoutSpacing`, or cluster parameters._

## Starting Configs

**Simple grid** (100 instances):
```json
[{"operation": "generateScene", "meshCount": 100, "uniformLayout": true, "layoutSpacing": 2.0, "referenceMeshPaths": ["/World/Source"]}]
```

**Complex benchmark** (mixed unique and instanced):
```json
[{"operation": "generateScene", "meshCount": 10000, "uniformLayout": true, "uniqueMeshPercentage": 0.3, "clusteredPercent": 0.5, "numClusters": 5}]
```

## Prerequisites & Workflows

- Requires at least one reference mesh path to be specified.
- This is a hidden utility operation (`getVisible()` returns false).

## Known Limitations

- Hidden operation — not shown in the UI.
- Intended for testing and benchmarking only.