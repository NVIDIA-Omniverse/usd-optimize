<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Operation pipelines (USD authoring & optimization)

Curated multi-operation chains organized by bottleneck. Use this as a
starting point — every asset is different, so treat each chain as a recipe
to adapt, not a fixed prescription.

For per-operation parameters and tuning guidance, see the matching
`.agents/operations/<key>.md` guide. For invocation patterns, see
`.agents/operations/INVOCATION.md`. To execute a named pipeline, pass
`--pipeline <name>` to the `run-operations` skill (or copy the JSON below).

> **Scope.** This doc covers Scene Optimizer ops that operate on USD content
> — geometry, materials, hierarchy, primvars, animation. It does **not**
> cover render-side / runtime knobs (RTX flags, FSD renderer instancing,
> viewport pacing). For authoring-side fixes that live outside Scene
> Optimizer (payload structure, file format, scenegraph instancing), see the
> sidebar below.

---

## Diagnose first

**Don't run anything blindly.** Validation tells you which rules fired and
which prims are affected — that's the signal you build the pipeline from.

```bash
/run-validators path/to/asset.usd
/interpret-validators path/to/asset.usd
```

The interpret report includes a per-rule fix tier (T1 = run the op, T2 =
run + tune, T3 = analysis-only / manual) and the operation key for each
firing rule. Those op keys are the inputs to the pipelines below.

If the validator report shows mostly **base** rules and 0 Scene Optimizer
issues, the asset likely has no `UsdGeomMesh` prims (mesh-only SO rules
short-circuit via `REQUIRES_MESH`). The fix is upstream — see the
*Upstream authoring* sidebar below.

---

## Lossless vs. bounded-loss

The named pipelines below are split into two categories:

- **Lossless** — reorganize / dedup / regenerate metadata without removing
  authored geometry. Safe to run by default.
  - `safe-cleanup`, `memory-reduction`, `load-time-reduction`, `hierarchy-dedup`
- **Bounded-loss** — remove geometry, repair topology, or decimate.
  Effects are visible but bounded; the agent should confirm with the
  user before running, especially what the goal is (preserve silhouette
  vs. hit a triangle target).
  - `mesh-count-reduction` (drops sub-screen-space meshes; conservative
    decimation driven by geometric error budget) and `data-quality-baseline`
    (regenerates normals, may change topology via `makeManifold`).

When uncertain about which pipeline to run, fall back to `safe-cleanup` —
it's all-lossless and a reasonable starting point on any asset.

### Decimation: prefer `maxMeanError` over `reductionFactor`

`decimateMeshes` has two stop conditions and the choice between them
matters more than the value you pick:

- **`maxMeanError`** — stops when the mean geometric error exceeds the
  threshold. **Use this when shape integrity matters** (the typical case).
  Value is in world units, so the right number depends on scene scale —
  `0.01` is a reasonable starting point for scenes in meter units; tune
  against your asset.
- **`reductionFactor`** — stops when the vertex count hits a target
  *percentage* (0–100). **Use this only when the goal is a specific
  reduction rate** (e.g., "drop 25% to fit a memory budget"). Default
  `50.0` keeps half the vertices; values below 10 will likely produce
  results that no longer resemble the original.

> ⚠️ **`reductionFactor` is a percentage (0–100), not a fraction.**
> Passing `0.5` means "keep 0.5% of vertices" — drops 99.5%. If you mean
> "drop half" use `50.0`.

To use one and disable the other, set the unused one to `0.0` with a float
literal. For quality-driven decimation:
`{"reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": true}`. For
target-fraction decimation:
`{"reductionFactor": 75.0, "maxMeanError": 0.0, "pinBoundaries": true}`.

`mesh-count-reduction` defaults to `maxMeanError` mode for that reason.
For per-parameter detail (`pinBoundaries`, `guideDecimation`, etc.) see
`.agents/operations/decimateMeshes.md`.

---

## By bottleneck

### Safe cleanup (default fallback)

**Goal:** baseline lossless cleanup that's safe to run on any asset
without user intervention. Use this when the user asks "just optimize
this" without specifying constraints, or when the validator report is
ambiguous.

**Pipeline (`--pipeline safe-cleanup`):** _LOSSLESS_

