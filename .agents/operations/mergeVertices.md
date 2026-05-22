<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Merge Vertices

**Key:** `mergeVertices`
**Source:** `source/operations/mergeVertices/MergeVertices.cpp`

## Overview

**Legacy command — use `meshCleanup` instead.** This operation exists for backward compatibility. `meshCleanup` includes vertex merging plus additional cleanup steps.

Merge Vertices welds vertices that are within a distance tolerance, reducing vertex count and fixing mesh seams.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `tolerance` | float | `0.0` | Distance tolerance for merging, in world units. |
| `mergeBoundaries` | bool | `true` | Merge vertices on mesh boundaries. |
| `removeIsolatedVertices` | bool | `true` | Remove vertices not connected to any face after merging. |
| `makeManifold` | bool | `true` | Repair non-manifold topology after merging. |

## Tuning Order

1. **`tolerance` first** — Start with default. Increase if seams remain visible.
2. **`mergeBoundaries` second** — Disable if boundary edges should remain separate.
3. **`makeManifold` last** — Enable only if manifold topology is required downstream.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Visible seams remain after merging | `tolerance` | Increase | Tolerance is in world units; raise gradually until seams close. Values that are too high cause unintended welds across distinct geometry. |
| Boundaries collapse unexpectedly | `mergeBoundaries` | Set to `false` | Disables welding across mesh boundary edges (e.g., between distinct shells). |
| Stray vertices remain after merge | `removeIsolatedVertices` | Enable | Removes vertices left disconnected after the weld pass. |

## Starting Configs

**Standard vertex merge**:
```json
[{"operation": "mergeVertices", "tolerance": 0.0001}]
```

## Prerequisites & Workflows

- Hidden operation (`getVisible()` returns false).
- `meshCleanup` is the recommended alternative for most use cases.

## Known Limitations

- Hidden operation — not shown in the UI.
- Tolerance is in world units.