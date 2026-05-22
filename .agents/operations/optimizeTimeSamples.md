<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Optimize Time Samples

**Key:** `optimizeTimeSamples`
**Source:** `source/operations/optimizeTimeSamples/OptimizeTimeSamples.cpp`

## Overview

Optimize Time Samples removes redundant time samples from animated attributes. In USD, animated values are stored as time samples — one per frame (or sub-frame). If consecutive samples have the same value, the duplicates waste memory and I/O bandwidth.

The operation detects and removes time samples where the value hasn't meaningfully changed. **`removeInterpolated`** also removes samples that can be perfectly reconstructed by linear interpolation between their neighbors.

**`epsilonD`** and **`epsilonF`** control the comparison tolerance for double and float values respectively — values within epsilon are considered equal.

**Key insight:** this is a lossless or near-lossless optimization. With zero epsilon, it's perfectly safe. With non-zero epsilon, it trades imperceptible precision for significant file size reduction.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to process. Empty = all prims. |
| `attributes` | string[] | `[]` | Specific attribute names to process. Hidden. |
| `removeInterpolated` | bool | `false` | Remove samples that can be reconstructed by linear interpolation. |
| `epsilonD` | double | `1e-12` | Comparison tolerance for double-precision values. |
| `epsilonF` | float | `1e-6` | Comparison tolerance for single-precision values. |
| `attributePaths` | string[] | `[]` | Specific attribute paths to process. Hidden. |

## Tuning Order

1. **`removeInterpolated` first** — Enable for maximum compression. Default is off for strict sample preservation.
2. **`epsilonD` / `epsilonF` second** — Defaults are near-zero (1e-12 and 1e-6). Increase for more aggressive near-lossless compression.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Animation looks jittery after optimization | `epsilonD/F` | Decrease | Tolerance too high, removing important samples |
| File size not reducing enough | `removeInterpolated` | Enable | Removes interpolatable samples |
| Sub-frame animation lost | `epsilonD/F` | Decrease to 0 | Ensure no samples are removed |

## Starting Configs

**Lossless optimization**:
```json
[{"operation": "optimizeTimeSamples", "removeInterpolated": true, "epsilonD": 0.0, "epsilonF": 0.0}]
```

**Near-lossless optimization** (small tolerance):
```json
[{"operation": "optimizeTimeSamples", "removeInterpolated": true, "epsilonD": 1e-7, "epsilonF": 1e-5}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage with animated attributes.
- Safe to run on any scene — worst case, it finds nothing to optimize.
- Common pipeline: animation export → `optimizeTimeSamples` → delivery.

## Known Limitations

- Hidden `attributes` and `attributePaths` parameters are for programmatic filtering.
- Non-numeric attribute types are not optimized.
- Epsilon values should be chosen carefully for each use case — too large and visible animation changes may occur.