```json
[
  {"operation": "computeExtents"},
  {"operation": "pruneLeaves"},
  {"operation": "deduplicateGeometry", "considerInstanceability": true},
  {"operation": "optimizeMaterials"},
  {"operation": "optimizeTimeSamples"}
]
```

Each step regenerates metadata or collapses duplicates without removing
authored data. None of the operations changes geometry vertex counts or
material visual appearance.

### Memory / file size

**Goal:** reduce the byte size of the asset and the prim count it
materializes at runtime.

**Pipeline (`--pipeline memory-reduction`):** _LOSSLESS_

```json
[
  {"operation": "deduplicateGeometry", "considerInstanceability": true},
  {"operation": "optimizeMaterials"},
  {"operation": "pruneLeaves"}
]
```

Why this order:

1. **`deduplicateGeometry`** first — collapses identical meshes into USD
   instances.
2. **`optimizeMaterials`** — collapses identical material networks. Often
   wins big on imported CAD where every prim got its own copy of the same
   shader.
3. **`pruneLeaves`** — removes empty `Xform` / `Scope` leaves left over
   from CAD export or hierarchy edits.

**See also:**
- For assets with whole-subtree duplicates (e.g., 12,000 copies of the
  same pallet hierarchy), prepend `deduplicateHierarchies` — see the
  `hierarchy-dedup` pipeline below.
- For poly-count reduction on top of dedup, use `mesh-count-reduction`,
  which adds bounded-loss decimation (see *Decimation* section above
  for `maxMeanError` vs `reductionFactor` guidance).

### Hierarchy dedup (CAD-style or structurally-repeated assets)

**Goal:** collapse whole duplicate prim subtrees (not just per-mesh
duplicates) into one prototype with `instanceable=true` references.

**Pipeline (`--pipeline hierarchy-dedup`):** _LOSSLESS_

```json
[
  {"operation": "deduplicateHierarchies"},
  {"operation": "deduplicateGeometry", "duplicateMethod": 2, "tolerance": 0.001}
]
```

Why this order:

1. **`deduplicateHierarchies`** first — walks the stage
   breadth-first under the default prim, groups duplicate subtrees, and
   replaces each duplicate with an internal reference + `instanceable=true`
   pointing at the first instance. On CAD-imported assemblies this
   typically delivers ~88% prim-count reduction by itself. Matches by
   structural hash + property-value comparison — safe on any asset.
2. **`deduplicateGeometry`** second — picks up per-mesh duplicates that
   the hierarchy pass didn't fold (different parents, same geometry).
   `duplicateMethod: 2` = Instanceable Reference, matching the
   hierarchy pass's output style.

### Load time

**Goal:** reduce time-to-first-frame and stage open time.

**Pipeline (`--pipeline load-time-reduction`):**

```json
[
  {"operation": "computeExtents"},
  {"operation": "pruneLeaves"},
  {"operation": "optimizeTimeSamples"},
  {"operation": "optimizeMaterials"}
]
```

Why this order:

1. **`computeExtents`** — authored extents let the renderer skip computing
   them at load and unlock fast bbox-based culling.
2. **`pruneLeaves`** — fewer prims to traverse at load.
3. **`optimizeTimeSamples`** — drops redundant animation samples; large
   wins on rigs and physics caches.
4. **`optimizeMaterials`** — fewer shaders to compile.

Load time is often dominated by **upstream authoring decisions** (text
`.usda` vs binary `.usdc`, monolithic vs payloaded layout, instancing
strategy). Scene Optimizer can't fix those; see the sidebar.

### Triangle / mesh count

**Goal:** reduce poly count and draw-call count while preserving visible
silhouette quality.

**Pipeline (`--pipeline mesh-count-reduction`):** _BOUNDED-LOSS — confirm before running_

```json
[
  {"operation": "meshCleanup",
   "mergeVertices": true,
   "removeIsolatedVertices": true,
   "removeDegenerateFaces": true},
  {"operation": "deduplicateGeometry"},
  {"operation": "removeSmallGeometry"},
  {"operation": "decimateMeshes",
   "reductionFactor": 0.0,
   "maxMeanError": 0.01,
   "pinBoundaries": true}
]
```

Why this order:

