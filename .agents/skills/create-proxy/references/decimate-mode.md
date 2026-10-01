<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# create-proxy — Decimate-mode step-by-step

The Python snippets below assume the Usd Optimize `pythonScript` execution environment, which exposes `stage` (a `pxr.Usd.Stage`). When invoking `pythonScript` from JSON the script body must be base64-encoded — see *Putting it together* below for how to encode.

## Step 1 — Deep-copy source to `<sourcePrimPath>_proxy`

```python
from pxr import Sdf

SOURCE_PATH = "/World/Asset"
PROXY_PATH  = SOURCE_PATH + "_proxy"

src_prim = stage.GetPrimAtPath(SOURCE_PATH)
if not src_prim or not src_prim.IsValid():
    raise RuntimeError("Source prim not found: " + SOURCE_PATH)

proxy_sdf = Sdf.Path(PROXY_PATH)
parent_prim = stage.GetPrimAtPath(proxy_sdf.GetParentPath())
if not parent_prim or not parent_prim.IsValid():
    raise RuntimeError("Proxy parent does not exist: " + str(proxy_sdf.GetParentPath()))

layer = stage.GetEditTarget().GetLayer()
Sdf.CreatePrimInLayer(layer, proxy_sdf)
Sdf.CopySpec(layer, Sdf.Path(SOURCE_PATH), layer, proxy_sdf)
```

`Sdf.CopySpec` copies the spec at the edit-target layer. References, payloads, and variantSets on the source are copied as-is; the underlying assets continue to resolve from their original locations.

## Step 2 — (Optional) Strip animation on the proxy subtree

```python
PROXY_PATH = "/World/Asset_proxy"

def _strip_anim(prim):
    for attr in prim.GetAttributes():
        if attr.GetNumTimeSamples() == 0:
            continue
        samples = attr.GetTimeSamples()
        first_value = attr.Get(samples[0])
        attr.Clear()
        if first_value is not None:
            attr.Set(first_value)
    for child in prim.GetAllChildren():
        _strip_anim(child)

proxy_prim = stage.GetPrimAtPath(PROXY_PATH)
if proxy_prim and proxy_prim.IsValid():
    _strip_anim(proxy_prim)
```

This replaces each animated attribute with the value at the first sampled time, then clears all samples. The source is untouched; only the proxy copy is affected.

Two caveats matter for referenced animation:

- `attr.GetNumTimeSamples()` is value-resolved across all layers, so after
  this strip it can still report samples from a referenced layer. Verify with
  `attr.Get(some_time)` if you need to confirm the stronger default authored
  by the strip is what downstream consumers see.
- If the edit target is weaker than the layer carrying animation, the stronger
  samples continue to win and the proxy stays animated. Flatten the source
  composition arcs or author the strip in a stronger layer before running the
  merge/decimate steps.

Skip this step entirely when `stripAnimation: false`.

## Step 3 — Merge the proxy subtree

```json
{
  "operation": "merge",
  "meshPrimPaths": ["/World/Asset_proxy"],
  "considerMaterials": false,
  "originalGeomOption": 1,
  "mergePoint": 1,
  "rootPath": "MergedProxy"
}
```

