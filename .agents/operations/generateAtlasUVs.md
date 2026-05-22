<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# generateAtlasUVs

**Key:** `generateAtlasUVs`
**Display name:** Auto UV Unwrap
**Source:** `source/operations/generateAtlasUVs/GenerateAtlasUVs.cpp`

## Overview

Auto UV Unwrap generates texture coordinates (UVs) by unfolding mesh surfaces into 2D. It broadly operates via a three-step process:

1. **Segment into patches** — the mesh surface is cut into groups of faces called UV islands. `distortionThreshold` controls how much stretch is tolerated before a patch is split into more pieces. Lower threshold = flatter pieces, more seams.

2. **Flatten each patch to 2D** — each island is parameterized (unfolded) onto the UV plane with minimal angular distortion.

3. **Pack islands into an atlas** — if `enableAtlasPacking=true`, all islands are arranged into a single [0,1]² UV space. UVs are optionally scaled to world-space dimensions (`useWorldSpaceScales`, `scaleFactor`, `scaleUnits`) for consistent texel density across meshes.

**Key insight:** `distortionThreshold` is the primary quality dial. Start at the default of 3.0 and tune from there — everything else is secondary.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` | Prim paths/expressions to process. Empty = all meshes. |
| `distortionThreshold` | float | `3.0` | Max per-patch distortion before splitting into more islands. Must be > 1 (clamped to 1.05 internally). Lower = less distortion, more seams. |
| `enableAtlasPacking` | bool | `true` | Pack UV islands into a single atlas. Disable to keep islands independently oriented. |
| `useWorldSpaceScales` | bool | `true` | Scale UV islands proportional to the world-space dimensions of the source mesh for consistent texel density. |
| `scaleFactor` | float | `0.01` | Uniform scale applied to UV islands (texel density multiplier). Only active when `useWorldSpaceScales=true`. |
| `scaleUnits` | float presets | `0.0` | Real-world unit in which `scaleFactor` is expressed (e.g. meters, centimeters). |
| `overwriteExisting` | bool | `true` | Overwrite existing `st` primvar if already present on the mesh. |

**distortionThreshold note:** the value passed is clamped to `max(1.05, distortionThreshold)` at line 145 of the source. Passing values ≤ 1 is equivalent to passing 1.05.

## Tuning Order

1. **`distortionThreshold` first** — this is the primary quality knob. Start at 3.0 and adjust based on the distortion vs. seam count trade-off you need.
2. **`enableAtlasPacking` second** — only disable for diagnostic purposes or workflows where independent island layout is required.
3. **`useWorldSpaceScales` / `scaleFactor` / `scaleUnits` third** — tune texel density after the UV shape looks correct. Adjust `scaleFactor` conservatively: if islands are far outside or inside [0,1]², try halving or doubling; if close, make smaller adjustments (e.g., ±20–30%).
4. **`overwriteExisting` last** — workflow flag, not a quality parameter.

## Visual Diagnosis

> **Screenshot guidance:** For this operation, ask for a screenshot of the **UV atlas** (the 2D UV layout), not the 3D mesh. Use a UV editor or apply a checkerboard texture to visualize island shape, distortion, and packing. A 3D viewport screenshot is not useful for diagnosing UV quality.

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Too many visible seams | `distortionThreshold` | Increase | Fewer islands, more distortion allowed |
| Stretching or shearing in textures | `distortionThreshold` | Decrease | More islands, each flatter |
| Islands overlapping in atlas | `enableAtlasPacking` | Ensure `true` | Disable only causes per-island layout, not overlap prevention |
| Texel density inconsistent across meshes | `useWorldSpaceScales` | Set `true` | Enables world-space scaling |
| Textures appear too small or too large | `scaleFactor` | Adjust | Increase to zoom in (denser), decrease to zoom out |
| Mesh skipped with no output | `overwriteExisting` | Set `true` | Or mesh has time samples / is an instance proxy |
| No change between runs | Check `paths` | Verify | Empty paths processes all meshes; explicit paths must match |

## Starting Configs

**Default first pass** (good starting point for most meshes):
```json
[{"operation": "generateAtlasUVs", "distortionThreshold": 3.0, "enableAtlasPacking": true, "useWorldSpaceScales": true, "scaleFactor": 0.01, "overwriteExisting": true}]
```

**Low distortion / many islands** (high-quality texturing, seams less visible in practice):
```json
[{"operation": "generateAtlasUVs", "distortionThreshold": 1.5, "enableAtlasPacking": true, "useWorldSpaceScales": true, "scaleFactor": 0.01, "overwriteExisting": true}]
```

**Fewer seams / more distortion** (cleaner UV layout, accept some stretch):
```json
[{"operation": "generateAtlasUVs", "distortionThreshold": 8.0, "enableAtlasPacking": true, "useWorldSpaceScales": true, "scaleFactor": 0.01, "overwriteExisting": true}]
```

**No atlas packing** (each island independently oriented, useful for diagnosis or custom packing):
```json
[{"operation": "generateAtlasUVs", "distortionThreshold": 3.0, "enableAtlasPacking": false, "useWorldSpaceScales": false, "scaleFactor": 0.01, "overwriteExisting": true}]
```

## Known Limitations

- **Time-varying meshes are skipped** — meshes with authored time samples on topology attributes are excluded to avoid crashes.
- **Instance proxies are skipped** — meshes under instance proxies are not processed.
- **UVs written to `st` primvar** — output is face-varying interpolation on the `st` primvar; existing `st` data is overwritten when `overwriteExisting=true`.
- **`distortionThreshold` minimum is 1.05** — values ≤ 1 are clamped regardless of input.
- **Meshes must have authored topology** — prims missing `points`, `faceVertexCounts`, or `faceVertexIndices` attributes are silently skipped.