1. **`meshCleanup`** — fixes degenerates and welds colocated verts before
   anything else operates on the data. Clean meshes decimate cleaner; dirty
   meshes produce dirty decimates.
2. **`deduplicateGeometry`** — instance-share what's identical so we
   decimate the prototype once.
3. **`removeSmallGeometry`** — drop sub-screen-space meshes the renderer
   would have drawn at zero pixels anyway.
4. **`decimateMeshes`** — finally reduce poly count on what remains.
   Defaults to **`maxMeanError`-driven** decimation (`reductionFactor: 0.0`,
   `maxMeanError: 0.01`, `pinBoundaries: true`) so the stop condition is
   geometric tolerance, not a fixed fraction. Bump `maxMeanError` for
   more reduction; lower it for tighter quality. **Switch to
   `reductionFactor` (e.g., `75.0` to drop 25%) only when the user wants
   a specific reduction rate** — see the *Decimation* section above.

To also collapse draw calls, append `merge` *cautiously* — see the merge
caveat below.

**Alternative — target-fraction decimation:** when the user wants a
specific reduction rate (e.g., "drop half"), swap step 4 for
`reductionFactor`-driven mode:

```json
{"operation": "decimateMeshes", "reductionFactor": 50.0, "maxMeanError": 0.0, "pinBoundaries": true}
```

Remember `reductionFactor` is a percentage in 0–100. Values below 10
typically destroy the silhouette (see the operation guide).

### Data quality

**Goal:** fix authoring issues that produce visual artifacts (bad normals,
non-manifold geometry, missing extents, inconsistent winding).

**Pipeline (`--pipeline data-quality-baseline`):**

```json
[
  {"operation": "generateNormals"},
  {"operation": "meshCleanup",
   "mergeVertices": true,
   "removeDegenerateFaces": true,
   "coorientFaces": true,
   "makeManifold": true},
  {"operation": "computeExtents"}
]
```

Why this order:

1. **`generateNormals`** — regenerates missing or invalid normals so
   downstream shading is correct.
2. **`meshCleanup`** with `coorientFaces` and `makeManifold` — fixes
   winding inconsistencies and non-manifold edges that can trip up
   physics, raytracing, and decimation.
3. **`computeExtents`** — ensures every prim has authored bounding-box
   metadata.

Run this **first** when the validator reports a mix of mesh-quality issues
— it's a prerequisite for the other three pipelines, since dirty meshes
make `deduplicateGeometry` and `decimateMeshes` produce worse results.

---

## Critical caveats

### Don't merge if the scene already uses instancing

`merge` (Merge Static Meshes) collapses prims into combined geometry. If
the input scene relies on **scenegraph instancing** (`instanceable=true` on
referenced/payloaded duplicates), **`UsdGeomPointInstancer`**, or
**geometry streaming**, merging undoes those wins:

- Each instance becomes its own copy of the merged geometry → memory
  blows up.
- Streaming chunks lose their boundaries → load benefits disappear.

Run `merge` only when the scene is *not* already instanced. If unsure,
inspect the stage with `usdview` — instances appear as `</__Prototype_*>`
master prims.

### `deduplicateGeometry` alone is mesh-level, not hierarchy-level

`deduplicateGeometry` collapses **individual mesh data**, not whole
hierarchies. If a partner has 12,000 copies of the same pallet hierarchy
(Xform → cube → cube → cube), running `deduplicateGeometry` alone
collapses the per-pallet *meshes* but leaves 12,000 copies of the
*hierarchy*.

For hierarchy dedup, use `deduplicateHierarchies` (the
`hierarchy-dedup` pipeline below). It walks the stage breadth-first under
the default prim and replaces duplicate subtrees with `instanceable=true`
internal references to the first instance. Pair it with
`deduplicateGeometry` afterwards to also catch per-mesh duplicates that
share parents in the hierarchy-grouped output.

Matching uses an FNV-1a hash of each subtree's shape, prim type names,
and sorted authored property names, then verifies all authored property
values match (excluding xformOps on the root prim). Safe on any asset —
structurally identical subtrees with different mesh data, material
parameters, etc. will NOT be collapsed. Pitfall: a single extra child
or extra authored attribute breaks the match — for finer per-mesh
matching despite small structural drift, fall back to the standard
two-step pipeline (hierarchy dedup → `deduplicateGeometry`).

