<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Remove Attributes

**Key:** `removeAttributes`
**Source:** `source/operations/removeAttributes/RemoveAttributes.cpp`

## Overview

Remove Attributes removes or blocks specified attributes from prims. This is useful for cleaning up scenes by removing unwanted metadata, custom attributes, or entire attribute namespaces.

Attributes can be specified by exact name or by namespace prefix (ending with `:`). For example, `"primvars:st"` removes a specific attribute, while `"custom:"` removes all attributes in the `custom` namespace.

**`mode`** controls whether attributes are removed (deleted from the layer) or blocked (authored as a block opinion, preventing inheritance).

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all prims) | Prim paths to process. Empty = all prims. |
| `mode` | enum | `Remove` (0) | Action: `Remove` (0) = delete, `Block` (1) = author block opinion. |
| `attributes` | string[] | `[]` | Attribute names or namespace prefixes to remove. |

## Tuning Order

1. **`attributes` first** — Specify what to remove. Use exact names or namespace prefixes.
2. **`mode` second** — `Remove` for permanent deletion. `Block` to override inherited values without deleting.

## Visual Diagnosis

_Not applicable — attribute-level cleanup; no rendered output to diagnose. If an attribute that should have been removed is still present, check that the name matches exactly or the namespace prefix ends with `:`._

## Starting Configs

**Remove specific attributes**:
```json
[{"operation": "removeAttributes", "attributes": ["customData:myAttr", "primvars:unused"]}]
```

**Remove entire namespace**:
```json
[{"operation": "removeAttributes", "attributes": ["custom:"]}]
```

## Prerequisites & Workflows

- Works standalone on any USD stage.
- Uses parallel processing (TBB) for performance on large stages.

## Known Limitations

- Invalid attribute names are logged as warnings and skipped.
- Namespace prefixes must end with `:` to be recognized.