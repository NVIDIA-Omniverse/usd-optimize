<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Optimize Materials

**Key:** `optimizeMaterials`
**Source:** `source/operations/optimizeMaterials/OptimizeMaterials.cpp`

## Overview

Optimize Materials reduces the number of materials in a scene by deduplicating identical materials and consolidating similar ones. This reduces draw calls and simplifies material management.

**`optimizeMaterialsMode`** controls the optimization strategy: deduplication finds materials with identical properties, while consolidation groups materials with similar properties.

**`materialsPath`** specifies where consolidated materials are created. The operation respects composition arcs — materials introduced via references cannot be deleted from the current edit target.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `materialPrimPaths` | string[] | `[]` (all materials) | Material prim paths to process. Empty = all materials. |
| `optimizeMaterialsMode` | enum | (default) | Optimization mode: deduplicate, consolidate, etc. |
| `materialsPath` | string | `""` | Path where consolidated materials are created. |
| `analysisCheckPrimvars` | bool | `false` | Check primvar bindings during analysis. Hidden. |

## Tuning Order

1. **`optimizeMaterialsMode` first** — Choose between deduplication (safe) and consolidation (more aggressive).
2. **`materialsPath` second** — Set a destination for consolidated materials.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Materials look swapped or shaders changed after run | `optimizeMaterialsMode` | Switch to deduplicate-only | Consolidation merges similar (not identical) materials; if visual fidelity matters, prefer the safer deduplicate mode. |
| Stale materials still present in scene | n/a | — | Materials introduced via external references can't be removed in the current edit target — flatten or relocate them first. |

## Starting Configs

**Deduplicate materials**:
```json
[{"operation": "optimizeMaterials"}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage with materials.
- Supports analysis mode for Asset Validator integration.
- Common pipeline: `optimizeMaterials` → `removeUnusedUVs`.

## Known Limitations

- Materials from external references cannot be deleted in the current edit target.
- Hidden `analysisCheckPrimvars` is for Asset Validator integration.