See `.agents/operations/deduplicateHierarchies.md` for the full
parameter reference and per-parameter pitfalls.

### All operations are destructive

Operations mutate the stage in-place. **Always run on a copy** or open
the asset, route the optimized result to a new layer, and `Export()` to a
new path. The `run-operations` skill defaults to writing output to a
sibling path so the source survives.

### Decimate after cleanup, not before

`decimateMeshes` makes geometric simplifications based on local topology.
Pre-existing degenerates and isolated vertices confuse the heuristic and
produce visible quality regressions. Always run `meshCleanup` first.

---

## Upstream authoring (when Scene Optimizer can't fix it)

Some performance issues are baked into the asset's authoring choices and
can't be undone by Scene Optimizer ops. If validation finds nothing
actionable on the SO side, look upstream:

| Symptom | Authoring fix |
|---|---|
| Slow open times on a `.usda` text file | Convert to `.usdc` binary (`usdcat -o out.usdc in.usda`). `.usdc` supports memory-mapped I/O; `.usda` does not. |
| Large monolithic asset that loads everything | Restructure as a lightweight interface file with a `Payload` to the heavy content. Open with `LoadNone` for fast model-hierarchy view; load payloads selectively. |
| Many copies of the same asset | Set `instanceable=true` on the referenced/payloaded prim. **Caveat:** only helps when the asset is referenced/payloaded — not for raw duplicated `Xform` hierarchies (use the hierarchy-dedup script above for that). |
| Millions of small repeated objects (bolts, vegetation) | `UsdGeomPointInstancer` instead of per-instance prims. |
| Layer count exploded (hundreds–thousands) | Audit composition — sublayers compound across references. |
| Asset shipped as `.usdz` for runtime | `.usdz` is a packaging format. Unpack to `.usdc` for runtime; USDZ front-loads everything and bypasses runtime caches. |

---

## Workflow loop

```
Diagnose  → /run-validators + /interpret-validators
Configure → pick a pipeline (this doc) or build a custom op list
Execute   → /run-operations <asset> --pipeline <name>  (or --config <json>)
Review    → re-run /run-validators on the output, compare counts
Iterate   → adjust parameters per the operation guide; re-execute
Validate  → confirm the targeted rules' failures dropped
```

Use `tools/perf_validators/run.sh compare before.json after.json` to diff
two summary JSONs — the per-rule delta tells you which ops moved the
needle.

---

## Validator → fix-op alignment

The mapping from validator rules to fix operations is canonical in
`.agents/skills/interpret-validators/SKILL.md` §Rule reference (search for
`SceneOptimizer*Checker`). The pipelines above pull from that mapping; if
the two ever disagree, the interpret-validators reference wins.

A condensed view of which ops resolve which families of validator
findings:

| Bottleneck | Validator rules typically firing | Pipeline |
|---|---|---|
| Memory / file size | DuplicateGeometry, FuzzyDuplicateGeometry, DuplicateMaterials, EmptyLeaf | `memory-reduction` |
| Hierarchy duplicates (CAD imports) | DuplicateHierarchies | `hierarchy-dedup` |
| Load time | EmptyLeaf, RedundantTimeSamples, FlatHierarchies | `load-time-reduction` |
| Triangle / mesh count | RtxMeshCount, MeshDensity, SmallMesh, ZeroExtent, SparseMeshes | `mesh-count-reduction` |
| Data quality | Normals, ColocatedVertices, DuplicateFaces, IsolatedVertices, NonManifold, Windings, ZeroAreaFaces | `data-quality-baseline` |

Rules not listed here are usually T3 (analysis-only) and need a manual
review of the reported prim list — see interpret-validators.

---

## Adding a new pipeline

To add a pipeline that the `--pipeline <name>` flag can resolve:

1. Add a section above with the JSON config and rationale.
2. Add the entry to `tools/perf_operations/pipelines.json` (the driver's
   pipeline registry — keys must match the `<name>` users will pass).
3. If it's a recommendation for a specific bottleneck, link it from the
   table above.

Keep the pipeline list small and curated. The point is to capture
load-bearing patterns, not to enumerate every possible op chain.
