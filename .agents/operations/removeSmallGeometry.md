<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Remove Small Geometry

**Key:** `removeSmallGeometry`
**Source:** `source/operations/removeSmallGeometry/RemoveSmallGeometry.cpp`

## Overview

Remove Small Geometry finds and removes meshes that are below a size threshold. This cleans up scenes by removing tiny objects that are visually insignificant — screws, bolts, washers, and other small parts that waste rendering resources.

**`detectionMethod`** controls how size is measured: by world-space extent, by face count, or by screen-space projection. **`threshold`** sets the cutoff value. **`removeMethod`** determines whether small meshes are deleted, deactivated, or hidden.

**Key insight:** screen-space detection is the most perceptually accurate (removes objects that would be too small to see) but requires camera information. Extent-based detection is simpler and works without a camera.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `removeMethod` | enum | `Delete` | How to handle small geometry: `Delete`, `Deactivate`, `Hide`. |
| `detectionMethod` | enum | `Extent` | How to measure size: `Extent`, `FaceCount`, etc. |
| `threshold` | float | `1.0` | Size threshold (units depend on detection method). |

## Tuning Order

1. **`detectionMethod` first** — Choose based on available information and needs.
2. **`threshold` second** — Adjust based on detection method. For extent-based: world units. For face count: number of faces.
3. **`removeMethod` third** — Use `Hide` or `Deactivate` initially to review results before deleting.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Visible objects disappear after run | `threshold` | Decrease, or use `Hide`/`Deactivate` first | Threshold is too aggressive for the chosen detection method; preview reversibly before deleting. |
| Small parts remain | `threshold` | Increase | Raise the cutoff. For elongated parts, extent-based detection may overestimate size — try `FaceCount` instead. |
| Wrong objects removed (e.g., camera-relative) | `detectionMethod` | Switch to `Extent` or `FaceCount` | Screen-space detection depends on camera state; world-space methods are scene-independent. |

## Starting Configs

**Remove tiny objects** (extent-based):
```json
[{"operation": "removeSmallGeometry", "detectionMethod": 0, "threshold": 0.5, "removeMethod": 0}]
```

**Safe preview** (hide instead of delete):
```json
[{"operation": "removeSmallGeometry", "detectionMethod": 0, "threshold": 1.0, "removeMethod": 2}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Effective as an early step to reduce scene complexity before more expensive operations.
- Common pipeline: `removeSmallGeometry` → `merge` → further optimization.

## Known Limitations

- Threshold values depend on scene units and the chosen detection method.
- Extent-based detection uses the axis-aligned bounding box, which may overestimate size for elongated objects.