<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Fit Primitives

**Key:** `fitPrimitives`
**Source:** `source/operations/fitPrimitives/Primitive.cpp`

## Overview

Fit Primitives analyzes meshes and replaces them with simpler geometric primitives (spheres, cylinders, cones, cubes) when the mesh closely matches one of those shapes. This dramatically reduces polygon count for mechanical/CAD scenes where many objects are recognizable primitives.

The operation tests each mesh against enabled primitive types and checks whether the fit meets tolerance thresholds. **`vertexTolerance`** controls the RMS relative distance from vertices to the fitted shape, while **`volumeTolerance`** checks that the relative volume difference is acceptable.

**`gpuFaceCountThreshold`** determines when to offload fitting to the GPU for large meshes. **`generateMeshes`** controls whether fitted primitives are output as USD primitives (e.g., UsdGeomSphere) or as generated polygon meshes with configurable subdivision.  If meshes are output, a prototype mesh is created for each primitive type (sphere, cylinder, cone, and cube) with the given subdivision parameters.  The meshes that were fit are replaced by a UsdPrim which instance the appropriate prototype, using a transform to perform the fit.

**Source-verified semantics for `ignoreNonConstPrimvars` and
`ignoreSubsets`:** the names are easy to misread. Both default to `true`, and
at the default the operation **allows fitting despite non-constant primvars
and geometry subsets**; if a mesh is replaced, those primvars / subsets are
discarded. Setting either to `false` is the **restrictive** setting that
**skips** affected meshes to preserve their data. Normal primvars are
allow-listed in the source and never block fitting at any setting.

For CAD/BIM/MEP scenes, the default args work for aggressive primitive
replacement on meshes whose only non-constant primvar is Normal. If analysis
reports a high `nonconstPrimvarMeshCount`, those meshes carry additional
non-Normal primvars (UVs, display colors, subsets) that would be discarded on
replacement; set `ignoreNonConstPrimvars: false` and/or `ignoreSubsets: false`
only when preserving that data matters more than primitive count reduction.

**Key insight:** start with all primitive types enabled and generous tolerances, then tighten tolerances to reduce false positives.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `gpuFaceCountThreshold` | int | `0` | Face count above which GPU is used for fitting. |
| `showFittingParameters` | bool | `true` | Reveal advanced fitting tolerance parameters. |
| `vertexTolerance` | float | `0.01` | Max allowed distance from vertices to fitted primitive surface. Hidden behind showFittingParameters. |
| `volumeTolerance` | float | `0.01` | Max allowed relative volume difference between mesh and fitted primitive. Hidden behind showFittingParameters. |
| `ignoreNonConstPrimvars` | bool | `true` | When `true` (default), allow fitting meshes that have non-constant non-Normal primvars; if replaced, those primvars are discarded. Set `false` to preserve such data by skipping those meshes. Normal primvars are always allow-listed and never block fitting. |
| `ignoreSubsets` | bool | `true` | When `true` (default), allow fitting meshes that have geometry subsets (multi-material); if replaced, the subsets are discarded. Set `false` to preserve subsets by skipping those meshes. |
| `allowNegativeVolume` | bool | `true` | Allow fitting to meshes with inverted normals. |
| `allowMissingEndcaps` | bool | `true` | Allow fitting cylinders/cones that lack end caps. |
| `fitSphere` | bool | `true` | Enable sphere fitting. |
| `fitCylinder` | bool | `true` | Enable cylinder fitting. |
| `fitCone` | bool | `true` | Enable cone fitting. |
| `fitCube` | bool | `true` | Enable cube fitting. |
| `generateMeshes` | bool | `false` | Output polygon meshes instead of USD primitives. |
| `sphereLongitudeDivisions` | int | `16` | Longitude divisions for generated sphere meshes. Visible when generateMeshes=true. |
| `sphereLatitudeDivisions` | int | `8` | Latitude divisions for generated sphere meshes. Visible when generateMeshes=true. |
| `cylinderWallDivisions` | int | `16` | Wall divisions for generated cylinder meshes. Visible when generateMeshes=true. |
| `cylinderLatitudeDivisions` | int | `1` | Latitude divisions for generated cylinder meshes. Visible when generateMeshes=true. |
| `coneSideDivisions` | int | `16` | Side divisions for generated cone meshes. Visible when generateMeshes=true. |
| `coneLengthDivisions` | int | `1` | Length divisions for generated cone meshes. Visible when generateMeshes=true. |

## Tuning Order

1. **Primitive type toggles (`fitSphere`, `fitCylinder`, `fitCone`, `fitCube`) first** — Disable types not present in the scene to speed up processing.
2. **`vertexTolerance` second** — Start at default 0.01. Increase to accept rougher fits (more replacements). Decrease for stricter matching.
3. **`volumeTolerance` third** — Tighten if fitted primitives look too different in size from originals.
4. **`ignoreNonConstPrimvars` / `ignoreSubsets` fourth** — Defaults are aggressive; set `false` to preserve UVs / display colors / subsets by skipping affected meshes.
5. **`generateMeshes` last** — Enable if downstream tools require polygon meshes rather than USD primitive types.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Too few meshes being replaced | `vertexTolerance` | Increase | Tolerance too strict for mesh resolution |
| Fitted shapes look wrong size | `volumeTolerance` | Decrease | Volume check too lenient |
| Textured objects being replaced (UVs / display colors lost) | `ignoreNonConstPrimvars` | Set to `false` | Skips meshes carrying non-Normal primvars. |
| Open cylinders not detected | `allowMissingEndcaps` | Set to true | Allows fitting without end caps |
| Generated meshes look faceted | Division parameters | Increase | More subdivisions for smoother output |

## Starting Configs

**CAD scene cleanup** (aggressive fitting):
```json
[{"operation": "fitPrimitives", "vertexTolerance": 0.05, "volumeTolerance": 0.15}]
```

**CAD/BIM/MEP pipe-heavy scenes** (common Revit/HVAC/plumbing case):
```json
[{"operation": "fitPrimitives"}]
```

To preserve non-Normal primvars / subsets instead:
```json
[{"operation": "fitPrimitives", "ignoreNonConstPrimvars": false, "ignoreSubsets": false}]
```

**Conservative fitting** (strict tolerances):
```json
[{"operation": "fitPrimitives", "vertexTolerance": 0.01, "volumeTolerance": 0.05}]
```

**Spheres and cylinders only**:
```json
[{"operation": "fitPrimitives", "fitCone": false, "fitCube": false}]
```

## Prerequisites & Workflows

- Works standalone on any mesh scene.
- Most effective on CAD/mechanical scenes with recognizable geometric shapes.
- Common pipeline: `removeUnusedUVs` → `fitPrimitives` → `meshCleanup` (with
  `mergeVertices: true`) → `computeExtents`, then optionally
  `deduplicateGeometry` to find duplicate fitted shapes.

## Known Limitations

- Fitted primitives cannot preserve UV layouts, display colors, or geometry subsets. Defaults favor replacement; set `ignoreNonConstPrimvars: false` and/or `ignoreSubsets: false` when preserving that data matters.
- GPU acceleration requires CUDA availability; falls back to CPU when unavailable.
