<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Find Overlapping Meshes

**Key:** `findOverlappingMeshes`
**Source:** `source/operations/findOverlappingMeshes/FindOverlappingMeshesOperation.cpp`

## Overview

Find Overlapping Meshes detects interfering geometry — meshes whose surfaces intersect or penetrate each other. This is distinct from `findCoincidingGeometry` (which finds identical geometry in the same space) and `findOccludedMeshes` (which finds fully hidden geometry).

Results can be reported as individual overlap pairs or grouped into islands of connected overlaps. **`reportIslands`** controls grouping: when enabled, overlapping meshes are clustered into connected components using a union-find algorithm. **`fullStageReport`** controls whether individual overlaps are listed when processing the full stage (can produce very large output).

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `reportIslands` | bool | `false` | Group overlapping meshes into connected islands. |
| `fullStageReport` | bool | `false` | Report individual overlaps even when processing the full stage. |
| `useGpu` | bool | `true` | Use GPU for overlap detection. Falls back to CPU (with a warning) when CUDA is unavailable. |

## Tuning Order

1. **`paths` first** — Restrict to a subset for faster results; empty processes the full stage.
2. **`reportIslands` second** — Enable to see connected groups rather than individual pairs.
3. **`fullStageReport` third** — Enable to see all overlaps when processing the full stage (may be large).
4. **`useGpu` fourth** — Enable for GPU acceleration if CUDA is available.

## Visual Diagnosis

_Not applicable — analysis-only; results are reported as JSON pairs or islands. Visually verify by selecting reported overlaps in the viewport; tune via `paths` and `reportIslands` to refine output._

## Starting Configs

**Quick check on specific prims**:
```json
[{"operation": "findOverlappingMeshes", "paths": ["/World/Building"], "reportIslands": true}]
```

**Full stage analysis**:
```json
[{"operation": "findOverlappingMeshes", "fullStageReport": true, "reportIslands": true}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Supports analysis mode.
- Use results to guide manual cleanup or `deduplicateGeometry`.

## Known Limitations

- Full-stage analysis without `paths` suppresses individual overlap reporting by default (use `fullStageReport` to override).
- GPU mode requires CUDA; falls back to CPU when unavailable (logs `"GPU requested but CUDA is not available. Falling back to CPU."` — `FindOverlappingMeshesOperation.cpp:398`).
- Supports analysis mode for reporting overlap counts.
- O(n²)-ish on mesh count, even with the BVH-style spatial pruning the lib does. The mesh count, not the per-mesh face count, is what blows up on industrial assemblies (see Performance below).

## Performance — Serial CPU vs GPU

Measured on an Intel Xeon Icelake (16 vCPU) + NVIDIA L40.

> **All "CPU" timings below are serial / single-threaded.** The upstream
> `MeshTools::ClashDetectorParameters::useParallelCpuClashDetection` flag
> defaults to `false`, so the lib's CPU path is single-threaded out of the
> box. The numbers in the table are CPU-with-one-core, not 16-core CPU.

| Stage size | Mode | Wall | Notes |
|---|---|---|---|
| 8 MB, 525 meshes | Serial CPU | 917.67 s | `useGpu=false`, `useParallelCpu=false` (defaults) |
| 8 MB, 525 meshes | GPU | 1.51 s | ~600× over serial CPU |
| 438 MB, 175k meshes | GPU | killed at 53 m | Still inside `validate()`; default `maximumNumberOfVerticesPerTile=0` means no tiling, may be paging through VRAM |

### Takeaways

- For small/medium stages (single-digit thousands of meshes), GPU is the right default and ~600× faster than the lib's serial CPU path. (Multi-threaded CPU would be faster than serial CPU but is opt-in via `useParallelCpu=true`.)
- For industrial assemblies in the 100k+ mesh range, even GPU doesn't finish in a usable time at full-stage scope. Reach for **`paths=[...]` to scope to subassemblies**, or set `maximumNumberOfVerticesPerTile` to enable GPU tiling. The bottleneck is algorithmic (BVH worst-case is still O(n²) on dense overlap candidates), not constant factors.