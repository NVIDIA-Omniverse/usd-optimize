---
name: create-proxy
description: Create a USD proxy mesh sibling. Use to generate decimated, bbox, or LOD stand-ins, with optional render/proxy purpose variant set.
version: "1.0.0"
allowed-tools: Read, Write, Bash
metadata:
  author: NVIDIA Corporation
  tags: [usd, proxy, lod, decimation]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Create USD Proxy

Generate a low-detail proxy mesh as a sibling of a source prim hierarchy, then mark the two with USD `purpose` so downstream tooling can pick render vs. proxy automatically. The result is roughly:

```
/World/Asset             ← source (untouched), purpose = render
/World/Asset_proxy       ← merged + decimated mesh, purpose = proxy
```

Use this skill when a user asks to:

- Create a proxy / LOD stand-in for a hi-res asset
- Speed up viewport interaction by giving downstream tools a low-poly fallback
- Set up a render/proxy pair so DCCs can swap representations via USD `purpose`

## What this skill covers

This SKILL.md is the navigation layer. Detailed step bodies live in `references/` so the agent can fetch them on demand. Search for keywords like `decimate`, `bbox`, `variant`, `pre-flight`, `pitfalls`, `verification` to jump.

- **Agent usage notes** — two operating modes (config-only vs end-to-end).
- **Inputs** — required + optional arguments.
- **Choosing your proxy mode** — three strategies (decimate, bbox-whole, bbox-per-mesh).
- **Workflow at a glance** — short summary of the step ordering.
- **Pre-flight checks** — required preconditions before authoring.
- **References** — pointers to the detailed step-by-step docs.
- **Common pitfalls** — known foot-guns.
- **Verification** — how to confirm the proxy is wired up correctly.

Companion: operation guides under `.agents/operations/` for each underlying operation.

## Agent usage notes

This skill has two operating modes — pick based on what the user asked for:

- **Config-only.** Produce the JSON config and hand it back. Default when the user has not provided input/output USD paths or a Scene Optimizer runtime.
- **End-to-end.** Generate the config *and* run it. Only when the user has explicitly given an input USD, an output USD, and an available SO runtime (see *Execution context* below).

Operating rules for either mode:

- **Temp files go outside tracked content.** Scratch JSON configs or `pythonScript` script bodies belong in an agent-writable scratch directory the user has approved — typically the OS temp dir, but if you're sandboxed (Codex, etc.) use whichever writable root the runtime allows. Don't drop scratch files in `docs/`, `source/`, `scene-optimizer-core/source/`, or any version-controlled location. Save in-tree only when the user explicitly asks.
- **Never run a config containing `<base64 ...>` placeholders.** The example assembled config in `references/decimate-mode.md` and `references/bounding-box-modes.md` shows shape, not a runnable artifact. Encode each script body before invoking the runner, and **omit disabled optional steps entirely** (drop Step 2 when `stripAnimation: false`, drop Step 6 when `setupVariantSet: false`) — don't ship empty/placeholder bodies.
- **JSON-parse the assembled config before running.** A typo in a substituted base64 string or a missed comma will surface as a parse error from the runner; catch it locally first.

## Inputs

The user must supply `sourcePrimPath`. Everything else has a default:

| Input | Default | Meaning |
|---|---|---|
| `sourcePrimPath` | (required) | Absolute path of the source prim, e.g. `/World/Asset`. |
| `proxyMode` | `"decimate"` | Which proxy strategy: `"decimate"` (merge + decimate the source — full pipeline, recognizable shape), `"boundingBoxWhole"` (single 8-vert AABB around the source — cheapest possible proxy), or `"boundingBoxPerMesh"` (one AABB per leaf mesh, mirroring source hierarchy — preserves silhouette). See *Choosing your proxy mode* below. |
| `reductionFactor` | `25.0` | (Decimate mode only.) `decimateMeshes` target percentage of vertices to retain (0–100). 25 = keep 25%. Set to `0.0` if `maxMeanError` should drive stopping. |
| `maxMeanError` | `0.0` | (Decimate mode only.) `decimateMeshes` max mean geometric error. `0.0` = disabled. |
| `stripAnimation` | `true` | (Decimate mode only.) Strip time samples on the proxy subtree before merging. Bounding-box modes don't copy source data so this doesn't apply. |
| `setupVariantSet` | `false` | Author a variant set that flips render/proxy purposes. When `true`, the four overrides below kick in. |
| `variantSetParentPath` | parent of source | Prim where the variant set is authored. Must be an ancestor of both `sourcePrimPath` and the proxy. |
| `variantSetName` | `"displayPurpose"` | Name of the variant set. |
| `defaultVariantName` | `"render"` | Default variant: source.purpose=render, proxy.purpose=proxy. |
| `swappedVariantName` | `"proxy"` | Swapped variant: source.purpose=proxy, proxy.purpose=render. |

