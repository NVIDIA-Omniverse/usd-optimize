<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Generate Projection UVs

**Key:** `generateProjectionUVs`
**Source:** `source/operations/generateProjectionUVs/GenerateProjectionUVs.cpp`

## Overview

Generate Projection UVs creates texture coordinates by projecting them onto meshes using one of several projection methods (planar, cylindrical, spherical, cubic, or triplanar). This is useful for meshes that lack UV coordinates or need new UVs for a different texturing workflow.

**`projectionType`** selects the projection method. **`scaleFactor`** controls the UV scale — larger values produce more texture repetitions. **`useWorldSpaceScales`** determines whether the scale is applied in world space (consistent across all meshes) or object space (relative to each mesh).

**Key insight:** choose the projection type based on the dominant shape of your meshes. Triplanar works well for architectural geometry; spherical for organic shapes.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `preprojectionXform` | matrix | identity | Transform applied before projection. Hidden. |
| `projectionType` | enum | `Cube` (4) | Projection method: `Planar` (0), `Spherical` (1), `Cylindrical` (2), `Triplanar` (3), `Cube` (4). |
| `useWorldSpaceScales` | bool | `true` | Scale UVs in world space for consistent texel density. |
| `scaleFactor` | float | `1.0` | UV scale factor. Larger = more texture repetitions. |
| `scaleUnits` | enum | `Meters` | Units for the scale factor: `Meters`, `Centimeters`, etc. |
| `overwriteExisting` | bool | `true` | Overwrite existing UV coordinates. |

## Tuning Order

1. **`projectionType` first** — Default is `Cube` (4). Choose based on mesh shape: `Triplanar` (3) for boxes/walls, `Cylindrical` (2) for pipes, `Spherical` (1) for organic shapes.
2. **`scaleFactor` second** — Adjust for desired texel density. Start at 1.0 and increase/decrease based on texture resolution.
3. **`useWorldSpaceScales` third** — Enable for consistent texturing across meshes of different sizes.
4. **`overwriteExisting` last** — Enable only if you want to replace existing UVs.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Texture too stretched or compressed | `scaleFactor` | Adjust | Match to texture resolution |
| Texture scale varies between meshes | `useWorldSpaceScales` | Enable | Ensures consistent texel density |
| UVs unchanged on some meshes | `overwriteExisting` | Enable | Existing UVs are preserved by default |
| Projection seams visible | `projectionType` | Change | Try Triplanar to minimize seams |

## Starting Configs

**Triplanar for architecture**:
```json
[{"operation": "generateProjectionUVs", "projectionType": 3, "scaleFactor": 1.0, "useWorldSpaceScales": true}]
```

**Cylindrical for pipes**:
```json
[{"operation": "generateProjectionUVs", "projectionType": 2, "scaleFactor": 1.0}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Often used after `merge` when merged meshes need new UV coordinates.
- Common pipeline: `merge` → `generateProjectionUVs` → texture assignment.

## Known Limitations

- Projection seams are inherent to the projection method — no projection type is seam-free for all shapes.
- The `preprojectionXform` parameter is hidden and intended for programmatic use.