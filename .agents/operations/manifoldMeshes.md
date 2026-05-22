<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Manifold Meshes

**Key:** `manifoldMeshes`
**Source:** `source/operations/manifoldMeshes/Manifold.cpp`

## Overview

**Legacy command — use `meshCleanup` with `makeManifold: true` instead.** This operation exists for backward compatibility.

Manifold Meshes repairs non-manifold topology in meshes, producing watertight manifold meshes. A manifold mesh is one where every edge is shared by exactly two faces, every vertex is surrounded by a single fan of faces, and there are no self-intersections.

Manifold topology is required by many geometry processing operations (Boolean operations, 3D printing, physics simulation). This operation fixes non-manifold edges, vertices, and other topological defects while transferring mesh attributes.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |

## Tuning Order

_Not applicable — single parameter; nothing to order._

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Vertex count or topology changed unexpectedly | n/a | — | Expected — making a mesh manifold may add/remove vertices and faces. Inspect specific meshes via `paths` if regression is suspected. |
| Mesh appears holes-filled or split | n/a | — | Manifold repair may close non-manifold edges, which can fill holes or duplicate faces; use `meshCleanup` with `makeManifold: true` for finer control. |

## Starting Configs

**Make all meshes manifold**:
```json
[{"operation": "manifoldMeshes"}]
```

**Specific meshes only**:
```json
[{"operation": "manifoldMeshes", "paths": ["/World/Engine/Block", "/World/Engine/Head"]}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Required before operations that need manifold input (Boolean operations, shrinkwrap with strict topology).
- Common pipeline: `meshCleanup` → `manifoldMeshes` → further processing.

## Known Limitations

- May significantly modify mesh topology to achieve manifold status.
- Vertex count may change during the process.
- Runs on CPU only (GPU path was removed as CPU was faster for this operation).