- `meshPrimPaths: ["<proxy_path>"]` scopes merge to the proxy subtree only — the source is left alone.
- `considerMaterials: false` collapses everything into one mesh regardless of material binding. Use `true` if you need per-material geometry subsets on the proxy.
- `originalGeomOption: 1` (Delete) cleans up the pre-merge meshes inside the proxy that participated in a merge group. The enum is `0` = Ignore, `1` = Delete, `2` = Deactivate, `3` = Hide (`RemoveMethod` in `source/core/src/RemovePrims.h`); `1` is also the operation's default. `0` would leave those meshes in place, so the proxy would keep both the merged mesh and the copies it replaced. Single-mesh boundaries (one mesh per parent) aren't merged at all and stay in place regardless.
- `mergePoint: 1` is `eXform` — display name **"Parent Xform"**. Each Xform-typed parent acts as a merge boundary, so meshes under one Xform consolidate together. This is the most aggressive practical choice for a proxy, which is why this recipe sets it explicitly — the operation's own default is `0` (Stage). Other useful values from `MergePointOption` (`source/core/src/geometry/SpatialClustering.h`): `0` = Stage (pseudo-root), `7` = Root Prim, `8` = Parent Prim (every prim is a boundary — most conservative), `9` = Original Prim.
- `rootPath: "MergedProxy"` is **a leaf name, not a full path**. Internally `rootPath` is split into a relative `parentPath` (appended onto every merge boundary) and a `name` leaf — so passing `"/A/B/C"` gets you `<boundary>/B/C` on every output. Pass a single token unless you specifically want the prefix-append behavior.

### Step 3 variants (when the proxy still has too many prims)

This recipe's `mergePoint: 1` skips boundaries that contain only one mesh — those singletons stay in the proxy untouched, so the proxy ends up with `~merge_groups + ~singletons` prims. That's usually fine. Two variants tighten things further when needed:

| Variant | What it does | When to use |
|---|---|---|
| `"allowSingleMeshes": true` | Folds singleton-per-boundary meshes into the merge flow as one-mesh "merge groups." Each singleton gets a merged copy created next to it; with `originalGeomOption: 1` (Delete), the originals participate and get cleaned up. | The proxy still has lots of leftover unmerged geometry after Step 3 and you want every mesh to flow through merge → decimate uniformly. |
| `"mergePoint": 7` (`eRootPrim`, "Root Prim") | Treats only root prims as merge boundaries, so the entire proxy subtree consolidates into a single merge group. | You want one mesh (or a small handful per material bucket) for the proxy and don't care about preserving the proxy's internal hierarchy. |
| `"mergePoint": 0` (`eDefault`, "Stage" / pseudo-root) | Even wider — the pseudo-root is the only boundary. Result: one merge group across the proxy. | Same intent as `7` above, useful when the proxy isn't directly under a root prim. |

Caveat: very small meshes (a few faces each) won't decimate below their topological minimum, so `reductionFactor: 5` on a proxy of many small singletons can still overshoot the target percentage even with `allowSingleMeshes: true`. If the actual reduction comes in noticeably higher than the matrix predicted, that's usually why — drop the reduction further, or accept the floor.

## Step 4 — Decimate with Topology Simplification

```json
{
  "operation": "decimateMeshes",
  "paths": ["/World/Asset_proxy", "/World/Asset_proxy//*"],
  "reductionFactor": 25.0,
  "allowCutAndGlue": true
}
```

Both entries are needed, because the proxy root can be either kind of prim. `//*` is an **SdfPathExpression** matching all *descendants* — required when the proxy root is a Scope/Xform, since a bare path would match only that non-Mesh prim and decimate would find nothing to do. The bare path is required when the source was itself a single Mesh, because then the proxy *is* the mesh and has no descendants to match. Listing both covers either shape; a mismatch is silent, with exit 0 and no log output at all. The `paths` argument accepts the full SdfPathExpression syntax (predicates like `{Mesh}`, glob patterns, etc.).

`allowCutAndGlue: true` is the **Topology Simplification** mode. The merged proxy mesh frequently has discontinuous topology inherited from the inputs, so cut-and-glue gives the decimator room to improve quality at aggressive reductions. It costs more time but is almost always worth it for proxy generation.

For picking `reductionFactor` vs `maxMeanError` (and concrete values), see `parameter-tuning.md`. The example value `25.0` is the matrix's general-purpose default; replace it with the row that matches your source's vertex count and intent.

## Step 5 — Set purposes

