<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Flatten Hierarchy

**Key:** `flattenHierarchy`
**Source:** `source/operations/flattenHierarchy/FlattenHierarchy.cpp`

## Overview

Flatten Hierarchy removes redundant Xform prims from a stage's hierarchy, reducing prim count. The operation identifies Xform prims that serve only as containers for a single child and eliminates the unnecessary nesting by reparenting children up.

The operation carefully preserves visual correctness: transforms are compensated so meshes remain in the same world-space position, visibility is maintained, and material bindings are updated to point to the correct locations after reparenting.

**`identity`** controls whether only identity-transform Xforms are removed (safe, no visual change) or all redundant Xforms (compensating transforms applied to children).

**Key insight:** start with `identity=true` for a safe first pass that removes obviously unnecessary groups. Set `identity=false` for more aggressive flattening that may require transform compensation.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to process. Empty = entire stage. |
| `identity` | bool | `false` | Only remove Xforms that have identity (no-op) transforms. |

## Tuning Order

1. **`identity` first** — Start with `true` for safe removal. Set to `false` for aggressive flattening.
2. **`paths` second** — Optionally restrict to specific subtrees.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Geometry visibly shifts after flattening | `identity` | Set to `true` | Identity-only mode skips Xforms with non-trivial transforms; aggressive mode applies compensating transforms which can drift on time-sampled or skew transforms. |
| Hierarchy not reduced as much as expected | `identity` | Set to `false` | Aggressive mode also collapses non-identity Xforms; ensures more prims are removed. |
| Material bindings appear broken | n/a | — | The operation rewires bindings; broken bindings indicate a USD edit conflict — re-run on a fresh authored layer. |

## Starting Configs

**Safe flattening** (identity only):
```json
[{"operation": "flattenHierarchy", "identity": true}]
```

**Aggressive flattening**:
```json
[{"operation": "flattenHierarchy", "identity": false}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage.
- Respects references, relationships, material bindings, and composition arcs.
- Common pipeline: `flattenHierarchy` → `pruneLeaves`.

## Known Limitations

- Prims with external/internal references are not flattened (reference targets would break).
- Prims with time-sampled transforms are preserved to avoid rewriting animation.
- Material bindings and relationships are tracked and rewired after reparenting.
- The operation uses `SdfBatchNamespaceEdit` which may fail for certain complex hierarchy cases.