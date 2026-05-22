<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Remove Prims

**Key:** `removePrims`
**Source:** `source/operations/removePrims/RemovePrimsPlugin.cpp`

## Overview

Remove Prims identifies and removes invisible prims and orphaned overs from a USD stage. This cleans up scenes by eliminating prims that don't contribute to the visual result.

The operation handles two categories independently:

1. **Invisible prims** — prims with `visibility = invisible` that are not visible in the rendered scene. These can be deleted or deactivated.
2. **Orphaned overs** — over opinions that don't correspond to any defined prim (often left over from deleted references or moved prims). These can be deleted, deactivated, or hidden.

**Key insight:** orphaned overs are a common source of scene bloat in production workflows. Removing them reduces file size and load times.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to process. Empty = all prims. |
| `removeInvisible` | bool | `true` | Find and remove invisible prims. |
| `invisibleRemoveMethod` | enum | `Deactivate` (1) | How to remove invisible prims: `Delete` (0) or `Deactivate` (1). |
| `removeOrphanedOvers` | bool | `true` | Find and remove orphaned over opinions. |
| `orphanedOverRemoveMethod` | enum | `Delete` (0) | How to remove orphaned overs: `Delete` (0), `Deactivate` (1), or `Hide` (2). |
| `explicitMode` | bool | `false` | Use explicit prim lists instead of auto-detection. Hidden. |
| `explicitInvisiblePaths` | string[] | `[]` | Explicit list of invisible prim paths. Hidden. |
| `explicitOrphanedPaths` | string[] | `[]` | Explicit list of orphaned over paths. Hidden. |

## Tuning Order

1. **`removeInvisible` / `removeOrphanedOvers` first** — Enable the categories you want to clean.
2. **`invisibleRemoveMethod` second** — `Delete` removes from the layer entirely; `Deactivate` preserves the prim but marks it inactive (safer, reversible).
3. **`orphanedOverRemoveMethod` third** — `Delete` is the most thorough cleanup; `Deactivate` or `Hide` are safer options.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Hidden objects still consuming memory | `removeInvisible` | Enable | Invisible prims still load data |
| Stage file larger than expected | `removeOrphanedOvers` | Enable | Orphaned overs add to file size |
| Need to restore removed prims | Remove methods | Use Deactivate | Reversible compared to Delete |

## Starting Configs

**Standard cleanup** (delete both):
```json
[{"operation": "removePrims", "removeInvisible": true, "invisibleRemoveMethod": 0, "removeOrphanedOvers": true, "orphanedOverRemoveMethod": 0}]
```

**Safe cleanup** (deactivate instead of delete):
```json
[{"operation": "removePrims", "removeInvisible": true, "invisibleRemoveMethod": 1, "removeOrphanedOvers": true, "orphanedOverRemoveMethod": 1}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage.
- Supports analysis mode for Asset Validator integration.
- Common pipeline: `removePrims` → `pruneLeaves`. Removing prims often orphans their parent Xform/Scope groups, leaving empty hierarchy nodes. `pruneLeaves` cleans these up.

## Known Limitations

- Composition arcs are respected — prims defined in referenced or payloaded layers cannot be removed from the current edit target, so they are deactivated instead. For example, if you reference `building.usd` and try to delete `/Building/Roof`, the roof prim will be set inactive (hidden) rather than removed, since the definition lives in the referenced layer.
- The `explicitMode` parameters are intended for programmatic use and are hidden from the UI.