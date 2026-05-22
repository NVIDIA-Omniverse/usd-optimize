<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Dice Meshes

**Key:** `diceMeshes`
**Source:** `source/operations/diceMeshes/DiceMeshes.cpp`

## Overview

Dice Meshes cuts meshes into smaller pieces along a 3D grid — like slicing a block of cheese with a wire grid. This is useful for breaking large, sparse meshes into smaller pieces that can be individually culled by the renderer, improving draw performance.

The operation overlays a uniform 3D grid on the mesh and precisely cuts faces that cross cell boundaries, using double precision internally for numerical stability when faces are near grid boundaries. The grid can be regular (uniform cell size) or irregular (custom cut heights per axis), and the up-vector parameters define a custom coordinate frame for the grid.

**`gridCellX/Y/Z`** control cell size — smaller cells produce more pieces but better culling. **`splitDices`** controls whether the diced pieces remain as one mesh or become separate prims (separate prims enable instance culling but increase prim count).

**Key insight:** cell size should be tuned relative to the scene's median mesh extent. Cells much larger than most objects won't help; cells much smaller create excessive fragments.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `splitDices` | bool | `false` | Whether to split diced pieces into separate prims. |
| `gridType` | enum | `Regular` (0) | Grid type: `Regular` (0) or `Irregular` (1). |
| `cutHeightsX` | string | `""` | Space-separated cut heights along X (Irregular grid only). |
| `cutHeightsY` | string | `""` | Space-separated cut heights along Y (Irregular grid only). |
| `cutHeightsZ` | string | `""` | Space-separated cut heights along Z (Irregular grid only). |
| `gridCellX` | float | `0.0` | Grid cell size along X in world units (Regular grid only). |
| `gridCellY` | float | `0.0` | Grid cell size along Y in world units (Regular grid only). |
| `gridCellZ` | float | `0.0` | Grid cell size along Z in world units (Regular grid only). |
| `gridOriginX` | float | `0.0` | Grid origin X coordinate. |
| `gridOriginY` | float | `0.0` | Grid origin Y coordinate. |
| `gridOriginZ` | float | `0.0` | Grid origin Z coordinate. |
| `advancedSettings` | bool | `false` | Show advanced up-vector settings. |
| `upVectorAx` | float | `1.0` | Up vector A, X component. Hidden behind advancedSettings. |
| `upVectorAy` | float | `0.0` | Up vector A, Y component. Hidden behind advancedSettings. |
| `upVectorAz` | float | `0.0` | Up vector A, Z component. Hidden behind advancedSettings. |
| `upVectorBx` | float | `0.0` | Up vector B, X component. Hidden behind advancedSettings. |
| `upVectorBy` | float | `1.0` | Up vector B, Y component. Hidden behind advancedSettings. |
| `upVectorBz` | float | `0.0` | Up vector B, Z component. Hidden behind advancedSettings. |
| `upVectorCx` | float | `0.0` | Up vector C, X component. Hidden behind advancedSettings. |
| `upVectorCy` | float | `0.0` | Up vector C, Y component. Hidden behind advancedSettings. |
| `upVectorCz` | float | `1.0` | Up vector C, Z component. Hidden behind advancedSettings. |

## Tuning Order

1. **`gridCellX/Y/Z` first** — These control the resolution of the dice. Start with values proportional to the scene's median mesh extent (e.g., 10x median extent). Decrease for finer culling.
2. **`gridType` second** — Use `Regular` for uniform grid cells. Use `Irregular` with custom cut heights for non-uniform dicing.
3. **`splitDices` third** — Enable to get separate prims (better for instanced culling); disable to keep a single mesh with subset faces.
4. **`gridOriginX/Y/Z` fourth** — Adjust if the default grid origin causes cuts through important features.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Too many small fragments | `gridCellX/Y/Z` | Increase | Cells are too small relative to scene |
| No visible improvement in culling | `gridCellX/Y/Z` | Decrease | Cells are too large to split sparse meshes |
| Cuts through important features | `gridOriginX/Y/Z` | Adjust | Shift grid to avoid feature boundaries |
| Need non-uniform cuts | `gridType` | Switch to Irregular | Use custom cut heights per axis |

## Starting Configs

**Standard dicing** (good default for large scenes in centimeters):
```json
[{"operation": "diceMeshes", "gridCellX": 100.0, "gridCellY": 100.0, "gridCellZ": 100.0, "splitDices": true}]
```

**Fine dicing** (smaller cells for detailed culling):
```json
[{"operation": "diceMeshes", "gridCellX": 25.0, "gridCellY": 25.0, "gridCellZ": 25.0, "splitDices": true}]
```

**Coarse dicing** (fewer cuts, lower overhead):
```json
[{"operation": "diceMeshes", "gridCellX": 500.0, "gridCellY": 500.0, "gridCellZ": 500.0, "splitDices": false}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Often suggested by the `sparseMeshes` analysis operation for large sparse meshes.
- Common pipeline: `sparseMeshes` (analysis) → `diceMeshes` for large meshes, `splitMeshes` for disjoint meshes.

## Known Limitations

- Custom cut heights (`cutHeightsX/Y/Z`) are space-separated (not comma-separated).
- The advanced up-vector settings (`upVectorA/B/C`) define a custom coordinate frame for object-aligned dicing and are hidden by default.
- Cell sizes are in world units and must be scaled with scene units.