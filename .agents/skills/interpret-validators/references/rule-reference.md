<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# interpret-validators — Rule reference

The mapping below is the source of truth for the `Fix tier` and `Operation`
columns presented in the Step 4 summary table. **To verify a rule's backing operation**, grep `OPERATION_NAME` in
`source/core/python/omni/scene/optimizer/validators/<module>.py`. The
authoritative list of registered rules is in `_default_rule_classes()` /
`_expensive_rule_classes()` in `_plugin.py`.

## SceneOptimizer rules (default)

| Rule | Backing op | Tier | Notes |
|------|-----------|------|-------|
| SceneOptimizerCoincidingGeometryChecker | `findCoincidingGeometry` | T3 | Analysis-only. Fix: review prim list, remove duplicates with `removePrims`. |
| SceneOptimizerColocatedVerticesChecker | `meshCleanup` | T1 | `meshCleanup` merges colocated vertices. |
| SceneOptimizerDuplicateFacesChecker | `meshCleanup` | T1 | `meshCleanup` removes duplicate faces. |
| SceneOptimizerDuplicateGeometryChecker | `deduplicateGeometry` | T1 | Converts identical meshes to USD instances. |
| SceneOptimizerDuplicateHierarchiesChecker | `deduplicateHierarchies` | T1 | Collapses duplicate prim *hierarchies* (whole subtrees) into instanceable internal references. Matches by structural hash + property-value comparison — safe on any asset. Pair with `deduplicateGeometry` after (see `hierarchy-dedup` pipeline) to also catch per-mesh duplicates that share geometry but sit under different parents. |
| SceneOptimizerDuplicateMaterialsChecker | `optimizeMaterials` | T1 | Merges duplicate material definitions. |
| SceneOptimizerEmptyLeafChecker | `pruneLeaves` | T1 | Removes leaf prims with no geometry. |
| SceneOptimizerFlatHierarchiesChecker | `findFlatHierarchies` | T3 | Analysis-only. Fix: `flattenHierarchy` operation. |
| SceneOptimizerFlattenHierarchyChecker | `flattenHierarchy` | T2 | Has params; tune via `tune-parameters` skill. |
| SceneOptimizerFuzzyDuplicateGeometryChecker | `deduplicateGeometry` | T1 | Same op, different threshold. |
| SceneOptimizerIndexedPrimvarChecker | `optimizePrimvars` | T1 | Converts to indexed primvars. |
| SceneOptimizerInvisiblePrimsChecker | `removePrims` | T2 | Confirm intent before removing — invisible may be deliberate. |
| SceneOptimizerIsolatedVerticesChecker | `meshCleanup` | T1 | `meshCleanup` removes isolated verts. |
| SceneOptimizerMeshDensityChecker | `countVertices` | T2 | Informational. To reduce, prefer lossless paths first (`deduplicateGeometry`, `removeSmallGeometry`); add `decimateMeshes` only after confirming the goal with the user — silhouette preservation (`maxMeanError`) vs target reduction rate (`reductionFactor` 0–100). See `.agents/operations/PIPELINES.md` *Decimation* section. |
| SceneOptimizerNonManifoldChecker | `meshCleanup` | T2 | Some non-manifold cases require DCC edit; `meshCleanup` handles common ones. |
| SceneOptimizerNormalsChecker | `generateNormals` | T1 | Regenerates missing/invalid normals. |
| SceneOptimizerPrimitiveFitChecker | `fitPrimitives` | T2 | Replaces meshes with USD primitives where it fits; tune carefully. |
| SceneOptimizerRedundantTimeSamplesChecker | `optimizeTimeSamples` | T1 | Removes redundant samples on animated attributes. |
| SceneOptimizerRtxMeshCountChecker | `rtxMeshCount` | T2 | Informational threshold check. Reduce mesh count via `deduplicateGeometry` + `flattenHierarchy` + `removeSmallGeometry`. |
| SceneOptimizerSmallMeshChecker | `removeSmallGeometry` | T1 | Removes meshes below a screen-space threshold. |
| SceneOptimizerSparseMeshChecker | `sparseMeshes` | T2 | Tune density thresholds. |
| SceneOptimizerUnusedUVsChecker | `removeUnusedUVs` | T1 | Removes UV sets not bound to any material. |
| SceneOptimizerWindingsChecker | `meshCleanup` | T1 | Fixes inconsistent face winding. |
| SceneOptimizerZeroAreaFacesChecker | `meshCleanup` | T1 | Removes degenerate faces. |
| SceneOptimizerZeroExtentChecker | `removeSmallGeometry` | T1 | Validator runs `removeSmallGeometry` in analysis mode to find zero-extent meshes; running it as a fix removes them. If the fix should be "recompute extent metadata" instead of removal, run `computeExtents` first. |

