<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Triangulate Meshes

**Key:** `triangulateMeshes`
**Source:** `source/operations/triangulateMeshes/Triangulate.cpp`

## Overview

Triangulate Meshes converts all polygon faces to triangles. This is required by some renderers and simulation tools that only support triangle meshes.

The operation splits non-triangle faces (quads, n-gons) into triangles while transferring all mesh attributes. GPU acceleration is used for large meshes above the vertex count threshold.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `gpuVertexCountThreshold` | int | `1000000` | Vertex count above which GPU is used. Hidden. |

## Tuning Order

_Not applicable — single user-visible parameter (`paths`); the GPU threshold is internal._

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Face count grows much more than expected | n/a | — | Expected — each n-gon becomes n−2 triangles. If counts are extreme, the source likely had high-vertex n-gons. |
| Mesh appearance changed (shading seams, etc.) | n/a | — | Triangulation should preserve attributes; if shading shifts, check that primvars (normals/UVs) interpolate as expected on the new triangles. |

## Starting Configs

**Triangulate all meshes**:
```json
[{"operation": "triangulateMeshes"}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Required before operations that assume triangle-only meshes.

## Known Limitations

- Increases face count (each quad becomes 2 triangles, each n-gon becomes n-2 triangles).
- GPU threshold is hidden; GPU acceleration requires CUDA.