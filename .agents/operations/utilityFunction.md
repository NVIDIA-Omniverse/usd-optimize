<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Utility Function

**Key:** `utilityFunction`
**Source:** `source/operations/utilityFunction/UtilityFunction.cpp`

## Overview

Utility Function is a container for small one-off operations that don't merit their own plugin. It runs exactly one of four sub-functions — selected by the `function` argument — against the prims in `primPaths`:

- **Deinstance** — flips `instanceable=true` prims back to `instanceable=false`, materializing the instance hierarchy.
- **Unbind Materials** — removes direct and collection material bindings from each prim (does not delete the bound material prims themselves).
- **Set Instanceable** — flips eligible referenced/payloaded prims to `instanceable=true`. Skips prims whose children carry opinions from anywhere other than the reference (those prims have been modified and can't safely be instanced).
- **Flatten Instances** — for prims that are USD instances, drops the composition arc to the prototype so the instance becomes a regular standalone hierarchy.

The operation runs one function per execution; chain multiple `utilityFunction` entries in a config to run more than one.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `primPaths` | string[] | `[]` | Prim paths to act on. Supports the standard SDF-path expressions used elsewhere in Scene Optimizer. |
| `function` | enum | `Deinstance` | Which sub-function to run: `Deinstance`, `Unbind Materials`, `Set Instanceable`, or `Flatten Instances`. |

## Tuning Order

_Not applicable — no numeric tuning. Pick `function` and `primPaths`._

## Visual Diagnosis

_Not applicable — no rendered output to diagnose. Effects are structural (instancing state, material bindings, composition)._

## Starting Configs

**Deinstance everything** (turn off instancing on every prim under a root):
```json
[{"operation": "utilityFunction", "primPaths": ["/World"], "function": "Deinstance"}]
```

**Unbind materials from a subtree** (strip material bindings without deleting the materials):
```json
[{"operation": "utilityFunction", "primPaths": ["/World/Props"], "function": "Unbind Materials"}]
```

**Make eligible referenced prims instanceable** (typical pre-merge optimization):
```json
[{"operation": "utilityFunction", "primPaths": ["/World"], "function": "Set Instanceable"}]
```

**Flatten USD instances back to standalone hierarchies**:
```json
[{"operation": "utilityFunction", "primPaths": ["/World/Instances"], "function": "Flatten Instances"}]
```

## Prerequisites & Workflows

- Standalone — works on any USD stage.
- Common pre/post steps:
  - Run `Deinstance` or `Flatten Instances` before `merge` if the merge logic should see the materialized prims rather than instance proxies.
  - Run `Set Instanceable` after referencing-heavy authoring to recover instance memory savings.
  - Run `Unbind Materials` before material-replacement operations.

## Known Limitations

- **One function per call** — to run several sub-functions, chain multiple `utilityFunction` entries; the operation deliberately doesn't batch them.
- **Deinstance** materializes prototypes as concrete hierarchies, which can substantially increase stage size and memory.
- **Set Instanceable** only succeeds when the prim has a direct reference or payload arc and none of its immediate children carry opinions from elsewhere; modified prims are skipped silently.
- **Flatten Instances** only acts on prims where `IsInstance()` is true; non-instances are no-ops.
- **Unbind Materials** removes the binding relationship but does not delete the material prim — the material remains in the stage and may be re-bound later.

For full Omniverse documentation see: https://docs.omniverse.nvidia.com/extensions/latest/ext_scene-optimizer/operations.html#utility-functions