<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Sparse Meshes

**Key:** `sparseMeshes`
**Source:** `source/operations/sparseMeshes/SparseMeshes.cpp`

## Overview

Sparse Meshes is a hidden analysis operation that identifies meshes with poor spatial density — geometry that occupies a large bounding box relative to its actual surface area. These sparse meshes are poor candidates for frustum culling because their bounding box extends far beyond their visible geometry.

The operation categorizes sparse meshes into two types:

1. **Disjoint sparse meshes** — single meshes containing disconnected geometry (e.g., a single mesh prim with multiple separate objects). The operation suggests splitting these via `splitMeshes`.
2. **Large sparse meshes** — contiguous meshes that are large relative to the scene median but have low density. The operation suggests dicing these via `diceMeshes`.

The output includes suggested operations with pre-computed parameters tuned to the scene's characteristics (median extent size, spatial thresholds, etc.).

This is a zero-argument, analysis-only operation.

## Parameters

None.

## Tuning Order

_Not applicable — no parameters to tune._

## Visual Diagnosis

_Not applicable — analysis-only; output is a JSON list of sparse meshes plus suggested follow-up operations (`splitMeshes`/`diceMeshes`)._

## Starting Configs

```json
[{"operation": "sparseMeshes"}]
```

## Prerequisites & Workflows

- Hidden operation (`getVisible()` returns false).
- Analysis-only — does not modify the stage.
- Outputs suggested `splitMeshes` and `diceMeshes` operations with pre-tuned parameters.
- Common pipeline: `sparseMeshes` (analysis) → execute suggested operations.

## Known Limitations

- Hidden operation — not shown in the UI.
- Analysis-only; must be run in analysis mode.
- Thresholds are hardcoded internal constants tuned for typical scenes.
- Returns error if executed in non-analysis mode.