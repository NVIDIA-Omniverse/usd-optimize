<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Delete Hidden Prims

**Key:** `deleteHiddenPrims`
**Source:** `source/operations/deleteHiddenPrims/__init__.py`

## Overview

Delete Hidden Prims finds and deletes all prims that have their visibility set to `invisible`. This is a Python-based operation that traverses the stage, collects invisible prims, and delegates their deletion to the `deletePrims` operation.

The operation prunes traversal at invisible prims — if a parent is invisible, its children are not individually traversed (the parent's deletion will remove them).

This is a zero-argument operation with no tunable parameters.

## Parameters

None.

## Tuning Order

_Not applicable — no parameters to tune._

## Visual Diagnosis

_Not applicable — no rendered output to diagnose; effect is structural (invisible prims removed)._

## Starting Configs

```json
[{"operation": "deleteHiddenPrims"}]
```

## Prerequisites & Workflows

- Hidden operation (`visible` returns false).
- Internally uses the `deletePrims` operation.
- Alternative to `removePrims` with `removeInvisible=true`, but simpler and with no configuration options.

## Known Limitations

- Hidden operation — not shown in the UI.
- No ability to deactivate or hide instead of delete.
- Only checks the `visibility` attribute — not computed visibility from ancestors.