<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Optimize Skeleton Roots

**Key:** `optimizeSkelRoots`
**Source:** `source/operations/optimizeSkelRoots/OptimizeSkelRoots.cpp`

## Overview

Optimize Skeleton Roots merges all skinned meshes within each UsdSkelRoot to improve GPU skinning performance. In many character rigs, multiple small meshes are bound to the same skeleton — merging them into fewer, larger meshes reduces draw calls and allows more efficient GPU skinning computation.

The operation:
1. Finds all `UsdSkelRoot` prims in the stage.
2. For each `UsdSkelSkeleton` prim under a root, collects all skinned mesh targets bound to that skeleton.
3. Merges the meshes using the standard merge operation, with skeleton-aware bucketing.
4. Rebinds the merged meshes to the original skeleton.

Meshes with blend shapes are skipped as they cannot be safely merged.

This is a zero-argument operation with no tunable parameters.

## Parameters

None.

## Tuning Order

_Not applicable — no parameters to tune._

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Skinned meshes still many after run | n/a | — | Meshes with blend shapes are skipped intentionally; nested `SkelRoot`s are skipped. Inspect each `SkelRoot` for those cases. |
| Mesh appearance changed after merge | n/a | — | Skeleton-aware merging should preserve visuals; if not, check that all source meshes shared the same skeleton bindings before running. |

## Starting Configs

```json
[{"operation": "optimizeSkelRoots"}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage with UsdSkel setups.
- Most effective on character scenes with many small skinned meshes per skeleton.
- Internally uses the `merge` operation with skeleton-aware configuration.

## Known Limitations

- Meshes with blend shapes are skipped.
- Nested SkelRoots are not supported — if a SkelRoot is nested inside another, only the outermost root is processed and inner roots are skipped.
- The merge operation is invoked internally with default settings (meshes are merged regardless of spatial proximity, so all skinned meshes under one skeleton become a single mesh).