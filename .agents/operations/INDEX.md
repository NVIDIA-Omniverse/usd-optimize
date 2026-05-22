<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Operation Index

Quick reference for all Scene Optimizer operations. Use this to find operations by name, check guide availability, and prioritize tuning guide authoring.

**Companion docs:**
- [`INVOCATION.md`](INVOCATION.md) — how to actually *call* an operation (Python API, JSON helper, driver script).
- [`PIPELINES.md`](PIPELINES.md) — curated multi-op chains organized by bottleneck (memory, load time, mesh count, data quality).
- [`_template.md`](_template.md) — template for new operation guides.

| Operation | Key | Args |
|---|---|---|
| Dice Meshes | `diceMeshes` | 22 |
| Fit Primitives | `fitPrimitives` | 21 |
| Primitives to Meshes | `primitivesToMeshes` | 14 |
| Merge | `merge` | 14 |
| Box Clip | `boxClip` | 13 |
| Mesh Cleanup | `meshCleanup` | 11 |
| Generate Scene | `generateScene` | 11 |
| De-duplicate Geometry | `deduplicateGeometry` | 9 |
| Decimate Meshes | `decimateMeshes` | 8 |
| Remove Prims | `removePrims` | 8 |
| Find Occluded Meshes | `findOccludedMeshes` | 7 |
| Generate Projection UVs | `generateProjectionUVs` | 7 |
| Shrinkwrap | `shrinkwrap` | 7 |
| Generate Atlas UVs | `generateAtlasUVs` | 7 |
| Optimize Time Samples | `optimizeTimeSamples` | 6 |
| Optimize Primvars | `optimizePrimvars` | 6 |
| Generate Normals | `generateNormals` | 5 |
| Subdivide Meshes | `subdivideMeshes` | 5 |
| Merge Vertices | `mergeVertices` | 5 |
| Split Meshes | `splitMeshes` | 5 |
| Edit Stage Metrics | `editStageMetrics` | 4 |
| Remove Small Geometry | `removeSmallGeometry` | 4 |
| Compute Pivot | `pivot` | 4 |
| Remesh Meshes | `remeshMeshes` | 4 |
| Optimize Materials | `optimizeMaterials` | 4 |
| Find Coinciding Geometry | `findCoincidingGeometry` | 4 |
| Find Overlapping Meshes | `findOverlappingMeshes` | 4 |
| Deduplicate Hierarchies | `deduplicateHierarchies` | 3 |
| Count Vertices | `countVertices` | 3 |
| Print Stats | `printStats` | 3 |
| Remove Attributes | `removeAttributes` | 3 |
| Remove Unused UVs | `removeUnusedUVs` | 3 |
| Prune Leaves | `pruneLeaves` | 3 |
| Find Flat Hierarchies | `findFlatHierarchies` | 3 |
| Organize Prototypes | `organizePrototypes` | 2 |
| Flatten Hierarchy | `flattenHierarchy` | 2 |
| Triangulate Meshes | `triangulateMeshes` | 2 |
| Utility Function | `utilityFunction` | 2 |
| Compute Extents | `computeExtents` | 1 |
| Delete Prims | `deletePrims` | 1 |
| Manifold Meshes | `manifoldMeshes` | 1 |
| RTX Mesh Count | `rtxMeshCount` | 1 |
| Python Script | `pythonScript` | 1 |
| Delete Hidden Prims | `deleteHiddenPrims` | 0 |
| Remove Untyped Prims | `removeUntypedPrims` | 0 |
| Sparse Meshes | `sparseMeshes` | 0 |
| Optimize Skeleton Roots | `optimizeSkelRoots` | 0 |
