<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Organize Prototypes

**Key:** `organizePrototypes`
**Source:** `source/operations/organizePrototypes/OrganizePrototypes.cpp`

## Overview

Organize Prototypes moves internal scene-graph instance prototypes under a user-specified namespace (class prim). This cleans up scene hierarchy by consolidating scattered prototypes into a single location, improving scene readability and potentially reducing composition overhead.

The operation finds all instanceable references in the stage, copies their targets under a class prim at **`prototypesNamespace`**, and rewrites the references to point to the new location. The original prototype prims are then converted to instances referencing the new location.

**`hierarchyLevels`** controls how many ancestor levels are preserved in the new location, maintaining some of the original hierarchy structure.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `prototypesNamespace` | string | `"Prototypes"` | Prim path where prototypes will be organized under. |
| `hierarchyLevels` | int | `0` | Number of ancestor levels to preserve when reparenting prototypes. |

## Tuning Order

1. **`prototypesNamespace` first** — Choose a meaningful location (e.g., `/Prototypes` or `/World/Prototypes`).
2. **`hierarchyLevels` second** — Increase to preserve hierarchy context. 0 = flat organization.

## Visual Diagnosis

_Not applicable — purely structural reorganization. Verify by inspecting the new `prototypesNamespace` location after running and confirming references resolve._

## Starting Configs

**Standard organization**:
```json
[{"operation": "organizePrototypes", "prototypesNamespace": "Prototypes"}]
```

**Preserve hierarchy**:
```json
[{"operation": "organizePrototypes", "prototypesNamespace": "Prototypes", "hierarchyLevels": 2}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage with instanceable references.
- Common pipeline: `deduplicateGeometry` → `organizePrototypes`.

## Known Limitations

- Only processes internal references on the edit target layer. External references are skipped.
- The namespace path must be a valid absolute prim path.
- Prototypes that are already class prims are not moved.