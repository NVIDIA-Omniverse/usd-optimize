<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Remove Unused UVs

**Key:** `removeUnusedUVs`
**Source:** `source/operations/removeUnusedUVs/RemoveUnusedUVs.cpp`

## Overview

Remove Unused UVs finds and removes texture coordinate (UV) attributes that are not referenced by any bound material. This reduces memory and file size by eliminating UV data that serves no purpose.

The operation checks standard UV names (`primvars:st`, `primvars:st0`, `primvars:st1`, `primvars:st2`) plus any custom names provided. For each mesh, it resolves the bound material and checks whether it contains a `UsdUVTexture` shader or asset-typed inputs that would require UVs. If no material usage is found, the UV attribute is removed.

**`mode`** controls whether unused UVs are removed (deleted) or blocked (prevents inheritance).

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to process. Empty = all prims. |
| `mode` | enum | `Remove` (0) | Action: `Remove` (0) = delete, `Block` (1) = author block opinion. |
| `attributes` | string[] | `[]` | Additional UV attribute names to check beyond the defaults. |

## Tuning Order

1. **`mode` first** — `Remove` for permanent cleanup. `Block` for non-destructive override.
2. **`attributes` second** — Add custom UV names if your pipeline uses non-standard naming.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Texture mapping breaks after run | `mode` | Switch to `Block` (1) | If a UV set was conservatively required by a material, switching to `Block` lets you reverse later. Verify the material binding resolves via `UsdShadeMaterialBindingAPI`. |
| Custom UVs not removed despite being unused | `attributes` | Add the UV name | The operation only checks the standard `primvars:st`/`st0`/`st1`/`st2` plus what's in `attributes`. |

## Starting Configs

**Standard cleanup**:
```json
[{"operation": "removeUnusedUVs"}]
```

**With custom UV names**:
```json
[{"operation": "removeUnusedUVs", "attributes": ["primvars:myCustomUV"]}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage with materials and meshes.
- Supports analysis mode for Asset Validator integration.
- Common pipeline: `optimizeMaterials` → `removeUnusedUVs`.
- For CAD/BIM scenes, run this early in cleanup pipelines before primitive
  fitting. BIM exports often carry empty or unbound UV sets on many meshes;
  removing them before `fitPrimitives` and `meshCleanup` can be a major file
  size win and avoids carrying useless primvars through later mesh passes.

## Known Limitations

- Heuristic-based: if a material has asset-typed inputs, UVs are conservatively kept even if the material doesn't actually use them.
- Only checks standard UV names plus user-provided additions.
- Materials must be resolvable via `UsdShadeMaterialBindingAPI`.