```python
from pxr import Usd, UsdGeom

SOURCE_PATH = "/World/Asset"
PROXY_PATH  = "/World/Asset_proxy"

def _set_purpose_subtree(root_path, purpose_token):
    """Set purpose on the root, then clear/override any descendant authored
    purpose opinions so inheritance actually flows. Setting only the root is
    sufficient when no descendant has an authored purpose attribute, but on
    real assets that's not safe to assume."""
    root = stage.GetPrimAtPath(root_path)
    if not root or not root.IsValid():
        raise RuntimeError("prim not found: " + root_path)
    UsdGeom.Imageable(root).GetPurposeAttr().Set(purpose_token)
    for prim in Usd.PrimRange(root):
        if prim == root:
            continue
        imageable = UsdGeom.Imageable(prim)
        if not imageable:
            continue
        attr = imageable.GetPurposeAttr()
        if attr.HasAuthoredValue():
            # Clearing the descendant opinion lets inheritance from the root flow through.
            attr.Clear()

_set_purpose_subtree(SOURCE_PATH, UsdGeom.Tokens.render)
_set_purpose_subtree(PROXY_PATH,  UsdGeom.Tokens.proxy)
```

The naive form (`...GetPurposeAttr().Set(...)` on the root only) works *if* no descendant has an authored `purpose` opinion — `purpose` is an inherited attribute, so children resolve their parent's value by default. But if any descendant has its own authored purpose (e.g. parts of the original asset were already tagged `proxy` or `guide`), those authored opinions will *not* be overridden by setting the parent. The subtree helper above clears descendant authored opinions so the root's purpose flows through cleanly.

After this step, USD-aware tooling (Hydra, usdview, Kit) will display the source for `render` purpose and the decimated mesh for `proxy` purpose.

## Step 6 — (Optional) Variant set that swaps purposes

When `setupVariantSet: true`, author a variant set on `variantSetParentPath` (default: parent of source) with two variants. The default variant is the standard pass; the swapped variant flips purposes so the proxy renders and the source acts as proxy.

```python
from pxr import UsdGeom

SOURCE_PATH        = "/World/Asset"
PROXY_PATH         = "/World/Asset_proxy"
VS_PARENT_PATH     = "/World"          # variantSetParentPath
VS_NAME            = "displayPurpose"  # variantSetName
DEFAULT_VARIANT    = "render"          # defaultVariantName
SWAPPED_VARIANT    = "proxy"           # swappedVariantName

vs_parent = stage.GetPrimAtPath(VS_PARENT_PATH)
if not vs_parent or not vs_parent.IsValid():
    raise RuntimeError("VariantSet parent prim not found: " + VS_PARENT_PATH)

vset = vs_parent.GetVariantSets().AddVariantSet(VS_NAME)

# Default variant: source=render, proxy=proxy
vset.AddVariant(DEFAULT_VARIANT)
vset.SetVariantSelection(DEFAULT_VARIANT)
with vset.GetVariantEditContext():
    UsdGeom.Imageable(stage.GetPrimAtPath(SOURCE_PATH)).GetPurposeAttr().Set(UsdGeom.Tokens.render)
    UsdGeom.Imageable(stage.GetPrimAtPath(PROXY_PATH)).GetPurposeAttr().Set(UsdGeom.Tokens.proxy)

# Swapped variant: source=proxy, proxy=render
vset.AddVariant(SWAPPED_VARIANT)
vset.SetVariantSelection(SWAPPED_VARIANT)
with vset.GetVariantEditContext():
    UsdGeom.Imageable(stage.GetPrimAtPath(SOURCE_PATH)).GetPurposeAttr().Set(UsdGeom.Tokens.proxy)
    UsdGeom.Imageable(stage.GetPrimAtPath(PROXY_PATH)).GetPurposeAttr().Set(UsdGeom.Tokens.render)

# Step 5 authored `purpose` directly on these two prims, and a local opinion
# outranks a variant one: left in place, it makes the variant switch a silent no-op.
# Clear both now that the variants carry them.
# Step 5's descendant clears still stand, so each variant's value inherits down the subtree.
for _path in (SOURCE_PATH, PROXY_PATH):
    UsdGeom.Imageable(stage.GetPrimAtPath(_path)).GetPurposeAttr().Clear()

vset.SetVariantSelection(DEFAULT_VARIANT)
```