## SceneOptimizer rules (expensive)

| Rule | Backing op | Tier | Notes |
|------|-----------|------|-------|
| SceneOptimizerOccludedMeshesChecker | `findOccludedMeshes` | T2 | Analysis. Fix: pass the reported prim paths to `removePrims`. |
| SceneOptimizerFindOverlappingMeshesChecker | `findOverlappingMeshes` | T3 | Analysis-only. Fix: review and remove/merge in DCC. |

## Base asset-validator rules (`omni.asset_validator.DefaultPlugin`)

The full list lives in the upstream `omniverse-asset-validator` package; we don't
mirror it here. Many base rules detect issues that map cleanly onto a Scene
Optimizer operation — surface the equivalent op so the user has an automated fix
path even when the rule itself is upstream.

**Stage / metadata (no SO equivalent — manual fix):**

- `KindChecker`, `DefaultPrimChecker`, `StageMetadataChecker` — stage-metadata
  rules. **T3 / manual.** Fix via USD Python API:
  `stage.SetDefaultPrim(...)`, `prim.SetMetadata('kind', 'component')`,
  `UsdGeom.SetStageUpAxis(...)`, etc. Check the CSV `Suggestion` column.
- `OmniOrphanedPrimChecker`, `OmniDefaultPrimChecker` — Omni-flavored variants.
  T3 / manual.
- `LayerSpecChecker` — type/value mismatches in layer specs. T3 / manual.

**External references (no SO equivalent — manual fix):**

- `MissingReferenceChecker` — unresolvable references. T3 / manual. Common cause:
  asset flattened on another machine with absolute paths. Fix by re-flattening
  with the textures available, or rewriting absolute paths to relative.
- `MaterialPathChecker` — `info:mdl:sourceAsset` attributes pointing at missing
  files. T3 / manual. Same root cause as `MissingReferenceChecker`.
- `NormalMapTextureChecker` — `UsdUVTexture inputs:file` unresolvable. T3 / manual.

**Geometry rules with SO operation equivalents:**

| Base rule | Equivalent SO op | Tier |
|-----------|------------------|------|
| `ExtentsChecker` | `computeExtents` | T1 |
| `IndexedPrimvarChecker` | `optimizePrimvars` | T1 |
| `WeldChecker` | `meshCleanup` (welds colocated verts) | T1 |
| `NormalsValidChecker` | `generateNormals` | T1 |
| `ZeroAreaFaceChecker` | `meshCleanup` | T1 |
| `UnusedMeshTopologyChecker` | `meshCleanup` (removes unreferenced points) | T1 |
| `ManifoldChecker` | `meshCleanup` (some non-manifold cases need DCC) | T2 |

When marking these in the summary table, label the tier as `T1-equiv` /
`T2-equiv` so the user knows the fix is a Scene Optimizer op, not the
validator's own `--fix` (this repo's validators don't ship a `--fix` mode).

For rules not in this list, treat as **T3 / manual** and surface the CSV
`Suggestion` column verbatim. Don't invent fix commands.
