<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Optimize Primvars

**Key:** `optimizePrimvars`
**Source:** `source/operations/optimizePrimvars/OptimizePrimvars.cpp`

## Overview

Optimize Primvars reduces memory usage by optimizing how primvar (per-vertex/per-face attributes like UVs, colors) data is stored. It can index primvars, flatten indexed primvars, simplify constant primvars, or remove primvars entirely.

**`mode`** controls the optimization action per primvar:
- `Ignore` — skip
- `Index` — add indices to share duplicate values
- `IndexForced` — index even if it increases data size
- `Flatten` — remove indices, expanding to per-face-vertex
- `Remove` — delete the primvar

**`simplify`** detects primvars where all values are the same and converts them to constant interpolation, saving significant memory.

**Key insight:** indexing is almost always beneficial — it reduces data size by sharing duplicate values. The `simplify` option catches the common case of uniform-value primvars.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `primvars` | string[] | `[]` | Specific primvar names to process. Empty = all primvars. |
| `mode` | enum | `Ignore` (0) | Optimization mode: `Ignore` (0), `Index` (1), `IndexForced` (2), `Flatten` (3), `Remove` (4). |
| `simplify` | bool | `false` | Convert uniform-value primvars to constant interpolation. |
| `removeIfBound` | bool | `false` | Remove primvars even if a material is bound (risky — may break material evaluation). |
| `primvarPaths` | string[] | `[]` | Specific primvar paths to process. Hidden. |

## Tuning Order

1. **`mode` first** — Default is `Ignore` (0). Set to `Index` (1) for the safest and most common optimization.
2. **`simplify` second** — Default is off. Enable to catch constant-value primvars (always safe).
3. **`primvars` third** — Optionally restrict to specific primvar names if you don't want to optimize all.
4. **`removeIfBound` last** — Only enable if you know the primvars are truly unused.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Materials rendering incorrectly | `removeIfBound` | Disable | Removed primvars that materials needed |
| No visible change but less memory | — | — | Optimization working as intended |
| File size not reduced | `mode` | Set to Index | Flatten increases data size |

## Starting Configs

**Standard optimization** (index all primvars):
```json
[{"operation": "optimizePrimvars", "mode": 1, "simplify": true}]
```

**Remove specific primvars**:
```json
[{"operation": "optimizePrimvars", "primvars": ["st1", "st2"], "mode": 4}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Safe to run after any mesh modification operation.
- Common pipeline: `merge` → `optimizePrimvars` → delivery.

## Known Limitations

- `Remove` mode with `removeIfBound=true` may break material evaluation.
- Hidden `primvarPaths` is for programmatic use.
- Supports analysis mode for Asset Validator integration.