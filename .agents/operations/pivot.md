<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compute Pivot

**Key:** `pivot`
**Source:** `source/operations/pivot/Pivot.cpp`

## Overview

Compute Pivot recalculates and sets pivot points (transform origins) for meshes or transforms. This is useful when meshes have been moved or merged and their pivot no longer corresponds to a meaningful location (e.g., center of mass, bottom center).

**`method`** controls the pivot computation method. **`applyTo`** determines whether pivots are set on meshes, transforms, or both. **`overwrite`** controls whether existing pivots are replaced.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `meshPrimPaths` | string[] | `[]` (all) | Prim paths to process. Empty = all. |
| `overwrite` | bool | `true` | Overwrite existing pivot points. |
| `applyTo` | enum | (default) | What to apply pivots to: meshes, transforms, or both. |
| `method` | enum | (default) | Pivot computation method (e.g., center of mass, bounding box center, bottom center). |

## Tuning Order

1. **`method` first** — Choose the desired pivot location.
2. **`applyTo` second** — Select which prim types to affect.
3. **`overwrite` third** — Disable to preserve existing pivots.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Object visibly translates after pivot recompute | `overwrite` | Disable, or compensate transform externally | Changing the pivot without compensating the transform shifts the object's apparent position. |
| Pivot at unexpected location (e.g., far from mesh) | `method` | Switch to a different method | E.g., bounding-box center vs. bottom center vs. center of mass produce different pivots. |

## Starting Configs

**Recompute all pivots**:
```json
[{"operation": "pivot", "overwrite": true}]
```

## Prerequisites & Workflows

- Works standalone on any mesh or transform prim.
- Useful after `merge` or `flattenHierarchy` when pivots need to be recalculated.

## Known Limitations

- Pivot computation depends on mesh extents, which may be expensive for large meshes.