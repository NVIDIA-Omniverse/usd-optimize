<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Mesh Cleanup

**Key:** `meshCleanup`
**Source:** `source/operations/meshCleanup/MeshCleanup.cpp`

## Overview

Mesh Cleanup performs a suite of mesh repair operations to fix common topological defects. Each sub-operation can be independently enabled/disabled, making this a flexible "fix-up" pass.

The operations include: merging collocated vertices, contracting degenerate edges, removing degenerate faces, removing isolated vertices, removing duplicate faces, co-orienting face normals, and making meshes manifold.

**`mergeVertices`** is the primary operation — it welds vertices within a distance tolerance, which is often a prerequisite for other cleanup steps. **`tolerance`** controls the weld distance.

**Key insight:** run `mergeVertices` first (it's the foundation), then enable other cleanup steps as needed. For meshes from CAD conversion, enabling everything is usually safe.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `mergeVertices` | bool | `true` | Merge collocated vertices within tolerance. |
| `tolerance` | float | `0.0` | Distance tolerance for vertex merging, in world units. |
| `mergeBoundaries` | bool | `true` | Merge vertices on mesh boundaries (open edges). |
| `mergeNeighbors` | bool | `true` | Merge vertices that share edges. |
| `contractDegenerateEdges` | bool | `true` | Contract edges with zero or near-zero length. |
| `removeDegenerateFaces` | bool | `true` | Remove faces with zero area. |
| `removeIsolatedVertices` | bool | `true` | Remove vertices not connected to any face. |
| `removeDuplicateFaces` | bool | `true` | Remove faces that are exact duplicates. |
| `coorientFaces` | bool | `false` | Make face normals consistent across the mesh. |
| `makeManifold` | bool | `false` | Repair non-manifold topology. |

## Tuning Order

1. **`mergeVertices` + `tolerance` first** — This is the most impactful step. Start with the default tolerance; increase if meshes have gaps between seemingly-connected vertices.
2. **`removeDegenerateFaces` / `contractDegenerateEdges` second** — Clean up zero-area geometry that can cause rendering artifacts.
3. **`removeIsolatedVertices` / `removeDuplicateFaces` third** — Remove waste geometry.
4. **`coorientFaces` fourth** — Enable if normals appear flipped or inconsistent.
5. **`makeManifold` last** — Only enable if downstream operations require manifold topology (e.g., boolean operations, 3D printing).

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Visible seams between mesh parts | `tolerance` | Increase | Vertices not close enough to merge |
| Mesh distorted after cleanup | `tolerance` | Decrease | Tolerance too large, merging unintended vertices |
| Flickering faces in viewport | `removeDegenerateFaces` | Enable | Zero-area faces cause z-fighting |
| Flipped normals on some faces | `coorientFaces` | Enable | Inconsistent face winding |
| Holes appear after other operations | `makeManifold` | Enable | Non-manifold edges need repair |

## Starting Configs

**Standard cleanup (canonical for decimation prep)** — safe, comprehensive
defaults. `tolerance: 0.0` is shown explicitly: it means "merge only exact
coincident vertices" (the precise case CAD imports often leave behind), not
"no merging." Leave `makeManifold: false` here; only enable it when a
downstream op explicitly requires manifold topology, since manifold repair can
rearrange faces in ways the user does not expect:
```json
[{
  "operation": "meshCleanup",
  "mergeVertices": true,
  "tolerance": 0.0,
  "mergeBoundaries": true,
  "mergeNeighbors": true,
  "contractDegenerateEdges": true,
  "removeDegenerateFaces": true,
  "removeIsolatedVertices": true,
  "removeDuplicateFaces": true,
  "makeManifold": false
}]
```

**Full cleanup** (all repairs, including topology-changing manifold repair):
```json
[{"operation": "meshCleanup", "mergeVertices": true, "mergeBoundaries": true, "mergeNeighbors": true, "contractDegenerateEdges": true, "removeDegenerateFaces": true, "removeIsolatedVertices": true, "removeDuplicateFaces": true, "coorientFaces": true, "makeManifold": true}]
```

**With non-zero tolerance** (merge vertices across visible gaps — use with care, may introduce unintended welds in models with intentional seams):
```json
[{"operation": "meshCleanup", "tolerance": 0.001}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Often run after `boxClip` or other operations that may create degenerate geometry.
- Recommended to run before `decimateMeshes` to avoid issues from dirty input meshes.
- Common pipeline: `merge` → `shrinkwrap` → `meshCleanup`.

## Known Limitations

- Tolerance is in world units — adjust for scene scale.
- `makeManifold` can significantly modify mesh topology.
- `coorientFaces` requires connected faces to propagate orientation.