`VS_PARENT_PATH` must be an ancestor of both source and proxy (otherwise the variant body can't author opinions on them). The default `parent of source` is the natural choice for a sibling proxy. Setting it elsewhere produces a variant that cannot reach the prims it is meant to swap.

## Putting it together (decimate mode)

The `pythonScript` operation expects its `python` argument as base64 — when
assembling the JSON, encode each script body. The placeholders below show the
shape only; never run a config that still contains `<base64 ...>` text.

```json
[
  {"operation": "pythonScript",   "python": "<base64 of step 1>"},
  {"operation": "pythonScript",   "python": "<base64 of step 2>"},
  {"operation": "merge",          "meshPrimPaths": ["/World/Asset_proxy"], "considerMaterials": false, "originalGeomOption": 1, "mergePoint": 1, "rootPath": "MergedProxy"},
  {"operation": "decimateMeshes", "paths": ["/World/Asset_proxy", "/World/Asset_proxy//*"], "reductionFactor": 25.0, "allowCutAndGlue": true},
  {"operation": "printStats"},
  {"operation": "pythonScript",   "python": "<base64 of step 5>"},
  {"operation": "pythonScript",   "python": "<base64 of step 6>"}
]
```

The `printStats` step right after `decimateMeshes` is the runtime verification half of the analysis loop — its output in the Usd Optimize log lets you (or the agent) confirm the post-decimation vertex count matches what the matrix predicted from the agent-side pre-analysis. See `parameter-tuning.md`.

Drop step 2 when `stripAnimation: false`. Drop step 6 when `setupVariantSet:
false`. Omit disabled optional steps entirely rather than inserting empty
placeholder scripts. The `printStats` step can be dropped if you don't need
verification, but it's cheap.

To base64-encode each script body:

- Linux / macOS / bash: `python3 -c "import base64,sys; print(base64.b64encode(sys.stdin.read().encode()).decode())" < step1.py`
- Windows / PowerShell: `[Convert]::ToBase64String([Text.Encoding]::UTF8.GetBytes((Get-Content -Raw step1.py)))`

When using `pythonScript` from the Kit UI's code editor, encoding is transparent — paste plain Python.

## End-to-end runner

For end-to-end mode (input USD + output USD + an Usd Optimize runtime), the standard runner is `UsdOptimizeCore.executeConfig`:

```python
from pxr import Usd
from usd_optimize.core import ExecutionContext, UsdOptimizeCore

stage = Usd.Stage.Open(INPUT_USD)
config = [...]   # the assembled list from "Putting it together" above

context = ExecutionContext()
context.set_stage(stage)
results = UsdOptimizeCore.getInstance().executeConfig(context, config)
if not all(ok for ok, _err, _out in results):
    raise RuntimeError("pipeline failed -- check Usd Optimize log")

stage.GetRootLayer().Export(OUTPUT_USD)   # writes a new file; INPUT_USD is left untouched
```

Two important properties:

- The runner mutates the stage in memory. As long as you don't call `stage.Save()` or `stage.GetEditTarget().GetLayer().Save()`, the input USD on disk is untouched. `stage.GetRootLayer().Export(OUTPUT_USD)` writes the modified stage to a new file.
- **Use `GetRootLayer().Export()`, not `stage.Export()`.** `stage.Export()` writes the *flattened composed* stage, which resolves variant selections and discards the variant set itself — so with `setupVariantSet: true` the Step 6 variant set is silently missing from the output.
- `pythonScript` script bodies passed inside the config must be base64-encoded (see encoding commands above).

The Python environment for `executeConfig` requires Usd Optimize on `PYTHONPATH` and its native libs on `PATH` — defer to `.agents/skills/build/SKILL.md` for a source-tree build, or to a prebuilt-package install skill / repo install docs for a packaged runtime. Do not duplicate environment setup here.
