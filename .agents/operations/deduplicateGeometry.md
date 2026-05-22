<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# De-duplicate Geometry

**Key:** `deduplicateGeometry`
**Source:** `source/operations/deduplicateGeometry/DeduplicateGeometry.cpp`

## Overview

De-duplicate Geometry finds meshes that are geometrically identical (or near-identical) and replaces duplicates with instances of a single prototype. This reduces memory usage and improves rendering performance by eliminating redundant geometry.

The fuzzy matching mode works by computing oriented bounding boxes (OBBs) via PCA for each mesh and mapping them to points in a 3D space based on their half-extents. Meshes that map to nearby points are candidates for matching. This approach is independent of tessellation and vertex ordering, enabling detection of duplicates that differ in mesh topology. The GPU path accelerates the pairwise comparison.

In non-fuzzy mode, the operation compares meshes using vertex data directly and can detect duplicates even when they differ by transform. **`duplicateMethod`** controls how duplicates are handled: `Instanceable Reference` creates USD instanceable references, `Reference` creates non-instanceable references, `Copy Values` bakes the prototype data into duplicates, and `Set Attribute` tags duplicates with an attribute.

**`tolerance`** controls how close vertices must match for meshes to be considered duplicates (maps to `relTolerance` in the underlying library). **`fuzzy`** enables the OBB-based approximate matching. **`allowScaling`** permits matches between meshes that differ only by a uniform scale.

**Key insight:** start with strict matching (low tolerance, fuzzy off) for safe deduplication. Enable fuzzy matching and increase tolerance only if you know meshes should match but have minor differences from CAD conversion artifacts. Fuzzy matching is particularly effective for CAD scenes like assembly lines where identical parts may have been tessellated differently.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `meshPrimPaths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `tolerance` | float | `0.001` | Vertex matching tolerance (stage units, worldspace). |
| `duplicateMethod` | enum | `Instanceable Reference` (2) | How to handle duplicates: `Copy Values` (0), `Reference` (1), `Instanceable Reference` (2), `Set Attribute` (3). |
| `ignoreAttributes` | string[] | `[]` | Attributes/namespaces to ignore during comparison. Entries ending with `:` ignore entire namespaces. Visible for Reference/Instanceable Reference methods. |
| `fuzzy` | bool | `false` | Enable fuzzy matching via OBB-based shape comparison. |
| `allowScaling` | bool | `false` | Allow matches between meshes that differ by uniform scale. Visible when fuzzy is enabled. |
| `considerDeepTransforms` | bool | `true` | Look for duplicates where point values have been uniformly transformed. |
| `useGpu` | bool | `false` | Use GPU for comparison. Hidden. |
| `fuzzyOnly` | bool | `false` | Only perform fuzzy matching (skip exact matching). Hidden. |

## Tuning Order

1. **`tolerance` first** — Start with default 0.001. Increase if known duplicates aren't being detected.
2. **`duplicateMethod` second** — `Instanceable Reference` is generally preferred for memory savings; `Reference` is useful when instanceable references aren't supported downstream; `Copy Values` bakes the duplicate data.
3. **`ignoreAttributes` third** — Add attribute names or namespaces (ending with `:`) to ignore during comparison.
4. **`fuzzy` fourth** — Enable if meshes have been re-exported and vertices are in different order.
5. **`allowScaling` fifth** — Disable if differently-scaled meshes should remain separate.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Known duplicates not detected | `tolerance` | Increase | Meshes may differ slightly from conversion |
| Too many meshes being instanced | `tolerance` | Decrease | Tolerance too generous |
| Differently-sized objects merged | `allowScaling` | Disable | Prevents scale-different matching |
| Textured meshes incorrectly merged | `ignoreAttributes` | Remove UV entries | Ensures UV data is compared |

## Starting Configs

**Standard deduplication**:
```json
[{"operation": "deduplicateGeometry", "tolerance": 0.001, "duplicateMethod": 2}]
```

**Aggressive deduplication** (fuzzy matching):
```json
[{"operation": "deduplicateGeometry", "tolerance": 0.01, "fuzzy": true, "allowScaling": true}]
```

**Strict deduplication** (low tolerance):
```json
[{"operation": "deduplicateGeometry", "tolerance": 0.0001}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Most effective on scenes imported from CAD or scenes with many repeated objects.
- Common pipeline: `fitPrimitives` → `deduplicateGeometry` → `organizePrototypes`.

## Known Limitations

- Fuzzy matching is significantly slower than exact matching.
- GPU acceleration (`useGpu`) is hidden and may not be available on all systems.
- Instance creation requires that the stage supports USD instancing.