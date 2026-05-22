<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# create-proxy — Choosing `reductionFactor` vs `maxMeanError`

*(Only relevant when `proxyMode: "decimate"`. Skip this doc for bbox modes.)*

`decimateMeshes` exposes two stop conditions; you can use one, the other, or both. When both are non-zero, decimation stops at whichever fires first per mesh.

| Parameter | Direction | Pick when |
|---|---|---|
| `reductionFactor` (0–100) | **Amount to *retain*.** `25` = keep 25% / remove 75%. **Lower = more aggressive.** `0.0` disables. Use a float literal. | You want a quick proxy with a predictable output size. No specific quality budget. |
| `maxMeanError` (stage units) | Error budget. **Higher = more decimation.** `0.0` disables. Use a float literal. Values are in your stage's native units (resolved via `metersPerUnit`). | You have a tolerance budget — e.g. "silhouette can drift up to 1 cm" in a centimeter-scale stage, "up to 0.5 m" in a meter-scale stage. |

**Default rule of thumb:** start with `reductionFactor` only. Reach for `maxMeanError` when the user has a tolerance budget; combine both when you want a percentage cap *and* a quality floor.

## Recommended starting values

Pick the row by the source's pre-decimation vertex count (post-merge — but pre-merge total is a good proxy since `merge` doesn't dedupe vertices). `bbox_diag` is the source subtree's world-space bounding-box diagonal **in stage units** (compute via the agent-side analysis below).

| Vertex count | Use case | `reductionFactor` | `maxMeanError` |
|---|---|---|---|
| < 10K | Anything | `75.0` | `0.0` (already small — consider skipping decimate) |
| 10K – 100K | General preview | `40.0` | `0.0` |
| 10K – 100K | Silhouette-preserving | `0.0` | `0.001 × bbox_diag` |
| 100K – 1M | Distance LOD | `15.0` | `0.0` |
| 100K – 1M | Silhouette-preserving | `0.0` | `0.002 × bbox_diag` |
| 1M – 10M | Aggressive distance LOD | `10.0` | `0.0` |
| 1M – 10M | Silhouette-preserving | `0.0` | `0.005 × bbox_diag` |
| > 10M | Maximum reduction | `5.0` | `0.005 × bbox_diag` (cap with both) |

Worked examples for `0.001 × bbox_diag` (silhouette-preserving) on a 1m × 2m × 1m asset:

- Centimeter-scale stage (`metersPerUnit = 0.01`): `bbox_diag ≈ 245` → `maxMeanError ≈ 0.245` (≈ 2.5 mm).
- Meter-scale stage (`metersPerUnit = 1.0`): `bbox_diag ≈ 2.45` → `maxMeanError ≈ 0.00245` (≈ 2.5 mm).

`reductionFactor < 10` produces coarse results — verify visually at the intended view distance before shipping.

## Agent-side analysis (run before assembling the config)

Run this in your local USD/pxr environment to get the numbers you need to pick from the matrix:

```python
from pxr import Usd, UsdGeom

INPUT_USD   = "/path/to/input.usda"
SOURCE_PATH = "/World/Asset"

stage = Usd.Stage.Open(INPUT_USD)
src   = stage.GetPrimAtPath(SOURCE_PATH)
if not src or not src.IsValid():
    raise SystemExit(f"Source prim not found: {SOURCE_PATH}")

mpu = UsdGeom.GetStageMetersPerUnit(stage)

total_verts = 0
for prim in Usd.PrimRange(src):
    if prim.IsA(UsdGeom.Mesh):
        pts = UsdGeom.Mesh(prim).GetPointsAttr().Get()
        if pts:
            total_verts += len(pts)

bcache = UsdGeom.BBoxCache(Usd.TimeCode.Default(),
                           [UsdGeom.Tokens.default_, UsdGeom.Tokens.render])
bbox = bcache.ComputeWorldBound(src).ComputeAlignedBox()
diag = (bbox.GetMax() - bbox.GetMin()).GetLength() if not bbox.IsEmpty() else 0.0

print(f"vertex_count = {total_verts}")
print(f"bbox_diagonal = {diag} (stage units; metersPerUnit = {mpu})")
```

