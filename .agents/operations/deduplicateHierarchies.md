<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Deduplicate Hierarchies

**Key:** `deduplicateHierarchies`
**Source:** `source/operations/deduplicateHierarchies/DeduplicateHierarchies.cpp`

> **Safety note.** Duplicates are identified by subtree shape — the
> operation only merges prims whose hierarchy structure, prim types, and
> authored property names are identical, then refines the candidate
> groups by comparing all authored property values. This is safe on any
> asset.
>
> For per-mesh duplicate detection based on actual geometry, use
> `deduplicateGeometry` instead — typically as a follow-up step.

## Overview

Deduplicate Hierarchies finds duplicate prim *hierarchies* (whole subtrees) and replaces duplicates with instanceable internal references to the first instance (the prototype). The prototype keeps its authored content; every other duplicate gets its children deleted, an internal reference authored to the prototype, and `instanceable=true` set on it.

Unlike `deduplicateGeometry`, which compares individual meshes by vertex data, this operation compares *hierarchies*. Prims are grouped by an FNV-1a hash of each subtree's shape, prim type names, and sorted authored property names, then a full property-value comparison verifies that all authored property **values** match (excluding xformOp values on the root prim, which represent placement; descendant transforms must match). This is safe on any asset — structurally identical subtrees with different mesh data, material parameters, etc. will NOT be collapsed.

It walks **breadth-first** under the default prim (or under the user-supplied `paths` if non-empty), groups prims at each level, and any group with two or more prims becomes a duplicate group.

Once a level produces matches, the matched prims are pruned from further traversal. Children of *unmatched* prims become the next level. Material-related prims (Material/Shader/NodeGraph types, `Looks`/`Materials`/`Mesh` scopes, and prims whose name starts with a texture prefix like `Diffuse` or `Normal`) are skipped at every level. Prims that already author references or payloads are excluded from the final duplicate set so we don't replace an already-customised instance with a generic reference.

The operation creates internal instanceable references from duplicate subtrees to the first instance (the prototype).

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (whole stage) | Optional subtree roots. When non-empty, the BFS starts at the *children* of each listed prim path. Empty = walk children of the default prim. |
| `tolerance` | float | `0.001` | Acceptable difference for floating-point array properties (points, normals, UVs, etc.) and scalar float/double values when comparing subtrees. The value is in stage units. All other types — including transforms (matrices, scalar vectors, quaternions), topology indices, strings, etc. — always require exact match regardless of tolerance. Set to 0 for bitwise-exact comparison. Matches `deduplicateGeometry`'s default. |
| `ignoreShaderOutputs` | bool | `true` | Skip shader output attributes (`outputs:surface`, `outputs:displacement`, etc.) during value comparison. These often differ between material instances even when the geometry is identical, so ignoring them lets structurally-identical hierarchies under different material networks still match. Set to `false` for a stricter compare. |

> **Verbose logging.** `verbose` is not an op argument — it's a field on
> `ExecutionContext` (default off). To enable per-level / per-group
> progress logging for this op, prepend an `executionContext` entry to
> the chain: `{"operation": "executionContext", "verbose": true}`.

## Matching behavior

The structural hash covers, per descendant prim: relative path within the subtree, type name, and sorted authored property name tokens. After structural grouping, a **full property-value comparison** verifies candidates are truly identical — all authored attribute values must match (excluding `xformOp:*` and `xformOpOrder` on the root prim, which represent placement; descendant transforms must match). This means different mesh points, UVs, material parameters, internal transforms, or any other authored data will prevent deduplication.

Pitfall: a single extra child prim, extra authored attribute, or any differing attribute value (mesh points, UVs, material parameters) prevents matching unless `tolerance` is set. With `tolerance=0.001` (default), small floating-point drift is absorbed. Set to 0 for bitwise-exact comparison, or increase for assets with larger numerical drift from re-export or tessellation.

When you're unsure, run in analysis mode first and inspect the `{prototype: [duplicates]}` map before committing.

## Tuning Order

