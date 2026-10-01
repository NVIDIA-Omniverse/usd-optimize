<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# interpret-validators — Rule reference

The mapping below is the source of truth for the `Fix tier` and `Operation`
columns presented in the Step 4 summary table. **To verify a rule's backing operation**, grep `OPERATION_NAME` in
`source/validators/python/usd_optimize/validators/<module>.py`. The
authoritative list of registered rules is the `_RULE_CATEGORIES` tuple in
`validators/__init__.py`.

## UsdOptimize rules

| Rule | Backing op | Tier | Notes |
|------|-----------|------|-------|
| UsdOptimizeCoincidingGeometryChecker | `findCoincidingGeometry` | T3 | Analysis-only. Fix: review prim list, remove duplicates with `removePrims`. |
| UsdOptimizeColocatedVerticesChecker | `meshCleanup` | T1 | `meshCleanup` merges colocated vertices. |
| UsdOptimizeDuplicateFaceChecker | `meshCleanup` | T1 | `meshCleanup` removes duplicate faces. |
| UsdOptimizeDuplicateGeometryChecker | `deduplicateGeometry` | T1 | Converts identical meshes to USD instances. |
| UsdOptimizeDuplicateMaterialsChecker | `optimizeMaterials` | T1 | Merges duplicate material definitions. |
| UsdOptimizeEmptyLeafChecker | `pruneLeaves` | T1 | Removes leaf prims with no geometry. |
| UsdOptimizeFlatHierarchiesChecker | `findFlatHierarchies` | T3 | Analysis-only. Fix: `flattenHierarchy` operation. |
| UsdOptimizeFuzzyDuplicateGeometryChecker | `deduplicateGeometry` | T1 | Same op, different threshold. |
| UsdOptimizeHighVertexCountChecker | `countVertices` | T2 | Informational. To reduce, prefer lossless paths first (`deduplicateGeometry`, `removeSmallGeometry`); add `decimateMeshes` only after confirming the goal with the user — silhouette preservation (`maxMeanError`) vs target reduction rate (`reductionFactor` 0–100). See `docs/operations/decimateMeshes.rst`. |
| UsdOptimizeIndexedPrimvarChecker | `optimizePrimvars` **`mode=1`** | T1 | Converts to indexed primvars. **Requires `mode=1`** — the default `mode=0` is Ignore and logs "nothing to do!" without changing anything. |
| UsdOptimizeInvisiblePrimsChecker | `removePrims` | T2 | Confirm intent before removing — invisible may be deliberate. |
| UsdOptimizeIsolatedVerticesChecker | `meshCleanup` | T1 | `meshCleanup` removes isolated verts. |
| UsdOptimizeNonManifoldChecker | `meshCleanup` | T2 | Some non-manifold cases require DCC edit; `meshCleanup` handles common ones. |
| UsdOptimizeNormalsChecker | `generateNormals` | T1 | Regenerates missing/invalid normals. |
| UsdOptimizePrimitiveFitChecker | `fitPrimitives` | T2 | Replaces meshes with USD primitives where it fits; tune carefully. |
| UsdOptimizeRedundantTimeSamplesChecker | `optimizeTimeSamples` | T1 | Removes redundant samples on animated attributes. |
| UsdOptimizeRtxMeshCountChecker | `rtxMeshCount` | T2 | Informational threshold check. Reduce mesh count via `deduplicateGeometry` + `flattenHierarchy` + `removeSmallGeometry`. |
| UsdOptimizeSmallMeshChecker | `removeSmallGeometry` | T1 | Removes meshes below a screen-space threshold. |
| UsdOptimizeSparseMeshChecker | `sparseMeshes` (analysis only) | T3 | **No arguments exist** — `sparseMeshes` is a hidden, analysis-only operation and its density thresholds are compile-time constants. Running it as a fix returns an error, which aborts the whole chain and exits 1. Remediate the reported prims manually (split or cluster them in a DCC). |
| UsdOptimizeUnusedUVsChecker | `removeUnusedUVs` | T1 | Removes UV sets not bound to any material. |
| UsdOptimizeWindingsChecker | `meshCleanup` | T1 | Fixes inconsistent face winding. |
| UsdOptimizeZeroAreaFacesChecker | `meshCleanup` | T1 | Removes degenerate faces. |
| UsdOptimizeZeroExtentChecker | `removeSmallGeometry` | T1 | Validator runs `removeSmallGeometry` in analysis mode to find zero-extent meshes; running it as a fix removes them. If the fix should be "recompute extent metadata" instead of removal, run `computeExtents` first. |

