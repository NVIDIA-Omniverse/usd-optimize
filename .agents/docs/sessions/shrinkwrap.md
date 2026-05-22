---
name: shrinkwrap-session
description: "Shrinkwrap tuning session logs with iteration details."
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Shrinkwrap Tuning Session Summary

## Session 1: Teapot — Closing a Hole at the Spout Tip

**Input:** `shrinkwrap-test-files/teapot/teapot.usdc`
**Goal:** Fill the hole at the tip of the teapot spout while keeping the surface tight to the original mesh.
**Scene units:** Centimeters (metersPerUnit = 0.01), bounding box ~6.4 units across.

### Iteration Log

| # | voxelSize | erode | threshold | adaptivity | Result |
|---|-----------|-------|-----------|------------|--------|
| 1 | 0.1 | 8.0 | 2.0 | 0.0 | Hole closed, but surface very bloated/puffy. Lid rim lost definition. |
| 2 | 0.1 | 4.0 | 2.0 | 0.0 | Even more bloated — decreasing erode was the wrong direction. |
| 3 | 0.1 | 12.0 | 2.0 | 0.0 | No visible change from iteration 1. Erode beyond 8 had no effect at this voxelSize. |
| 4 | 0.05 | 64.0 | 1.0 | 0.0 | **Breakthrough** — halving voxelSize produced the first visible change. Surface much tighter. |
| 5 | 0.05 | 64.0 | 0.025 | 0.0 | Tightest fit yet. Handle hole visible, spout well-defined. But spout tip slightly over-eroded. |
| 6 | 0.05 | 8.0 | 0.05 | 0.0 | **Best result.** Spout closed, surface hugs the original, no over-erosion. |

### Key Learnings

1. **voxelSize is the most important parameter.** Iterations 1-3 all produced nearly identical output because voxelSize=0.1 was too coarse — changes to erode and threshold had no visible effect. Halving voxelSize to 0.05 was the single biggest improvement.

2. **threshold should be as small as possible.** Starting at 2.0 caused severe bloating. The final value of 0.05 was just enough to close the spout hole without inflating other features (like the lid-body gap).

3. **erode's effect plateaus around 8.** Increasing erode from 8 to 12 produced no change. Going to 64 with a fine voxelSize did tighten the surface, but also over-eroded the thin spout. The default of 8 was optimal for this mesh.

4. **erode recovers detail, it doesn't destroy it.** Decreasing erode from 8 to 4 made the surface *more* bloated, not less. Higher erode snaps the surface back toward the original; lower erode leaves it puffier.

### Final Config
```json
[{"operation": "shrinkwrap", "dim": 0, "voxelSize": 0.05, "erode": 8.0, "threshold": 0.05, "adaptivity": 0.0}]
```

---

## Session 2: Hidden Mesh — Wrapping Walls While Removing Interior Geometry

**Input:** `shrinkwrap-test-files/hiddenMesh/hiddenMesh.usda`
**Goal:** Shrinkwrap a box made of 6 wall panels so it wraps the outer surface, removing a Suzanne (monkey head) hidden inside.
**Scene units:** Centimeters (metersPerUnit = 0.01), root has 100x scale. Effective bounding box ~400 cm.

### Pre-processing

Shrinkwrap operates per-prim, so it had to be combined with a **merge** operation first to combine all 7 meshes (6 walls + monkey) into a single mesh. Without merging, each wall and the monkey were shrinkwrapped independently.

```json
[
  {"operation": "merge"},
  {"operation": "shrinkwrap", ...}
]
```

After shrinkwrap, the original `/merged` prim (containing the monkey) had to be manually deactivated in the output USD, as no clean automated removal method was found.

### Iteration Log

| # | voxelSize | erode | threshold | Result |
|---|-----------|-------|-----------|--------|
| 1 | 5.0 | 8.0 | 20.0 | Rounded blob. Monkey gone but walls have no sharp edges. Grid only 21 voxels across — far too coarse. |
| 2 | 1.0 | 8.0 | 5.0 | Much sharper walls, no monkey. Corners still slightly beveled. |
| 2b | 1.0 | 2.0 | 5.0 | Identical to iteration 2. Changing erode had zero effect. |
| 2c | 1.0 | 1.0 | 5.0 | Identical again. At voxelSize=1.0, erode and threshold have no effect. |
| 2d | 1.0 | 8.0 | 10.0 | Still identical. threshold changes also had no effect. |
| 2e | 1.0 | 8.0 | 30.0 | Still identical. The voxel grid fully determines the output at this resolution. |
| 3 | 0.5 | 8.0 | 5.0 | **Sharpest walls.** 106k quads. Corners well-defined, clean interior, no monkey. |
| 4 | 4.0 | 8.0 | 30.0 | Rounded but monkey gone. First time threshold/erode visibly affected the output — the coarse grid allowed morphological operations to work. |
| 5 | 1.0 | 64.0 | 0.1 | **Monkey returned!** High erode + low threshold eroded through the walls, exposing interior geometry. Monkey visible but rounded. |

### Key Learnings

1. **voxelSize dominates for simple geometry.** For flat wall panels, the voxelization itself determines the output — threshold and erode only matter when the voxelSize is large enough that the walls are thin relative to the grid (a few voxels thick).

2. **There's a parameter regime where erode punches through walls.** With voxelSize=1.0 and erode=64, the erosion was aggressive enough to eat through the wall geometry, re-exposing the hidden monkey. This demonstrates that erode operates in voxel units and its effect depends on wall thickness relative to voxelSize.

3. **The "no effect" plateau is real.** At voxelSize=1.0, every combination of erode (1-32) and threshold (0.1-30) produced identical output (151,076 active voxels, 27,544 quads). The walls were thick enough at this resolution that morphological operations had nothing to do.

4. **threshold controls which gaps get closed, erode controls how deep it digs.** A low threshold allows erode to dig into thin features (walls, panels). A high threshold prevents this by treating the gaps as features to preserve. This is the erode/threshold interaction described in the tuning guide.

5. **Merge is a prerequisite for multi-mesh shrinkwrap.** Without merging, shrinkwrap treats each prim independently and cannot reason about interior vs exterior across separate meshes.

### Best Result

Iteration 3 provided the best balance — sharp walls, no monkey, clean interior:

```json
[
  {"operation": "merge"},
  {"operation": "shrinkwrap", "dim": 0, "voxelSize": 0.5, "erode": 8.0, "threshold": 5.0, "adaptivity": 0.0}
]
```

---

## General Tuning Principles (from both sessions)

1. **Start with voxelSize.** It's the resolution of the entire operation. If it's too coarse, nothing else matters. If it's too fine, you'll pay in memory/time for no benefit.

2. **Keep threshold as small as possible.** Only increase it enough to close the specific gaps you care about. Excess threshold causes bloating and can close features you want to keep.

3. **Leave erode at the default (8) unless you have a reason to change it.** It recovers surface detail after gap-closing. Higher values help but plateau quickly. Very high values can erode through thin geometry.

4. **If parameter changes have no visible effect, change voxelSize first.** This was the most common trap in both sessions — sweeping erode and threshold while voxelSize was the actual bottleneck.

5. **Watch the output stats.** Identical active voxel counts and quad counts across runs mean the parameters aren't affecting the output. Change voxelSize to break out of the plateau.

6. **Use merge before shrinkwrap for multi-mesh scenes.** Shrinkwrap operates per-prim and can't reason about interior/exterior across separate meshes.