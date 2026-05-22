<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Count Vertices

**Key:** `countVertices`
**Source:** `source/operations/countVertices/CountVertices.cpp`

## Overview

Count Vertices is a hidden analysis utility that categorizes meshes by vertex count into high, very high, and extreme buckets. This helps identify meshes that may benefit from decimation or LOD generation.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `high` | int | (default) | Vertex count threshold for "high" category. |
| `veryHigh` | int | (default) | Vertex count threshold for "very high" category. |
| `extreme` | int | (default) | Vertex count threshold for "extreme" category. |

## Tuning Order

_Not applicable — analysis-only thresholds; no interaction order beyond setting each bucket boundary independently._

## Visual Diagnosis

_Not applicable — analysis-only; no rendered output to diagnose._

## Starting Configs

_Not applicable — hidden internal operation; thresholds are set programmatically by the caller._

## Prerequisites & Workflows

- Hidden operation (`getVisible()` returns false).
- Used internally for scene analysis and reporting.

## Known Limitations

- Hidden operation — not shown in the UI.
- Analysis-only utility.