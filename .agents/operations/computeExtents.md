<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Compute Extents

**Key:** `computeExtents`
**Source:** `source/operations/computeExtents/ComputeExtentsPlugin.cpp`

## Overview

Compute Extents calculates and authors the `extent` attribute for meshes. The extent is an axis-aligned bounding box that USD uses for efficient spatial queries and culling. Missing or incorrect extents can cause rendering artifacts or incorrect spatial lookups.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |

## Tuning Order

_Not applicable — single parameter; nothing to order._

## Visual Diagnosis

_Not applicable — no rendered output to diagnose. Effects are an authored `extent` attribute used for spatial queries/culling._

## Starting Configs

**Compute all extents**:
```json
[{"operation": "computeExtents"}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Useful after mesh modification operations that don't update extents.

## Known Limitations

- Only computes extents for mesh prims.