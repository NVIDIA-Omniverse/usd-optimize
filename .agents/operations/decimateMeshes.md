<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Decimate Meshes

**Key:** `decimateMeshes`
**Source:** `source/operations/decimateMeshes/OmniMeshDecimate.cpp`

## Overview

Decimate Meshes reduces polygon count while preserving mesh shape as much as possible. It uses QEM-based (Quadric Error Metrics) edge-collapse simplification: edges are iteratively collapsed in order of least geometric error, and the mesh is locally re-triangulated after each collapse. Both CPU (parallel and sequential) and GPU paths are available; the GPU path is selected automatically for meshes above the vertex count threshold.

**`reductionFactor`** and **`maxMeanError`** are the two stop conditions. `reductionFactor` specifies the target percentage of vertices to retain (e.g., 50 = keep 50%). `maxMeanError` stops decimation when the average error exceeds the threshold. Either can be used independently or together — set `reductionFactor` to `0.0` or `maxMeanError` to `0.0` to disable one. Use float literals for these float parameters; some bindings reject integer `0`. When both are enabled, whichever is satisfied first stops decimation of that mesh.

**`guideDecimation`** enables importance-based decimation where a vertex color or corner normal attribute guides which regions are simplified more aggressively. **`pinBoundaries`** prevents boundary edges from collapsing, preserving mesh outlines. **`allowCutAndGlue`** permits the decimator to cut and re-glue the mesh for better quality at aggressive reduction levels.

**Key insight: prefer `maxMeanError` for silhouette-preserving decimation.** It bounds the geometric error (in world units), so the decimator stops before visible features are lost. Reach for `reductionFactor` only when the explicit goal is hitting a specific target reduction rate (e.g., "drop 25% to fit a memory budget"). Note that `reductionFactor` is a percentage in 0–100, not a fraction — `0.5` means "keep 0.5%", which destroys the mesh.

## Quality scale

`maxMeanError` is the maximum mean geometric distance the decimated surface is
allowed to drift from the original, expressed in **stage units**. Ask the user
for a physical tolerance in millimeters, then convert to stage units using the
stage's `metersPerUnit`:

```
maxMeanError = (mm_input / 1000) / metersPerUnit
```

Read `metersPerUnit` from the stage:

```python
from pxr import UsdGeom
mpu = UsdGeom.GetStageMetersPerUnit(stage)
```

`GetStageMetersPerUnit` returns USD's default (`0.01` = centimeters) when the
metadata is not set. If a factory/CAD/BIM stage has no authored
`metersPerUnit`, tell the user you are assuming USD's centimeter default and
ask them to confirm or correct the scale before writing a tolerance-sensitive
decimation config.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `reductionFactor` | float | `50.0` | Target percentage of original vertex count to retain (0–100). 50 = keep 50%. Set to `0.0` if using maxMeanError. |
| `maxMeanError` | float | `0.0` | Maximum mean error for decimation (stage units). `0.0` = disabled. |
| `guideDecimation` | enum | `By normals` (0) | Guide decimation: `By normals` (0), `By colors` (1), `Off` (2). |
| `pinBoundaries` | bool | `false` | Preserve mesh boundary edges during decimation. |
| `allowCutAndGlue` | bool | `false` | Allow topology changes (cut and re-glue) for better quality at aggressive reduction. |
| `cpuVertexCountThreshold` | int | `100000` | Use CPU parallel algorithm above this vertex count. |
| `gpuVertexCountThreshold` | int | `500000` | Use GPU algorithm above this vertex count. |

## Tuning Order

