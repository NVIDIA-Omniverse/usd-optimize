<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Box Clip

**Key:** `boxClip`
**Source:** `source/operations/boxClip/BoxClip.cpp`

## Overview

Box Clip removes or retains geometry based on an axis-aligned bounding box (AABB) region. Think of it as a 3D cookie cutter — everything inside or outside the box is kept or removed based on the clip mode.

The clipping region can be defined in two ways: by explicit min/max coordinates (`ByAABB`) or by referencing an existing prim's bounding box (`ByPrim`). **`clipMode`** controls whether geometry inside the box is kept or removed.

**`clipMode`** is the primary behavior control. `InsideCutMesh` precisely cuts faces along the box boundary planes using double-precision arithmetic for stability. `InsideKeep` retains partially intersecting faces whole, and `InsideDiscard` removes them entirely. The `Outside*` variants invert the region.

**Key insight:** define the clip box precisely first using `ByPrim` (simpler) or exact coordinates. Then choose a `clipMode` for clean edges.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `clipBoxDef` | enum | `Prim` (ByPrim = 1) | How to define the clip box: `Corners of Box` (ByAABB = 0) = explicit coordinates, `Prim` (ByPrim = 1) = from a prim's bounding box. |
| `minX` | float | `0.0` | Minimum X of clip box. Visible when clipBoxDef = ByAABB (0). |
| `minY` | float | `0.0` | Minimum Y of clip box. Visible when clipBoxDef = ByAABB (0). |
| `minZ` | float | `0.0` | Minimum Z of clip box. Visible when clipBoxDef = ByAABB (0). |
| `maxX` | float | `0.0` | Maximum X of clip box. Visible when clipBoxDef = ByAABB (0). |
| `maxY` | float | `0.0` | Maximum Y of clip box. Visible when clipBoxDef = ByAABB (0). |
| `maxZ` | float | `0.0` | Maximum Z of clip box. Visible when clipBoxDef = ByAABB (0). |
| `clipBoxPrimPath` | string | `""` | Prim path whose bounding box defines the clip region. Visible when clipBoxDef = ByPrim (1). |
| `ignoreClipBoxSide` | enum | `None` (0) | Side to ignore: `None` (0), `-X` (1), `+X` (2), `-Y` (3), `+Y` (4), `-Z` (5), `+Z` (6). |
| `clipMode` | enum | `InsideKeep` (0) | Combined clip mode: `InsideKeep` (0), `InsideCutMesh` (1), `InsideDiscard` (2), `OutsideKeep` (3), `OutsideDiscard` (4). There is no `OutsideCutMesh` mode — Outside+CutMesh is unsupported because the cut-mesh algorithm only operates on the inside region. At runtime, if `keepGeometry == eOutside` and `partiallyIntersectedPrims == eKeepIntersection`, the code resets `partiallyIntersectedPrims` to `eKeep` (logging via `SO_LOG_VERBOSE`), so it silently falls back to `OutsideKeep` (3). |

### Legacy Parameters

These are hidden from the UI and superseded by `clipMode`. Do not set them when using `clipMode`.

| Parameter | Type | Default | Description |
|---|---|---|---|
| `keepGeometry` | enum | `Inside` (0) | What geometry to keep: `Inside` (0) or `Outside` (1). |
| `partialIntersections` | enum | `Keep` (0) | Partial intersection handling: `Keep` (0), `Cut Mesh` (1), `Discard` (2). |

## Tuning Order

1. **`clipBoxDef` first** — Choose definition method. `ByPrim` is simpler if you have a reference prim.
2. **`minX/Y/Z` / `maxX/Y/Z` or `clipBoxPrimPath` second** — Define the clip region.
3. **`clipMode` third** — Choose the combined behavior preset. `InsideCutMesh` gives the cleanest results; `InsideKeep` is fastest; `OutsideKeep`/`OutsideDiscard` invert the region.
4. **`ignoreClipBoxSide` fourth** — Optionally extend the clip box to infinity on one side.

> **Note:** `clipMode` implicitly controls `keepGeometry` and `partialIntersections` (see Legacy Parameters below).

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Wrong geometry removed | `clipMode` | Switch Inside↔Outside | Use `OutsideKeep` (3) or `OutsideDiscard` (4) variants |
| Jagged edges at clip boundary | `clipMode` | Use `InsideCutMesh` (1) | Cuts faces for clean edges |
| Clip region wrong size | `min/maxX/Y/Z` | Adjust | Verify coordinates match scene units |
| Entire meshes disappearing | `clipMode` | Check | `InsideDiscard`/`OutsideDiscard` remove partial meshes entirely |

## Starting Configs

**Clip inside a region** (ByAABB + InsideCutMesh):
```json
[{"operation": "boxClip", "clipBoxDef": 0, "minX": -100.0, "minY": -100.0, "minZ": -100.0, "maxX": 100.0, "maxY": 100.0, "maxZ": 100.0, "clipMode": 1}]
```

**Clip by prim bounds** (ByPrim + InsideKeep):
```json
[{"operation": "boxClip", "clipBoxDef": 1, "clipBoxPrimPath": "/World/ClipRegion", "clipMode": 0}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Useful for cropping large environments to a region of interest.
- Common pipeline: `boxClip` → `meshCleanup` (to clean up cut edges).

## Known Limitations

- Only supports axis-aligned bounding boxes — no rotated or arbitrary clip volumes.
- Coordinates are in world units and must match the scene's unit scale.