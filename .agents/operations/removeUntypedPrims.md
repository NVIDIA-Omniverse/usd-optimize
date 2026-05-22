<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Remove Untyped Prims

**Key:** `removeUntypedPrims`
**Source:** `source/operations/removeUntypedPrims/__init__.py`

## Overview

Remove Untyped Prims deletes prims that have no USD schema type. Untyped prims are often artifacts from import processes and serve no purpose in the scene. Prims under `/Render` are excluded as they may be intentionally untyped render settings.

This is a zero-argument Python-based operation with no tunable parameters.

## Parameters

None.

## Tuning Order

_Not applicable — no parameters to tune._

## Visual Diagnosis

_Not applicable — removes only schema-untyped prims; no rendered output to diagnose. If meaningful prims disappear, verify they had a USD schema type (typed prims are not affected)._

## Starting Configs

```json
[{"operation": "removeUntypedPrims"}]
```

## Prerequisites & Workflows

- Hidden operation (`visible` returns false).
- Useful for cleaning up imported scenes with leftover untyped prims.

## Known Limitations

- Hidden operation — not shown in the UI.
- Excludes prims under `/Render` only — other special hierarchies are not protected.
- Hard deletion with no undo within the operation.