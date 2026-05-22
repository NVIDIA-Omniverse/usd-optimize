<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Find Flat Hierarchies

**Key:** `findFlatHierarchies`
**Source:** `source/operations/findFlatHierarchies/FindFlatHierarchiesOperation.cpp`

## Overview

Find Flat Hierarchies identifies prims with an excessively large number of children — "flat" hierarchy patterns where a single prim has hundreds or thousands of direct children. These flat structures can degrade performance in hierarchy traversal and UI display.

**`maxChildren`** sets the threshold: prims with at least this many children are reported. **`considerAllChildren`** controls whether to count all children (including inactive, unloaded, abstract) or only the default active/loaded/defined set.

This is an analysis-only operation — it reports findings but doesn't modify the stage.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to search. Empty = entire stage. |
| `maxChildren` | int | (default) | Threshold number of children to flag as flat. |
| `considerAllChildren` | bool | `false` | Count all children including inactive, unloaded, abstract. |

## Tuning Order

1. **`maxChildren` first** — Set the threshold. 100–500 is a typical range for detecting problematic flat hierarchies.
2. **`considerAllChildren` second** — Enable for a complete count including inactive prims.

## Visual Diagnosis

_Not applicable — analysis-only; results are reported as JSON paths and child counts._

## Starting Configs

**Find excessively flat hierarchies**:
```json
[{"operation": "findFlatHierarchies", "maxChildren": 100}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage.
- Supports analysis mode for Asset Validator integration.
- Use results to decide where to apply `organizePrototypes` or manual restructuring.

## Known Limitations

- Analysis-only — does not modify the stage.
- Returns a JSON result with paths and child counts.