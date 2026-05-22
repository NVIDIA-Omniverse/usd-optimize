<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Primitives to Meshes

**Key:** `primitivesToMeshes`
**Source:** `source/operations/primitivesToMeshes/PrimitiveToMesh.cpp`

## Overview

Primitives to Meshes converts USD geometric primitives (UsdGeomSphere, UsdGeomCylinder, UsdGeomCone, UsdGeomCube) into polygon mesh representations. This is the inverse of Fit Primitives and is useful when downstream tools or renderers require polygon meshes.

Each primitive type can be independently enabled/disabled, and the subdivision quality of the generated meshes is configurable per type. Higher subdivision counts produce smoother meshes at the cost of more polygons.

For each primitive type converted, a prototype mesh is created for the given subdivision parameters.  The primitives are converted by replacing them with a UsdPrim which instances the prototype with an appropriate transform to match the geometry.

**Key insight:** match subdivision quality to the visual importance and viewing distance of the objects. Background objects need fewer divisions than hero assets.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to process. Empty = all primitives. |
| `convertSpheres` | bool | `true` | Convert UsdGeomSphere to meshes. |
| `sphereLongitudeDivisions` | int | `32` | Longitude divisions for sphere meshes (min 3). |
| `sphereLatitudeDivisions` | int | `16` | Latitude divisions for sphere meshes (min 2). |
| `convertCylinders` | bool | `true` | Convert UsdGeomCylinder to meshes. |
| `cylinderWallDivisions` | int | `32` | Wall divisions for cylinder meshes (min 3). |
| `cylinderLatitudeDivisions` | int | `1` | Length divisions for cylinder meshes (min 1). |
| `cylinderEndcaps` | bool | `true` | Generate end caps for cylinders. |
| `convertCones` | bool | `true` | Convert UsdGeomCone to meshes. |
| `coneSideDivisions` | int | `64` | Side divisions for cone meshes (min 3). |
| `coneLengthDivisions` | int | `3` | Length divisions for cone meshes (min 1). |
| `coneBases` | bool | `true` | Generate bases for cones. |
| `convertCubes` | bool | `true` | Convert UsdGeomCube to meshes. |

## Tuning Order

1. **Type toggles (`convertSpheres`, etc.) first** — Disable types you don't need to convert.
2. **Division counts second** — Adjust per type based on visual quality needs. Defaults are 32 divisions for spheres/cylinders and 64 for cones; decrease to 16 for a lighter-weight starting point.
3. **End cap / base toggles third** — Disable if the geometry will never be viewed from those angles.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Spheres look faceted | `sphereLongitudeDivisions` / `sphereLatitudeDivisions` | Increase | More divisions for smoother appearance |
| Cylinders look angular | `cylinderWallDivisions` | Increase | More wall divisions for rounder profile |
| Too many polygons generated | Division parameters | Decrease | Reduce for distant/unimportant objects |
| Cylinders have holes at ends | `cylinderEndcaps` | Set to true | Generates cap geometry |

## Starting Configs

**Standard quality**:
```json
[{"operation": "primitivesToMeshes"}]
```

**High quality** (extra smooth):
```json
[{"operation": "primitivesToMeshes", "sphereLongitudeDivisions": 64, "sphereLatitudeDivisions": 32, "cylinderWallDivisions": 64, "coneSideDivisions": 128}]
```

**Low quality** (minimal polygons):
```json
[{"operation": "primitivesToMeshes", "sphereLongitudeDivisions": 12, "sphereLatitudeDivisions": 6, "cylinderWallDivisions": 12, "coneSideDivisions": 16}]
```

## Prerequisites & Workflows

- Works standalone on any scene containing USD geometric primitives.
- Often used after `fitPrimitives` with `generateMeshes=false` when meshes are later needed.
- Common pipeline: scene with native USD primitives → `primitivesToMeshes` → `merge` or other mesh operations.

## Known Limitations

- Only converts UsdGeomSphere, UsdGeomCylinder, UsdGeomCone, and UsdGeomCube.
- Other primitive types (e.g., UsdGeomCapsule) are not handled.