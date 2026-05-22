<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# RTX Mesh Count

**Key:** `rtxMeshCount`
**Source:** `source/operations/rtxMeshCount/RtxMeshCount.cpp`

## Overview

RTX Mesh Count is a hidden analysis operation that counts the number of RTX acceleration structures, RTX meshes, and unique RTX meshes in the scene. This is useful for understanding how a scene maps to GPU ray-tracing structures.

The analysis helps identify scenes where instancing could reduce the number of unique meshes, or where mesh merging could reduce the number of acceleration structures.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to analyze. Empty = entire stage. |

## Tuning Order

_Not applicable — single parameter; nothing to order._

## Visual Diagnosis

_Not applicable — analysis-only; no rendered output to diagnose._

## Starting Configs

_Not applicable — hidden internal operation; invoked programmatically by the caller. Pass `paths` to scope the analysis._

## Prerequisites & Workflows

- Hidden operation (`getVisible()` returns false).
- Analysis-only — supports analysis mode.
- Use to assess instancing and merge opportunities for RTX rendering.

## Known Limitations

- Hidden operation — not shown in the UI.
- Analysis-only — does not modify the stage.
- Returns JSON with `rtxAccelStructCount`, `rtxMeshCount`, `rtxUniqueMeshCount`, and `rtxMeshPrims`.