The proxy path is always `<sourcePrimPath>_proxy` — derived from the source path, not user-configurable.

## Choosing your proxy mode

Three modes; pick by use case, not by what looks visually nice:

| Mode | Output | Cost | When to use |
|---|---|---|---|
| `decimate` | Reduced-poly version of the merged source. | Topology-floor surprises; iterate-with-user loop. | Default. Proxy should "look like" the source at distance — recognizable shape with a smaller poly count. |
| `boundingBoxWhole` | Single 8-vert AABB around the entire source. | Constant 8 verts. Fully deterministic. | Occluder, picking proxy, far-far LOD where shape recognition is irrelevant. Cheapest possible representation. |
| `boundingBoxPerMesh` | One AABB per leaf mesh, mirroring source hierarchy. | `8 × source_mesh_count` verts. Fully deterministic. | Silhouette/structure preservation as a "Lego" approximation. Recognizable preview without paying for decimation. |

The bbox modes don't iterate — output is fully determined by the source's AABB(s). The matrix and the iterate-with-user loop only apply to `decimate` mode (see `references/parameter-tuning.md`).

## Execution context

| To do this... | You need... |
|---|---|
| Generate the JSON config | `sourcePrimPath`. Everything else has a default. |
| Execute the config | The above, plus: input USD path, output USD path, and a Scene Optimizer runtime — either a source-tree build or an installed prebuilt package. |

For runtime setup, defer to the existing skills rather than duplicating instructions here:

- **Source-tree build** — see `.agents/skills/build/SKILL.md`.
- **Prebuilt package** — see `.agents/skills/prebuilt-package/SKILL.md`.

## Workflow at a glance

When `proxyMode: "decimate"`:

1. **Deep-copy** source subtree → `<sourcePrimPath>_proxy` (`pythonScript`).
2. **(Optional) Strip animation** on the proxy subtree (`pythonScript`).
3. **Merge** the proxy subtree into a single mesh (`merge`).
4. **Decimate** with Topology Simplification on (`decimateMeshes` with `allowCutAndGlue: true`).
5. **Set purposes** — source=render, proxy=proxy (`pythonScript`).
6. **(Optional) Author variant set** that swaps purposes (`pythonScript`).

When `proxyMode: "boundingBoxWhole"` or `"boundingBoxPerMesh"`:

1. **Author bounding-box proxy** in one `pythonScript` (replaces Steps 1–4 above): defines `<source>_proxy` as a fresh Xform and authors one or more AABB box meshes inside it.
2. **Set purposes** — same as Step 5 above.
3. **(Optional) Author variant set** — same as Step 6 above.

Steps 1, 2, 5, 6 (decimate mode) and the bbox-authoring step are USD authoring glue not covered by Scene Optimizer ops; they run via the `pythonScript` operation. Steps 3, 4 (decimate mode only) are native SO operations.

## Pre-flight checks

Validate these before invoking the pipeline — they aren't gated by the operations themselves, so running blind on a missing prim or empty subtree produces a confusing failure deep into the run.

1. **`sourcePrimPath` resolves on the input stage.** Open the file (e.g. via `usdview` or a quick `pxr.Usd.Stage.Open` + `GetPrimAtPath`) and confirm the prim exists. Step 1 raises if it doesn't.
2. **The source subtree contains at least one `UsdGeomMesh`.** If it's only Xforms or non-mesh prims, Step 3 (`merge`) has nothing to consolidate and Step 4 will decimate nothing. Worth a quick traversal:
   ```python
   from pxr import Usd, UsdGeom
   src = stage.GetPrimAtPath("/World/Asset")
   has_mesh = any(p.IsA(UsdGeom.Mesh) for p in Usd.PrimRange(src))
   ```
