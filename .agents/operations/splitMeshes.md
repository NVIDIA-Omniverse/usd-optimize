<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Split Meshes

**Key:** `splitMeshes`
**Source:** `source/operations/splitMeshes/SplitMeshes.cpp`

## Overview

Split Meshes breaks meshes into smaller pieces based on geometric connectivity or spatial clustering. This is the complement to `merge` — where merge combines meshes, split separates them.

**`splitOn`** controls the split criterion: `Vertices` (0) splits based on vertex connectivity (disjoint vertex groups become separate pieces), `Geom Subsets` (1) splits based on existing geometry subsets (e.g., material assignments).

**`method`** controls how split pieces are output: `Geom Subsets` (0) keeps them as geometry subsets of the original mesh, `Meshes` (1) creates separate mesh prims.

For spatial clustering, **`spatialMode`** enables splitting by bounding-box proximity. **`spatialThreshold`** controls how close pieces must be to cluster together, and **`spatialMaxSize`** limits cluster size.

**Key insight:** vertex-based splitting is the most common mode. Use it to break disjoint meshes (meshes with disconnected parts) into separate prims for independent culling and processing.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `splitOn` | enum | `Vertices` (0) | Split criterion: `Vertices` (0) or `Geom Subsets` (1). |
| `method` | enum | `Meshes` (1) | Output method: `Geom Subsets` (0) or `Meshes` (1). |
| `splitCollocatedPoints` | bool | `false` | Treat collocated (same position) vertices as separate for splitting. |
| `originalGeomOption` | enum | `Delete` (1) | What to do with original geometry: `Ignore` (0), `Delete` (1), `Deactivate` (2), `Hide` (3). |
| `spatialMode` | enum | `None` (0) | Spatial clustering: `None` (0), `BoundingBox` (1), `VertexCount` (2). |
| `considerMaterials` | bool | `false` | Keep differently-materialed faces in separate split groups. |
| `mergePoint` | enum | `Stage` (0) | Where to place split output: `Stage` (0), `Parent Xform` (1), `Kind: Assembly` (2), `Kind: Group` (3), `Kind: Component` (4), `Kind: Model` (5), `Kind: Subcomponent` (6), `Root Prim` (7), `Parent Prim` (8), `Original Prim` (9). |
| `rootPath` | string | `""` | Root path for split output prims. |
| `considerAllAttributes` | bool | `false` | Consider all attributes (not just materials) for splitting. |
| `spatialThreshold` | float | `10.0` | Spatial clustering distance threshold. Visible when spatialMode=BoundingBox. |
| `spatialMaxSize` | float | `0.0` | Maximum spatial cluster size. Visible when spatialMode=BoundingBox. |
| `spatialVertexCount` | int | `10000` | Target vertex count per cluster. Visible when spatialMode=VertexCount. |
| `treatAsPrimvars` | string[] | `[]` | Attributes to treat as primvars during split. |
| `spatialDebug` | bool | `false` | Enable spatial clustering debug output. |
| `multiCluster` | string | `""` | JSON string with per-mesh clustering overrides. Hidden. |

## Tuning Order

1. **`splitOn` first** — `Vertices` (0) for standard connectivity-based splitting. `Geom Subsets` (1) to split along existing subset boundaries.
2. **`method` second** — `Meshes` (1) creates new mesh prims; `Geom Subsets` (0) keeps the original mesh with subsets.
3. **`spatialMode` third** — Enable for proximity-based clustering of split pieces.
4. **`spatialThreshold` / `spatialMaxSize` fourth** — Tune clustering distance and maximum cluster size.
5. **`considerMaterials` fifth** — Keep true to preserve material boundaries.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Mesh not splitting at expected points | `splitOn` | Try Geom Subsets (1) | Splits along existing subset boundaries |
| Too many tiny pieces | `spatialMode` | Enable BoundingBox | Groups nearby pieces together |
| Material assignments lost | `considerMaterials` | Enable | Preserves material groups |
| Original mesh still visible | `originalGeomOption` | Set to Delete | Remove source after splitting |

## Starting Configs

**Standard vertex split**:
```json
[{"operation": "splitMeshes", "splitOn": 0, "method": 1}]
```

**Split with spatial clustering**:
```json
[{"operation": "splitMeshes", "splitOn": 0, "method": 1, "spatialMode": 1, "spatialThreshold": 10.0, "spatialMaxSize": 100.0}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Most useful for disjoint meshes (single prims containing disconnected geometry).
- Common pipeline: `sparseMeshes` (analysis) → `splitMeshes` for disjoint meshes.

## Known Limitations

- `multiCluster` is a hidden JSON parameter for programmatic per-mesh overrides (used by `sparseMeshes`).
- Spatial clustering thresholds are in world units.