## UsdOptimize rules (slower / O(n²) on large stages)

These register alongside the rest; they're noted here only because their backing op can be slow on large stages.

| Rule | Backing op | Tier | Notes |
|------|-----------|------|-------|
| UsdOptimizeOccludedMeshesChecker | `findOccludedMeshes` | T2 | Analysis. Fix: pass the reported prim paths to `removePrims`. |
| UsdOptimizeFindOverlappingMeshesChecker | `findOverlappingMeshes` | T3 | Analysis-only. Fix: review and remove/merge in DCC. |

## Base usd-validation-nvidia rules (`usd_validation_nvidia:DefaultPlugin`)

The full list lives in the upstream `usd-validation-nvidia` package; we don't
mirror it here. Many base rules detect issues that map cleanly onto a Scene
Optimizer operation — surface the equivalent op so the user has an automated fix
path even when the rule itself is upstream.

**Stage / metadata (no Usd Optimize equivalent — manual fix):**

- `KindChecker`, `DefaultPrimChecker`, `StageMetadataChecker` — stage-metadata
  rules. **T3 / manual.** Fix via USD Python API:
  `stage.SetDefaultPrim(...)`, `prim.SetMetadata('kind', 'component')`,
  `UsdGeom.SetStageUpAxis(...)`, etc. Check the CSV `Suggestion` column.
- `OmniOrphanedPrimChecker`, `OmniDefaultPrimChecker` — Omni-flavored variants.
  T3 / manual.
- `LayerSpecChecker` — type/value mismatches in layer specs. T3 / manual.

**External references (no Usd Optimize equivalent — manual fix):**

- `MissingReferenceChecker` — unresolvable references. T3 / manual. Common cause:
  asset flattened on another machine with absolute paths. Fix by re-flattening
  with the textures available, or rewriting absolute paths to relative.
- `MaterialPathChecker` — `info:mdl:sourceAsset` attributes pointing at missing
  files. T3 / manual. Same root cause as `MissingReferenceChecker`.
- `NormalMapTextureChecker` — `UsdUVTexture inputs:file` unresolvable. T3 / manual.
- `UsdAsciiPerformanceChecker` — **T1.** Fires only on ASCII layers with large arrays.
  Fix by re-saving as crate: `usdcat -o out.usdc in.usda`.
  Re-running on the crate file clears it.
- `UsdShadeShaderSdrCompliance` and `MaterialUsdPreviewSurfaceChecker` —
  **known false positive in this build; dismiss, do not work them.**
  Both report `UsdPreviewSurface` as unregistered with the Sdr Registry,
  because the USD package we ship lacks the `usdShaders` plugin (`shaderDefs.usda`).
  They fire on essentially every asset with a `UsdPreviewSurface` shader,
  and say nothing about the asset itself.

**Geometry rules with Usd Optimize operation equivalents:**

| Base rule | Equivalent Usd Optimize op | Tier |
|-----------|------------------|------|
| `ExtentsChecker` | `computeExtents` | T1 |
| `IndexedPrimvarChecker` | `optimizePrimvars` **`mode=1`** (default `0` = Ignore) | T1 |
| `WeldChecker` | `meshCleanup` (welds colocated verts) | T1 |
| `NormalsValidChecker` | `generateNormals` | T1 |
| `ZeroAreaFaceChecker` | `meshCleanup` | T1 |
| `UnusedMeshTopologyChecker` | `meshCleanup` (removes unreferenced points) | T1 |
| `ManifoldChecker` | `meshCleanup` (some non-manifold cases need DCC) | T2 |

When marking these in the summary table, label the tier as `T1-equiv` /
`T2-equiv` so the user knows the remedy is a Usd Optimize operation they
run themselves, rather than a fix the validator applies for them.

(Usd Optimize's *own* rules are different: most attach a `Suggestion`, so
`IssueFixer` can apply them and `tools/validators/run.sh <asset> --fix`
drives exactly that. The base rules in the table above do not.)


For rules not in this list, treat as **T3 / manual** and surface the CSV
`Suggestion` column verbatim. Don't invent fix commands.