3. **`<sourcePrimPath>_proxy` does not already exist.** If it does, decide with the user before proceeding:
   - **Overwrite** — call out that Step 1's `Sdf.CopySpec` overwrites authored specs at that path on the edit-target layer.
   - **Delete first** — prepend a `pythonScript` step that calls `stage.RemovePrim("<source>_proxy")` (or use the `removePrims` operation).
   - **Different name** — the proxy path is hard-coded to `<source>_proxy` in every step; a different suffix means editing the steps consistently.
4. **Composition arcs.** `Sdf.CopySpec` copies *only the authored specs at the current edit-target layer* — it does NOT copy the fully composed prim. Two failure modes:
   - **Source has no authored spec in the edit-target layer.** If the source is reachable only through a reference/payload defined elsewhere, the edit target may have nothing to copy, and the proxy will be **empty or nearly empty** even though the source renders fine in `usdview`. Verify by checking `stage.GetEditTarget().GetLayer().GetPrimAtPath(SOURCE_PATH)` — if that returns `None` or a spec with no children, copy will not produce a usable proxy.
   - **Source has specs but only as overrides on referenced content.** The proxy will resolve the same composition arcs as the source — referenced/payloaded layers continue to resolve from their original locations. The result is not a self-contained flattened proxy.
   To handle either case, flatten the source subtree before running this skill, set the edit target to a layer that does carry the source's specs, or extend Step 1 to flatten arcs as it copies.

## References

The detailed step bodies, parameter tables, and end-to-end runner code live in companion docs to keep this SKILL.md focused on routing decisions:

| Reference | Covers |
|---|---|
| `references/decimate-mode.md` | Steps 1–6 for `decimate` mode (deep copy, strip animation, merge, decimate with Topology Simplification, set purposes, optional variant set), the assembled JSON config, and the standalone end-to-end runner. |
| `references/bounding-box-modes.md` | Common scaffolding + Mode A (`boundingBoxWhole`) + Mode B (`boundingBoxPerMesh`), plus the assembled JSON config for bbox mode. |
| `references/parameter-tuning.md` | `reductionFactor` vs `maxMeanError`, the vertex-count starting-value matrix, agent-side pre-analysis script, runtime verification, and the iterate-with-user loop. |

## Common pitfalls

- **Source must contain meshes.** If `<sourcePrimPath>` resolves to only Xforms or non-mesh prims, merge has nothing to consolidate. Validate before running, or expect an empty proxy.
- **Instances.** A subtree containing instance proxies cannot be authored in place. If your source uses USD instancing, run `utilityFunction` with `function: "Deinstance"` on the proxy subtree between Step 1 and Step 2.
- **References / payloads.** `Sdf.CopySpec` copies the spec at the edit target. The proxy will resolve the same composition arcs as the source (referenced/payloaded layers continue to resolve from their original locations). For a fully self-contained proxy, flatten references first.
- **Animation strip + referenced layers.** When the source's animation lives in a referenced/payloaded layer, Step 2's strip works via USD composition-strength override — `attr.Set(first_value)` authors a default at the edit target, and that default wins over any time samples in deeper layers. The proxy reads static at first-sample value. The override only holds while the edit target is *stronger* than the layer carrying the animation. For full safety in those setups, flatten the source's composition arcs before running this skill.
- **Time-sample inspection can mislead.** `attr.GetNumTimeSamples()` is value-resolved across all layers, so after the strip it can still report samples from a referenced layer. Verify with `attr.Get(some_time)` if you need to confirm the stronger default authored by the strip is what downstream consumers see.
- **Weak edit targets may not strip animation.** If the edit target is weaker than a layer that carries animation, the stronger time samples continue to win and the proxy stays animated. In that case, flatten the source composition arcs or author the strip in a stronger layer before running the merge/decimate steps.
- **Animation strip is destructive on the proxy.** Step 2 clears every time-sampled attribute on the proxy and replaces it with the first-sample value. Source animation is untouched.
- **Transforms collapse.** `merge` bakes local transforms into vertex positions. After Step 3 the merged mesh sits at `<source>_proxy` with identity transform.
- **VariantSet parent must dominate both prims.** `variantSetParentPath` must be an ancestor of both `<source>` and `<source>_proxy`. The default (parent of source) satisfies this for sibling layout. Setting it elsewhere produces a variant that cannot reach the prims it is meant to swap.
- **Topology Simplification is not free.** `allowCutAndGlue: true` is slower than the default decimator path. For very large stages, expect Step 4 to dominate runtime.