Use `vertex_count` to pick the matrix row, then compute `maxMeanError = <fraction> × bbox_diagonal` if the chosen row uses tolerance.

## Runtime verification

The assembled config (see `decimate-mode.md` § Putting it together) inserts a `printStats` step after `decimateMeshes`. **`printStats` reports whole-stage stats, not proxy-subtree stats** — useful as a coarse run log, but for the actual reduction number do a proxy-only traversal post-export:

```python
from pxr import Usd, UsdGeom

stage = Usd.Stage.Open(OUTPUT_USD)
proxy = stage.GetPrimAtPath("/World/Asset_proxy")

proxy_verts = sum(
    len(UsdGeom.Mesh(p).GetPointsAttr().Get() or [])
    for p in Usd.PrimRange(proxy)
    if p.IsA(UsdGeom.Mesh)
)
print(f"proxy vertex count: {proxy_verts:,}")
print(f"retained: {100.0 * proxy_verts / pre_decimate_verts:.2f}% (target: {REDUCTION_FACTOR}%)")
```

Compare `proxy_verts` against `pre_decimate_verts × (reductionFactor / 100)` to confirm the decimator hit its target. If actual is much higher, see [What to expect from the actual numbers](#what-to-expect-from-the-actual-numbers) below — the topology floor is usually the cause.

## Iterating with the user

*(Decimate mode only. Bounding-box modes are deterministic — there's nothing to iterate on.)*

The matrix is a starting point, not a verdict. Real proxies almost always need a second pass — first runs typically come in *higher* than the matrix predicted, and what counts as "acceptable" is a visual call the user has to make.

The expected loop:

1. Pick matrix-recommended values, run the pipeline, capture `printStats` after decimate.
2. Report **predicted vs actual** to the user, plus the proxy's mesh and vertex count: e.g. *"matrix predicted ~10% retained; decimate produced 10.86%."*
3. Ask: *"Acceptable? Or should I make the proxy more / less aggressive?"*
4. On "more aggressive": drop `reductionFactor` (e.g. 10 → 5 → 1), or enable `allowSingleMeshes` on the merge step (see `decimate-mode.md` § Step 3 variants) to fold singleton meshes into the merge flow, or both.
5. On "less aggressive": raise `reductionFactor`, or switch to a `maxMeanError` budget for a quality floor.
6. Re-run; repeat until the user signs off. Keep each run's settings + actual numbers in the conversation so the user has context for trade-offs.

### What to expect from the actual numbers

Real reduction depends on the asset's mesh-size distribution, not just `reductionFactor`. A few patterns worth recognising so you can set expectations and explain results to the user:

- **Topology floor.** The decimator can't shrink a mesh below its topological minimum (a closed cube can't go under 8 vertices, etc.). On assets dominated by many small parts — CAD assemblies, screws/bolts/clips — actual retention will floor *higher* than the target. Expect overshoot at low `reductionFactor` values.
- **Overshoot is non-monotonic in the percentage budget.** The worst overshoot tends to be in the mid-range, not at the extremes. If `reductionFactor: 5` overshoots more than `reductionFactor: 10` *or* `reductionFactor: 1`, that's not a bug — it's the floor effect interacting with which meshes still have headroom at that target.
- **Don't assume halving the value halves the result.** `reductionFactor: 1` doesn't necessarily produce 1/5 the verts of `reductionFactor: 5`. Past a certain target, most meshes are already at floor and only the largest meshes still respond.
- **Mesh count doesn't track vertex count.** `allowSingleMeshes: true` and the wider-boundary variants change *prim* counts dramatically (more or fewer merge groups) without proportionally changing the *vertex* count the GPU sees. When reporting to the user, lead with vertex count for size/perf framing and mesh count for hierarchy framing.

Together: don't promise a specific retention percentage from the matrix — quote the matrix as the target, run, and let the actual numbers drive the next decision.

The matrix exists so the agent doesn't start at a random number; the iteration loop, plus an honest read of the actual numbers, is what gets you to a usable proxy.