1. **`tolerance` first** — Default 0.001. If the operation finds fewer duplicates than expected, try increasing tolerance to absorb floating-point drift from re-export or tessellation. Set to 0 for bitwise-exact comparison. Only affects float/vec arrays and scalar float/double; integer topology always requires exact match.
2. **`paths` second** — Start with the default (whole-stage scan). Set this only when you need to restrict to a known subtree, or when the default-prim traversal pulls in unrelated content (e.g., a top-level `/Environment` xform).
3. **`ignoreShaderOutputs` last** — Default `true`. Flip to `false` only when the user explicitly wants shader output attributes to participate in the value comparison (rare — these usually differ between material instances even when the geometry is identical).

## Visual Diagnosis

_Not applicable — purely structural. Effects are composition-arc changes (new internal references) and `instanceable=true` flags. Verify by inspecting the stage hierarchy: prototype keeps its authored children, duplicates show up as instanceable refs to the prototype._

## Starting Configs

**Hierarchy-only (single-step)**:
```json
[{"operation": "deduplicateHierarchies"}]
```

**Recommended pipeline** — hierarchy dedup followed by per-mesh dedup. Catches both whole-subtree duplicates and identical meshes that share geometry but sit under different parents. This is the configuration that matches a fully-deduplicated reference asset most closely:
```json
[
  {"operation": "deduplicateHierarchies"},
  {"operation": "deduplicateGeometry", "duplicateMethod": 2, "tolerance": 0.001}
]
```

**With tolerance** for assets that have minor floating-point drift between duplicates (e.g. CAD re-exports):
```json
[{"operation": "deduplicateHierarchies", "tolerance": 0.001}]
```

**Restrict to a known subtree**:
```json
[{"operation": "deduplicateHierarchies", "paths": ["/World/Vegetation"]}]
```

## Prerequisites & Workflows

- **Stage must have a default prim** when `paths` is empty (the default scan starts at the default prim's children).
- **Recommended two-step pipeline**: this operation first (catches whole-subtree duplicates), then `deduplicateGeometry` (catches per-mesh duplicates the hierarchy pass missed because the meshes sit under different parents). Single-step hierarchy dedup typically catches ~70% of what the combined pipeline catches; the rest is per-mesh.
- For per-prototype merging (single mesh per prototype hierarchy) or external payload export to standalone files, those workflows require Kit-side helpers (`omni.kit.commands`, `omni.usd`) and are not available in this standalone library.
- **Save the result via the root layer**, not via `stage.Export()`. `stage.Export()` flattens the composed stage and renames Usd-instance prototypes to synthetic root-level paths (e.g. `/Flattened_Prototype_N`) — functionally equivalent, but loses the authored prototype names. Prefer `stage.GetRootLayer().Export(path)` or an equivalent layer-preserving save. The skill's Tier 1 example shows the correct call.

## Known Limitations

- **Internal references only.** The C++ port covers Mode 1 of the Python processor. Mode 1b (merge prototype meshes) and Mode 2 (external payloads) require Kit-side composition helpers and remain in the Python processor.
- **Material-related skip predicate.** Prims whose name is `Looks`, `Materials`, or `Mesh`, prims of type `Material`/`Shader`/`NodeGraph`, and prims whose name starts with a texture prefix (`Diffuse`, `Specular`, `Normal`, `Roughness`, `Metallic`, `Emissive`, `Opacity`, `AO`, `Displacement`) are unconditionally skipped. If a hierarchy you expected to dedup is being passed over, it likely matches one of those predicates.
- **Existing references / payloads excluded.** Prims with authored refs or payloads are detected at the matched-group stage but excluded from the final duplicate list, so they aren't overwritten. They still count as "matched" at their level — their children won't be visited at deeper levels.
- **No cross-stage support.** The operation only authors references to prims within the same stage. For external payload export, see the Python processor.
- **Transformations on duplicates are preserved.** Setting `instanceable=true` plus authoring an internal reference keeps the duplicate prim's local transform. Visual appearance should match before/after for typical transform-only duplicates; if your duplicates differ in attribute opinions other than transform, those opinions are *removed* (children get deleted before the reference is authored).