## Verification

After the pipeline runs, confirm:

- `<sourcePrimPath>_proxy` exists and contains a single `UsdGeomMesh` (or one per material bucket if `considerMaterials: true`).
- Source `purpose == render`, proxy `purpose == proxy`:
  ```python
  print(UsdGeom.Imageable(stage.GetPrimAtPath("/World/Asset")).GetPurposeAttr().Get())
  print(UsdGeom.Imageable(stage.GetPrimAtPath("/World/Asset_proxy")).GetPurposeAttr().Get())
  ```
- Proxy vertex count is roughly `reductionFactor%` of the merged-but-not-yet-decimated count. Use `printStats` (analysis mode) before and after Step 4 for a quick sanity check.
- When `setupVariantSet: true`: list variants on the parent prim and toggle them; the two purposes should flip between variants.

## Purpose

Generate a low-detail proxy sibling for a high-resolution USD subtree
and tag the two prims with USD `purpose` so DCCs and renderers can
pick render vs. proxy automatically. Supports three strategies —
decimate (merge + Topology Simplification), bounding-box-whole, and
bounding-box-per-mesh — plus an optional variant set that flips the
purpose pair on demand.

## Prerequisites

- A source USD asset with a valid `sourcePrimPath` containing at least
  one `UsdGeomMesh` (decimate / boundingBoxPerMesh modes); for
  boundingBoxWhole only the source bounding box is needed.
- For end-to-end execution: an SO runtime (source build via the
  `build` skill or an installed `prebuilt-package`), an input USD
  path, and an output USD path.
- For config-only mode: just `sourcePrimPath` and the desired
  parameters. The output is the assembled JSON config.
- Write access to a scratch directory (OS temp dir or sandbox temp
  root) for staging `pythonScript` bodies and the assembled config.

## Limitations

- **Source must contain authored specs at the edit-target layer** for
  decimate mode; pure reference/payload scenes need an upstream
  flatten.
- **Bounding-box modes are deterministic** — no parameter tuning, no
  iterate-with-user loop.
- **No DCC integration.** This skill authors USD `purpose` and an
  optional variant set; it does not configure how a specific renderer
  (Hydra, Storm, RTX, Maya, Houdini, …) presents them.
- **No automatic LOD selection.** The proxy is a single-level stand-in;
  multi-LOD chains would need additional skill logic on top.
- **Topology Simplification (`allowCutAndGlue: true`) is slow on very
  large stages.** Decimate runtime can dominate the pipeline; the
  iterate-with-user loop assumes the user can wait.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| Proxy is empty after the run | Source has no authored spec at the edit-target layer (reference/payload only). | Flatten the source subtree first, or set the edit target to a layer that carries the source's specs. |
| Proxy has many small meshes despite Step 3 | Default `mergePoint: 1` skips boundaries with a single mesh. | Set `allowSingleMeshes: true`, or widen with `mergePoint: 7` (Root Prim) / `0` (Stage). See `references/decimate-mode.md` § Step 3 variants. |
| Decimation overshoots the target % | Topology floor — many small meshes can't shrink below their minimum vertex count. | Drop `reductionFactor` further (e.g. 5 → 1) or accept the floor; see `references/parameter-tuning.md` § What to expect from the actual numbers. |
| Bbox box renders as a rounded blob | Default Catmull-Clark subdivision applied. | Ensure `subdivisionScheme = "none"` and `faceVarying` normals are authored — both are in `references/bounding-box-modes.md` § Common scaffolding. |
| `purpose` doesn't flow to descendants | Some descendant has its own authored `purpose` opinion. | Use `_set_purpose_subtree` in `references/decimate-mode.md` § Step 5 (it clears descendant authored opinions). |
| Variant body errors with "can't author opinion" | `variantSetParentPath` is not an ancestor of both source and proxy. | Pick a common ancestor (default: parent of source). |
| `pythonScript` step fails JSON parse | A `<base64 ...>` placeholder was left in the assembled config. | Replace every placeholder with the actual base64-encoded body before running. |
