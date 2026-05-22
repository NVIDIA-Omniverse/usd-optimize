<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Merge Static Meshes

**Key:** `merge`
**Source:** `source/operations/merge/Merge.cpp`

## Overview

Merge Static Meshes combines multiple meshes that share common properties into single merged meshes. This reduces scene prim count and draw calls, improving overall rendering performance.

Meshes are grouped ("bucketed") based on shared properties — material bindings, vertex attributes, etc. Within each bucket, meshes are combined into a single mesh. **`considerMaterials`** ensures meshes with different materials stay separate (or are merged with geometry subsets). Spatial clustering options can further subdivide merges to maintain reasonable mesh sizes.

The merge operation uses a clustering module that supports multiple spatial modes: no spatial grouping, bounding-box-based grouping, and vertex-count-based grouping.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `meshPrimPaths` | string[] | `[]` (all meshes) | Prim paths to consider for merging. Empty = all meshes. |
| `considerMaterials` | bool | `false` | Keep differently-materialed meshes separate. |
| `materialAlbedoAsVertexColors` | bool | `false` | Convert material albedo to vertex colors during merge. |
| `originalGeomOption` | enum | `Delete` (0) | What to do with original geometry: `Delete` (0), `Deactivate` (1), `Hide` (2). |
| `mergePoint` | enum | `Default` (0) | Where to create merged prims: `Default` (0), `Root` (1), `Parent` (2). |
| `rootPath` | string | `""` | Root path for merged output prims. |
| `considerAllAttributes` | bool | `false` | Consider all vertex attributes for bucketing. |
| `allowSingleMeshes` | bool | `false` | Allow buckets with a single mesh. |
| `spatialMode` | enum | `None` (0) | Spatial clustering: `None` (0), `BoundingBox` (1), `VertexCount` (2). |
| `spatialThreshold` | float | `10.0` | Spatial clustering distance threshold. |
| `spatialMaxSize` | float | `0.0` | Maximum spatial cluster size. |
| `spatialVertexCount` | int | `10000` | Target vertex count per cluster. |
| `treatAsPrimvars` | string[] | `[]` | Attributes to treat as primvars during merge. |
| `spatialDebug` | bool | `false` | Enable spatial clustering debug output. |

## Tuning Order

1. **`considerMaterials` first** — Keep true to preserve material assignments. Set false to merge everything.
2. **`spatialMode` second** — Enable spatial clustering for large scenes to keep merged meshes at reasonable sizes.
3. **`spatialThreshold` / `spatialMaxSize` third** — Tune clustering parameters.
4. **`originalGeomOption` fourth** — Choose what to do with source meshes after merge.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Want one merged prim per material instead of `GeomSubset`s | `considerMaterials` | Set to `true` | "Keep Materials Separate" — when on, the bucketer hashes by material binding so each material yields its own merged prim. When off (default), differing materials are merged under a single prim with `GeomSubset`s per material; per-material bindings are preserved either way. |
| Merged meshes are too large / cause GPU memory pressure | `spatialMode`, `spatialMaxSize` | Enable / Decrease | Spatial clustering caps cluster size; lowering `spatialMaxSize` produces more, smaller merged meshes. |
| Originals still visible after merge | `originalGeomOption` | Set to `Delete` (0) or `Hide` (2) | Default is `Delete`; choose `Hide` if you need to keep authored prims for downstream workflows. |
| Vertex attributes lost on merge | `considerAllAttributes`, `treatAsPrimvars` | Enable / Add the attribute | Bucketing only matches on the attributes it knows about — opt in for non-standard primvars. |

## Starting Configs

**Standard merge**:
```json
[{"operation": "merge"}]
```

**Merge with spatial clustering**:
```json
[{"operation": "merge", "spatialMode": 1, "spatialThreshold": 100.0, "spatialMaxSize": 500.0}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Most effective on scenes with many small meshes sharing the same material.
- Common pipeline: `merge` → `shrinkwrap` or `merge` → `generateNormals`.

## Known Limitations

- Merged meshes lose individual prim identity.
- Material bindings are preserved via geometry subsets when `considerMaterials=true`.
- Spatial clustering thresholds are in world units.