1. **`maxMeanError` first** — Set a non-zero value in stage units and disable the fraction cap with `reductionFactor: 0.0`. This is the silhouette-preserving default. Increase to allow more reduction; decrease for tighter quality. Convert from user-facing millimeters via `metersPerUnit`.
2. **`pinBoundaries` second** — Enable for meshes where edge outlines matter (e.g., architectural walls, 3D-mesh tiles that must align along boundaries).
3. **`reductionFactor` third — only when the goal is a target reduction rate.** Set in 0–100 (50 keeps half the vertices). Pair with `maxMeanError: 0.0` to disable the error cap. Values below 10 typically destroy the silhouette — see Known Limitations.
4. **`guideDecimation` fourth** — Enable when certain regions should be preserved with higher detail (vertex-color or normal-driven).
5. **`allowCutAndGlue` last** — Enable for aggressive decimation where topology changes are acceptable.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Important features lost / silhouette degraded | `maxMeanError` | Decrease | Tighten the error budget. If you'd been driving with `reductionFactor`, switch to `maxMeanError` (set `reductionFactor: 0.0`) — the error budget gives direct control over visible shape change. |
| Boundary edges collapsing | `pinBoundaries` | Enable | Preserves mesh outlines (e.g., architectural walls, 3D-mesh tiles that must align exactly along edges). |
| Mesh still too dense | `maxMeanError` | Increase | Allows more geometric error per mesh. Bump in small steps and re-check silhouette. |
| Need a specific reduction percentage | `reductionFactor` | Set in 0–100 | Only when the goal is hitting a target rate (memory budget, LOD level). Pair with `maxMeanError: 0.0`. |
| Slow on large meshes | `gpuVertexCountThreshold` | Decrease | Use GPU earlier. |

## Starting Configs

**Silhouette-preserving (default — start here)** — error-budget driven, scene-scale dependent. Convert the user's millimeter tolerance to stage units first:
```json
[{"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": true}]
```

**Conservative with boundary preservation** — same as above, smaller error budget:
```json
[{"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.001, "pinBoundaries": true}]
```

**Target reduction rate** — use only when the goal is hitting a specific percentage. `reductionFactor` is in 0–100 (50 = keep half). Pair with `maxMeanError: 0.0` to disable the error cap:
```json
[{"operation": "decimateMeshes", "reductionFactor": 50.0, "maxMeanError": 0.0, "pinBoundaries": true}]
```

**Aggressive LOD generation** — cuts geometry hard; expect visible silhouette change. Use only for LODs where the asset will be drawn at small screen size:
```json
[{"operation": "decimateMeshes", "reductionFactor": 10.0, "maxMeanError": 0.0, "pinBoundaries": true, "allowCutAndGlue": true}]
```

## Anti-pattern: omitting `pinBoundaries`

**`pinBoundaries: true` must appear literally in every `decimateMeshes` JSON
config that is intended to preserve silhouettes.** The SO parameter default is
`false`; omitting it from JSON means the decimator can collapse boundary edges.
This is easy to miss because safe examples usually discuss boundary
preservation in prose. When building configs, include the field explicitly.

```json
{"operation": "decimateMeshes",
 "paths": [],
 "reductionFactor": 0.0,
 "maxMeanError": 0.01,
 "pinBoundaries": true}
```

## Targeting and edit scope

Decimation modifies the actual vertex data of `UsdGeom.Mesh` prims. Before
running it on stages with references, payloads, scenegraph instances, or
`UsdSkel` bindings, surface the target-set choice to the user instead of
silently processing everything:

- Composed-in meshes usually write overrides on the assembly stage while the
  source asset remains high-poly. That can be useful for stage-local
  visualization, but source-level optimization or proxy variants are often the
  better publishing path.
- Skinned meshes have joint indices and weights tied to vertex order.
  Decimation changes the vertex list and can invalidate those bindings unless
  the user plans to regenerate skin weights.
- Scenegraph-instanced prims and prototype contents should be excluded unless
  the workflow intentionally edits shared instance data.

After the user chooses a target set, inject that path list into `paths` for
mesh-targeted ops in the chain (`meshCleanup`, `decimateMeshes`,
`generateNormals`, `removeSmallGeometry`, `computeExtents`, `fitPrimitives`).

## Prerequisites & Workflows

- Works standalone on any mesh.
- Often used for LOD generation or reducing meshes imported at excessive resolution.
- Common pipeline: `meshCleanup` → `decimateMeshes`.

## Known Limitations

- GPU acceleration requires CUDA and is used automatically for meshes above `gpuVertexCountThreshold` (default 500K vertices). A CPU parallel algorithm is used for meshes above `cpuVertexCountThreshold` (default 100K).
- `maxMeanError` values are in world units and depend on scene scale.
- Very aggressive reduction (factor < 10) will likely produce results that no longer resemble the original mesh.
