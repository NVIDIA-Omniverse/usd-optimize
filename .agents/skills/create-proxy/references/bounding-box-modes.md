<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# create-proxy — Bounding-box modes

When `proxyMode: "boundingBoxWhole"` or `"boundingBoxPerMesh"`, the entire merge+decimate flow (and the deep-copy that drives it) is replaced with a single `pythonScript` that:

1. Defines `<source>_proxy` as a fresh `Xform` with a parent-world-inverse `xformOp` so world-coord vertices author below it render at the right place even if the parent has a non-identity transform.
2. Computes one or more world-space AABBs from the source via `UsdGeom.BBoxCache`.
3. Authors box mesh(es) using a standard 8-vertex / 6-quad cube topology with **`subdivisionScheme = "none"`** and **`faceVarying` per-face normals**. Without these two, USD's default Catmull-Clark subdivision rounds the cube into a blob and shading goes wrong at corners.

## Common scaffolding

Both bbox modes share this setup. The script body for either mode prepends this:

```python
from pxr import Sdf, Usd, UsdGeom, Gf, Vt

SOURCE_PATH = "/World/Asset"
PROXY_PATH  = SOURCE_PATH + "_proxy"

src = stage.GetPrimAtPath(SOURCE_PATH)
if not src or not src.IsValid():
    raise RuntimeError("source not found: " + SOURCE_PATH)

# Define proxy Xform with parent-world-inverse so world-coord verts render correctly.
proxy = UsdGeom.Xform.Define(stage, PROXY_PATH)
xcache = UsdGeom.XformCache(Usd.TimeCode.Default())
parent = stage.GetPrimAtPath(Sdf.Path(PROXY_PATH).GetParentPath())
parent_w2l = xcache.GetLocalToWorldTransform(parent).GetInverse()
xformable = UsdGeom.Xformable(proxy)
xformable.ClearXformOpOrder()
xformable.AddTransformOp().Set(Gf.Matrix4d(parent_w2l))

# Topology + per-face flat normals shared by every AABB box authored below.
_BOX_FACE_COUNTS  = Vt.IntArray([4]*6)
_BOX_FACE_INDICES = Vt.IntArray([
    0,3,2,1, 4,5,6,7,
    0,1,5,4, 1,2,6,5,
    2,3,7,6, 3,0,4,7,
])
_BOX_NORMALS = Vt.Vec3fArray(
    [Gf.Vec3f( 0, 0,-1)]*4 + [Gf.Vec3f( 0, 0, 1)]*4 +
    [Gf.Vec3f( 0,-1, 0)]*4 + [Gf.Vec3f( 1, 0, 0)]*4 +
    [Gf.Vec3f( 0, 1, 0)]*4 + [Gf.Vec3f(-1, 0, 0)]*4
)

def _author_aabb_box(stage, prim_path, mn, mx):
    mesh = UsdGeom.Mesh.Define(stage, prim_path)
    mesh.CreatePointsAttr(Vt.Vec3fArray([
        Gf.Vec3f(mn[0], mn[1], mn[2]), Gf.Vec3f(mx[0], mn[1], mn[2]),
        Gf.Vec3f(mx[0], mx[1], mn[2]), Gf.Vec3f(mn[0], mx[1], mn[2]),
        Gf.Vec3f(mn[0], mn[1], mx[2]), Gf.Vec3f(mx[0], mn[1], mx[2]),
        Gf.Vec3f(mx[0], mx[1], mx[2]), Gf.Vec3f(mn[0], mx[1], mx[2]),
    ]))
    mesh.CreateFaceVertexCountsAttr(_BOX_FACE_COUNTS)
    mesh.CreateFaceVertexIndicesAttr(_BOX_FACE_INDICES)
    mesh.CreateExtentAttr(Vt.Vec3fArray([Gf.Vec3f(*mn), Gf.Vec3f(*mx)]))
    mesh.CreateSubdivisionSchemeAttr().Set(UsdGeom.Tokens.none)
    mesh.CreateNormalsAttr(_BOX_NORMALS)
    mesh.SetNormalsInterpolation(UsdGeom.Tokens.faceVarying)

bcache = UsdGeom.BBoxCache(Usd.TimeCode.Default(),
                           [UsdGeom.Tokens.default_, UsdGeom.Tokens.render])
```

## Mode A — `boundingBoxWhole`

```python
aabb = bcache.ComputeWorldBound(src).ComputeAlignedBox()
_author_aabb_box(stage, PROXY_PATH + "/BoundingBox", aabb.GetMin(), aabb.GetMax())
```

One 8-vert box at `<source>_proxy/BoundingBox`. Constant cost regardless of source complexity. Best fit for occluder, picking, far-far LOD.

## Mode B — `boundingBoxPerMesh`

```python
for prim in Usd.PrimRange(src):
    if not prim.IsA(UsdGeom.Mesh):
        continue
    aabb = bcache.ComputeWorldBound(prim).ComputeAlignedBox()
    if aabb.IsEmpty():
        continue
    rel = str(prim.GetPath())[len(SOURCE_PATH):]
    _author_aabb_box(stage, PROXY_PATH + rel, aabb.GetMin(), aabb.GetMax())
```

One box per leaf mesh under the source, mirrored at the same relative path under the proxy. Prim count == source mesh count; vertex count == `8 × source mesh count`. Preserves the asset's silhouette as a "Lego" approximation.

## Caveats

- **Per-mesh AABBs are slightly looser than the whole-subtree AABB.** Each mesh's world-space AABB compounds rotation padding from its parent chain, so the union of per-mesh AABBs can be a few percent larger in each axis than `ComputeWorldBound(source)`. Usually invisible on real assets; do not rely on per-mesh boxes as a tight collision/culling primitive.
- **No source data is copied.** Bbox modes don't run the deep-copy step, so `stripAnimation` is a no-op and the source is left exactly as authored.
- **Hierarchy depth in `boundingBoxPerMesh`.** Mirroring the source's prim hierarchy creates one intermediate `Xform` per ancestor. For very deep hierarchies that can be a lot of prims; if you want a flat proxy with all boxes at the proxy root, change the mirroring to `PROXY_PATH + f"/Box_{i}"` instead of `PROXY_PATH + rel`.
- **Determinism.** Output count and dimensions are fully determined by the source's AABB(s). The matrix and the iterate-with-user loop don't apply — there's nothing to tune.

## Putting it together (bbox mode)

The placeholders below show the shape only. Never run a config that still
contains `<base64 ...>` text; encode each real script body first and omit step
6 entirely when `setupVariantSet: false`.

```json
[
  {"operation": "pythonScript", "python": "<base64 of bbox-authoring script (scaffolding + Mode A or B body)>"},
  {"operation": "pythonScript", "python": "<base64 of step 5>"},
  {"operation": "pythonScript", "python": "<base64 of step 6>"}
]
```

The bbox-authoring script body is the *Common scaffolding* above with either the Mode A or Mode B loop appended. No merge, no decimate, no `printStats` — output is deterministic so there's nothing to verify against a prediction.

Steps 5 and 6 are identical to decimate mode — see `decimate-mode.md` § Step 5 / § Step 6 / § End-to-end runner.
