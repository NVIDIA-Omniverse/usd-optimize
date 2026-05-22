<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Delete Prims

**Key:** `deletePrims`
**Source:** `source/operations/deletePrims/DeletePrimsPlugin.cpp`

## Overview

Delete Prims is a hidden utility operation that permanently removes specified prims from the stage's edit target layer. It is used programmatically by other operations (e.g., `deleteHiddenPrims`, `findOccludedMeshes`) to perform the actual deletion.

**Data loss warning:** deletion is destructive and cannot be undone within the operation. Callers must validate prim paths and confirm intent before invoking this operation.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `primPaths` | string[] | `[]` | Prim paths (or SdfPathExpression patterns) to delete. |

## Tuning Order

_Not applicable — single parameter; nothing to order._

## Visual Diagnosis

Deletion is intentionally visible: the targeted prims disappear from the rendered stage. With no tunable parameters, there's nothing to tune by symptom — verify the caller's `primPaths` list when the wrong prims go missing. Note that prims defined outside the edit target are deactivated rather than removed; a downstream layer that re-asserts `active=true` will keep them rendered.

## Starting Configs

_Not applicable — hidden internal operation; not configured directly by users. See `deleteHiddenPrims` and `removePrims` for user-facing equivalents._

## Deletion Behavior

- **Scope:** removes the prim spec from the stage's edit target layer. Removing a parent implicitly removes all descendants in that layer.
- **Over specs:** by default, `over` specs (opinion overrides that don't define the prim) are skipped (not deleted). If a prim only exists as an `over` in the edit target, it is deactivated instead of removed.
- **Fallback to deactivation:** if a prim cannot be removed from the edit target layer (e.g., it is defined in a referenced layer), the operation sets it inactive (`prim.SetActive(false)`) rather than failing.
- **Default prim:** if the stage's default prim is in the deletion set, it is cleared (`ClearDefaultPrim`) before removal.
- **Batched removal:** deletions are grouped by parent prim and applied in a single `SdfChangeBlock` to minimize recomposition cost on large stages.
- **Invalid paths:** invalid or non-existent prim paths are silently skipped.
- **References:** any external references, relationships, or connections pointing to deleted prims will become broken. The operation does not scan for or warn about dangling references.
- **Return value:** always returns success. Errors (invalid prims, unremovable specs) are handled by fallback to deactivation, not by failure.

## Prerequisites & Workflows

- Hidden operation (`getVisible()` returns false) — not shown in the UI.
- Used internally by other operations; not intended for direct use.
- Callers (e.g., `deleteHiddenPrims`, `findOccludedMeshes` with `action=Delete`) must perform their own validation and user confirmation before passing paths to this operation.

## Known Limitations

- Cannot remove prims defined in layers other than the edit target — falls back to deactivation.
- Does not warn about or repair dangling references after deletion.
- No undo mechanism within the operation itself (USD undo depends on the host application's undo stack).