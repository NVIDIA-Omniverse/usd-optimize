<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Edit Stage Metrics

**Key:** `editStageMetrics`
**Source:** `source/operations/editStageMetrics/EditStageMetrics.cpp`

## Overview

Edit Stage Metrics modifies a stage's global metrics — up axis and linear units. This is essential when scenes from different sources use different coordinate systems or unit scales.

**`upAxis`** changes the stage up axis (Y or Z). **`metersPerUnit`** sets the meters-per-unit value. When changing axes or units, the operation can optionally collapse transforms to maintain visual consistency and ignore Omniverse cameras that shouldn't be affected.

**Key insight:** changing stage metrics doesn't automatically rescale geometry — it changes the interpretation of existing values. Enable `collapseXforms` to apply compensating transforms.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `upAxis` | enum | `None` (0) | Target up axis: `None` (0) = no change, `Y` (1), `Z` (2). |
| `metersPerUnit` | float | `0.01` | Meters per unit (e.g., 0.01 = centimeters, 1.0 = meters). 0.0 = no change. |
| `collapseXforms` | bool | `false` | Collapse transforms to maintain visual appearance after axis change. |
| `ignoreKitCameras` | bool | `true` | Exclude Omniverse-authored cameras from transformation. |

## Tuning Order

1. **`upAxis` first** — Set the desired up axis.
2. **`metersPerUnit` second** — Set the desired unit scale.
3. **`collapseXforms` third** — Enable to preserve visual appearance.

## Visual Diagnosis

_Not applicable — no rendered output to diagnose. Verify by checking the stage's `upAxis` and `metersPerUnit` metadata after running._

## Starting Configs

**Convert to Y-up, centimeters**:
```json
[{"operation": "editStageMetrics", "upAxis": 1, "metersPerUnit": 0.01, "collapseXforms": true}]
```

**Convert to Z-up, meters**:
```json
[{"operation": "editStageMetrics", "upAxis": 2, "metersPerUnit": 1.0, "collapseXforms": true}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage.
- Should be run before other operations when stage metrics need correction.

## Known Limitations

- Changing stage metrics after other operations may require re-running those operations.
- `ignoreKitCameras` skips cameras with Omniverse-specific attributes (e.g., `omni:kit:centerOfInterest`).