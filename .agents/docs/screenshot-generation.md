---
name: screenshot-generation
description: "Screenshot generation approaches for Scene Optimizer tuning sessions and batch validation."
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Screenshot Generation for Scene Optimizer

## When You Need This

- **/tune-parameters sessions:** before/after screenshots to diagnose parameter changes
- **Batch validation:** verifying operation results programmatically
- **Automated pipelines:** rendering previews without a human in the loop

## Quick Decision

| Approach | Install | GPU | Quality | Speed (cached) | Wireframe | Status |
|---|---|---|---|---|---|---|
| ovrtx | `pip install ovrtx` or local build | Required | RTX | ~5s | Edge-detect or composite | Recommended |
| usdrecord | Standalone OpenUSD | Optional | Storm | ~3s | No (FrameRecorder lacks drawMode) | Proven fallback |
| Storm wireframe | Standalone OpenUSD + PySide6 | Optional | Storm | ~3s | Native (DRAW_WIREFRAME) | Working |
| matplotlib wireframe | pxr + matplotlib | No | Lines only | ~10s | Native (projected edges) | Working |
| **usdview (interactive)** | Full OpenUSD | Optional | Storm | N/A (interactive) | Via draw mode menu | 3D inspection (viewer-only) |

**Recommendation:** Use ovrtx when available — fastest RTX-quality path. Fall back to usdrecord for shaded, or the custom Storm wireframe script for wireframe views. For **interactive** workflows, see [Interactive Viewing](#interactive-viewing).

## Availability Probes

Run these checks in order to determine which approach to use. Do not assume any tool is available — probe first.

```bash
# 1. ovrtx
python -c "import ovrtx; print('ok')" 2>/dev/null || python3 -c "import ovrtx; print('ok')" 2>/dev/null

# 2. usdrecord (standalone OpenUSD install)
which usdrecord 2>/dev/null || where usdrecord 2>/dev/null
```

Use the first approach that succeeds. If none are available, tell the user what to install.

## Opening Screenshots (Platform)

- **Windows:** `powershell.exe -Command "Invoke-Item '<path>'"` — do NOT use `cmd.exe /c start`, it does not reliably open files from Git Bash.
- **Linux/macOS:** `xdg-open <path>` (Linux) or `open <path>` (macOS).

---

## Prerequisites: Camera and RenderProduct

Most USD scenes from Scene Optimizer do **not** contain a Camera or RenderProduct. You must check and inject one if missing.

### Check for existing camera

In the USD inspection script (Step 3 of /tune-parameters), add:

```python
cameras = [p for p in stage.Traverse() if p.IsA(UsdGeom.Camera)]
```

If `cameras` is empty, use auto-camera positioning (see below).

---

## Approach 1: ovrtx (Recommended)

ovrtx is a lightweight C/Python SDK for Omniverse RTX rendering. It renders a frame in ~10 lines of Python without needing Kit.

### Install

```bash
pip install ovrtx pillow
# or
uv add ovrtx pillow
```

### Environment setup (this machine)

ovrtx bundles its own USD and **conflicts with standalone `pxr`** (usd-core). On this machine:

1. **Clear PYTHONPATH** — removes `C:\USD_2405\lib\python` which exposes the conflicting `pxr`
2. **Add ovrtx native dirs to PATH** — Carbonite plugin loader needs `ovrtx/bin`, `ovrtx/bin/plugins`, and `ovrtx/bin/plugins/rtx`
3. **Use the ovrtx venv Python** — the venv at `D:/ovrtx/examples/python/minimal/.venv/` has ovrtx + Pillow pre-installed

**Platform-specific environment setup:**

#### Windows (Git Bash)

- **Use backslash Windows paths with semicolons**, not forward-slash Unix paths with colons. Git Bash mangles paths like `D:/ovrtx/...` to `C:\Program Files\Git\ovrtx\...` when they look like relative paths. This causes `CRenderApi` load failures.
- **Filter out conflicting USD installations** from PATH. `C:\USD_2405\bin` and `C:\USD_2405\lib` contain 76+ DLLs (USD, MaterialX, TBB) that conflict with ovrtx's bundled versions. Windows DLL search order loads these before ovrtx's copies, causing ABI mismatches.

```bash
# Correct: backslash paths + semicolons + filter USD_2405 conflicts
OVRTX_BIN="D:\\ovrtx\\examples\\python\\minimal\\.venv\\Lib\\site-packages\\ovrtx\\bin"
export PYTHONPATH=""
export PATH="$OVRTX_BIN;$OVRTX_BIN\\plugins;$OVRTX_BIN\\plugins\\rtx;$PATH"
D:/ovrtx/examples/python/minimal/.venv/Scripts/python.exe <script.py>
```

```bash
# WRONG — Git Bash mangles these to C:\Program Files\Git\ovrtx\...
OVRTX_PKG="D:/ovrtx/examples/python/minimal/.venv/Lib/site-packages/ovrtx"
PATH="$OVRTX_PKG/bin:$OVRTX_PKG/bin/plugins:$OVRTX_PKG/bin/plugins/rtx:$PATH"
```

If you still get `CRenderApi` failures, filter out `USD_2405` from PATH entirely:
```bash
export PATH=$(echo "$PATH" | tr ':' '\n' | grep -v "USD_2405" | tr '\n' ':' | sed 's/:$//')
```

#### Linux / macOS

- **Use `LD_LIBRARY_PATH`** (not `PATH`) — ovrtx shared libraries (`.so`) are loaded via the dynamic linker, not the shell PATH.
- **Clear `PYTHONPATH`** — prevents standalone `pxr` from conflicting with ovrtx's bundled USD.
- **Use the ovrtx venv Python** — ensures ovrtx + Pillow are importable.

```bash
OVRTX_BIN="$HOME/ovrtx/examples/python/minimal/.venv/lib/python3.10/site-packages/ovrtx/bin"
export PYTHONPATH=""
export LD_LIBRARY_PATH="$OVRTX_BIN:$OVRTX_BIN/plugins:$OVRTX_BIN/plugins/rtx"
~/ovrtx/examples/python/minimal/.venv/bin/python <script.py>
```

**Important:** do NOT import `pxr` in the same process as ovrtx. Compute camera positions via a separate `pxr`-based script (the USD inspection step), then pass pre-computed values to the ovrtx script.

### Render script

Write this to a temp file (e.g., `_tmp_screenshot.py`). Camera position must be pre-computed (see Auto-Camera Positioning section).

```python
import sys
import ovrtx
from PIL import Image

scene_path = "<input.usd>"
output_path = "<screenshot.png>"
cam_pos = (<cx>, <cy>, <cz>)  # pre-computed from bbox
resolution = (1920, 1080)

renderer = ovrtx.Renderer()

# Inline USDA layer: injects camera + DomeLight + RenderProduct.
# Use string concatenation (not f-strings) to avoid brace escaping issues.
renderer.add_usd_layer('''
#usda 1.0
(subLayers = [@''' + scene_path + '''@])

def Camera "AutoCamera" {
    float focalLength = 24
    double3 xformOp:translate = ''' + str(cam_pos) + '''
    token[] xformOpOrder = ["xformOp:translate"]
}

def DomeLight "AutoDomeLight" {
    float inputs:intensity = 1000
}

def "Render" {
    def RenderProduct "Camera" {
        rel camera = </AutoCamera>
        int2 resolution = ''' + str(resolution) + '''
        rel orderedVars = [<LdrColor>]
        def RenderVar "LdrColor" {
            string sourceName = "LdrColor"
        }
    }
}
''')

products = renderer.step(render_products={"/Render/Camera"}, delta_time=1/60)
for _, product in products.items():
    for frame in product.frames:
        with frame.render_vars["LdrColor"].map(device=ovrtx.Device.CPU) as var:
            Image.fromarray(var.tensor.numpy()).save(output_path)

print(f"Screenshot saved to {output_path}", file=sys.stderr)
```

### Notes

- First run compiles RTX shaders — expect ~10s delay. Subsequent runs are fast (~5s).
- The inline USDA layer approach lets you inject camera + RenderProduct without modifying the original USD file.
- `Pillow` (`PIL`) is required for saving the image.
- **Do not use f-strings** for the USDA layer — curly braces in USDA conflict with Python f-string syntax. Use string concatenation instead.
- ovrtx **cannot coexist with `pxr`** in the same Python process. Run camera computation and ovrtx rendering as separate steps.

---

## Approach 2: usdrecord (Standalone OpenUSD)

Part of the OpenUSD toolset. Uses the Storm (OpenGL) renderer.

### Probe

```bash
which usdrecord || where usdrecord
```

### Usage

If the scene has no camera (or no lights), use a **session layer** to inject them. This is preferred over a sublayer wrapper because scenes with `RenderSettings` prims can interfere with camera discovery in the sublayer approach.

**Session layer file** (e.g., `_tmp_session_layer.usda`):
```usda
#usda 1.0

def Camera "AutoCamera" {
    float focalLength = 24
    double3 xformOp:translate = (<cx>, <cy>, <cz>)
    token[] xformOpOrder = ["xformOp:translate"]
}

def DomeLight "AutoDomeLight" {
    float inputs:intensity = 1000
}
```

Then render:

```bash
python <path-to-usdrecord> --purposes render --sessionLayer <session_layer.usda> --camera AutoCamera --imageWidth 1920 <input.usd> <output.png>
```

### Notes

- **`--purposes render` is the default.** Always pass `--purposes render` unless the user explicitly asks for proxy geometry.
- **`--camera AutoCamera` (name only), NOT `--camera /AutoCamera` (full path).** When using `--sessionLayer`, the full path form silently fails (empty `Camera:` in output, renders with default framing). The name-only form works correctly.
- **Use `--sessionLayer`, NOT a sublayer wrapper USDA.** Scenes with `RenderSettings` prims (e.g., from Omniverse authoring) cause the wrapper approach to fail — the camera is not found. Session layers avoid this conflict.
- usdrecord uses a default camera if the `--camera` path is invalid — you'll get a render, just not from the intended viewpoint.
- Storm quality is lower than RTX but sufficient for parameter tuning diagnostics.

---

## Auto-Camera Positioning

When the scene has no camera, compute placement from the bounding box (already extracted in Step 3 of /tune-parameters).

### Compute camera position

```python
import math
from pxr import Gf

# bbox_min and bbox_max from UsdGeom.BBoxCache
center = (bbox_min + bbox_max) / 2.0
size = bbox_max - bbox_min
diagonal = math.sqrt(size[0]**2 + size[1]**2 + size[2]**2)
distance = diagonal * 1.5

# Place camera based on stage up-axis
up_axis = UsdGeom.GetStageUpAxis(stage)  # "Y" or "Z"
if up_axis == "Y":
    camera_pos = (center[0], center[1], center[2] + distance)
    camera_rot = None  # Camera looks along -Z by default, which points at center
else:  # Z-up
    camera_pos = (center[0], center[1] - distance, center[2])
    camera_rot = (-90, 0, 0)  # Rotate camera to look along +Y toward center
```

### Inject as inline USDA (for ovrtx or usdrecord wrapper)

Replace the `AutoCamera` def in the ovrtx script or usdrecord wrapper USDA with computed values:

```usda
def Camera "AutoCamera" {
    float focalLength = 24
    double3 xformOp:translate = (<cx>, <cy>, <cz>)
    token[] xformOpOrder = ["xformOp:translate"]
}
```

For Z-up scenes, add a rotation to aim the camera at the center:

```usda
def Camera "AutoCamera" {
    float focalLength = 24
    double3 xformOp:translate = (<cx>, <cy>, <cz>)
    float3 xformOp:rotateXYZ = (-90, 0, 0)
    token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]
}
```

### Focal length heuristic

For very large or very small scenes, adjust focal length to keep the subject well-framed:

- Default: `focalLength = 24` (wide angle, good for most scenes)
- Tight framing: `focalLength = 50` with `distance = diagonal * 2.5`

---

## Naming Convention

Screenshots generated during /tune-parameters sessions should follow this pattern:

```
<output_dir>/screenshot_<operation>_v<iteration>.png
```

Example: `D:/scene-optimizer/_output/screenshot_shrinkwrap_v3.png`

---

## Wireframe Rendering

Wireframe views are useful for parameter tuning (seeing mesh density, triangle structure, edge flow). None of the standard tools (usdrecord, ovrtx) have a built-in wireframe flag, so custom approaches are needed.

### Approach comparison

| Method | True mesh edges | Depth occlusion | Shaded surface | RTX quality | Requires |
|---|---|---|---|---|---|
| **ovrtx two-pass composite** | Yes | Yes | Yes | Yes (RTX) | ovrtx (two subprocesses) |
| **ovrtx native wireframe** | Yes | Yes | No (wireframe only or flat) | Yes (RTX) | ovrtx (config modification) |
| Storm `UsdImagingGLEngine` | Yes | Yes | Yes (`WIREFRAME_ON_SURFACE`) | No (Storm) | PySide6 + pxr |
| matplotlib projected edges | Yes | No (see-through) | No | No | pxr + matplotlib |
| ovrtx edge detection | No (silhouette/crease only) | Yes | Optional (composite) | Yes | ovrtx |
| ovrtx + pxr depth composite | Yes | Yes | Yes | Yes | ovrtx + pxr (two processes) |

### ovrtx native wireframe (recommended, RTX quality)

ovrtx uses Carbonite settings in `ovrtx.config.json` (located at `<ovrtx_pkg>/bin/ovrtx.config.json`) to control wireframe mode.

**Settings to add to `ovrtx.config.json`:**
```json
{
    "rtx": {
        "wireframe": {
            "mode": 2,
            "globalWireframeThickness": 1.5
        },
        "wireframeColor": [0.0, 0.0, 0.0, 1.0],
        "debugMaterialType": 0
    }
}
```

**Setting reference:**

| Setting path | Type | Values |
|---|---|---|
| `/rtx/wireframe/mode` | int | `0` = off, `1` = wireframe only, `2` = wireframe-on-surface |
| `/rtx/wireframe/globalWireframeThickness` | float | Line thickness in pixels (default 1.5) |
| `/rtx/wireframeColor` | float[4] | RGBA color (note: flat key, not nested under `wireframe/`) |
| `/rtx/debugMaterialType` | int | `0` = flat shading (for mode 2), `-1` = normal materials |

**Usage flow:**
1. Back up the original `ovrtx.config.json`
2. Add the wireframe settings to the JSON
3. Render normally — wireframe overlay appears automatically in `LdrColor`
4. Restore the original config afterward

**Notes:**
- Mode 1 shows wireframe only (colored by material). Mode 2 shows wireframe-on-flat (forces flat gray shading, NOT true wireframe-on-shaded).
- **Mode 2 + `debugMaterialType: 0`** is for standalone flat wireframe views only. Do NOT use `debugMaterialType: 0` in configs intended for normal shaded rendering — it breaks path tracing (makes geometry transparent instead of showing the red fallback for failed MDL).
- **For the two-pass wireframe-on-shaded approach, use mode 1** (not mode 2). Mode 1 produces edges that can be composited onto the shaded pass. Mode 2's flat shading doesn't add value over the shaded pass.
- The config file must be valid JSON — the original `ovrtx.config.json` has trailing commas (non-standard). Write a clean version when modifying.
- **Carbonite locks settings after first Renderer init.** You cannot change wireframe mode between renders in the same process. Use separate subprocesses for multi-pass workflows (see two-pass approach below).
- **Always restore the original config** after rendering — use try/finally. A leftover `debugMaterialType: 0` or wireframe mode in the config will break subsequent shaded renders.

### ovrtx two-pass wireframe-on-shaded (RTX quality, best result)

True wireframe-on-shaded requires two separate ovrtx processes (shaded + wireframe) plus a compositing step. This is necessary because:
1. RTX mode 2 forces flat shading — it doesn't overlay wireframe on the actual shaded surface
2. Carbonite settings are locked after first init — can't switch modes in one process

**Pipeline:**
1. **Process 1:** Write config with `mode: 0` (normal), render shaded image
2. **Process 2:** Write config with `mode: 1` (wireframe), render wireframe image
3. **Composite (any Python):** Extract wireframe edges by comparing against background color, overlay onto shaded image

**Compositing strategy — multiply blend:**
- Wireframe render (mode 1) shows edges on a uniform gray background (~0.894)
- Use corner pixels as background reference, build an object mask from bg distance
- Normalize wireframe brightness by background (bg=1.0, darkest wire=0.0)
- Multiply-blend onto shaded: `shaded * wire_normalized` inside object area
- This works for both working materials (thin lines on gray) and failed MDL materials (colored surface with subtle wire transitions) — the multiply blend darkens proportionally to wireframe darkness without masking out the entire object

**Key gotcha — horizontal aperture:** When projecting pxr mesh edges to match ovrtx renders, use `SENSOR_WIDTH_MM = 20.955` (USD Camera default), not 36mm full-frame. Mismatch causes edges to cluster toward center.

See `.agents/scripts/rendering/ovrtx_two_pass_wireframe.py` for a full working implementation with orchestrator (spawns subprocesses automatically).

### Storm wireframe (best contrast, no GPU needed)

Bypasses `usdrecord` and uses `UsdImagingGLEngine` directly with `DRAW_WIREFRAME` or `DRAW_WIREFRAME_ON_SURFACE` draw mode. Requires PySide6 for the offscreen OpenGL context.

**Key API:**
```python
from pxr import UsdImagingGL

params = UsdImagingGL.RenderParams()
params.drawMode = UsdImagingGL.DrawMode.DRAW_WIREFRAME          # pure wireframe
# or: params.drawMode = UsdImagingGL.DrawMode.DRAW_WIREFRAME_ON_SURFACE  # shaded + wireframe overlay
params.wireframeColor = Gf.Vec4f(1.0, 1.0, 1.0, 1.0)  # white on black bg
params.enableLighting = False
params.enableSceneMaterials = False
```

**Available draw modes:** `DRAW_POINTS`, `DRAW_WIREFRAME`, `DRAW_WIREFRAME_ON_SURFACE`, `DRAW_SHADED_FLAT`, `DRAW_SHADED_SMOOTH`, `DRAW_GEOM_ONLY`, `DRAW_GEOM_FLAT`, `DRAW_GEOM_SMOOTH`.

**Important render params:**
- `params.showProxy = False` — **critical.** Proxy purpose meshes (often 8-point bounding boxes) occlude detailed render geometry in wireframe-on-surface mode.
- `params.showRender = True` — include render purpose geometry.

**Note:** `usdrecord`'s `FrameRecorder` does NOT expose `drawMode` — this is why a custom script is needed. The `--sessionLayer`, `--renderSettingsPrimPath` flags cannot set wireframe either (UsdRender.Settings has no draw mode attribute).

See `.agents/scripts/rendering/storm_wireframe.py` for a full working implementation using PySide6 `QOffscreenSurface` + FBO rendering.

### matplotlib wireframe (no GPU needed)

Extracts mesh triangle edges via pxr, projects to 2D with perspective, renders with `LineCollection`. Good for environments without GPU access.

**Key details:**
- Fan-triangulate faces: for each face with N vertices, create N-2 triangles as `(v0, v1, v2), (v0, v2, v3), ...`
- Use `UsdGeom.XformCache` to transform points to world space
- Project using perspective: `screen_x = (x_cam / z_cam) * focal_px + width/2`
- With dense meshes (>500K edges), downsample via random sampling
- No depth occlusion — all edges visible (see-through effect)

See `.agents/scripts/rendering/` for working implementations. The matplotlib approach follows the same edge extraction as `pxr_edge_composite.py` but renders with `matplotlib.collections.LineCollection` instead of depth-testing.

### ovrtx edge detection (silhouette/crease edges)

Uses ovrtx's `NormalSD` and `DistanceToCameraSD` AOVs with Sobel edge detection. Produces silhouette and sharp-crease edges only — NOT true mesh wireframe. Useful for showing BREP patch boundaries and surface discontinuities.

**AOV setup** — add to the RenderProduct in the inline USDA:
```usda
rel orderedVars = [<LdrColor>, <NormalSD>, <DistanceToCameraSD>]
def RenderVar "NormalSD" { string sourceName = "NormalSD" }
def RenderVar "DistanceToCameraSD" { string sourceName = "DistanceToCameraSD" }
```

**Data format:** Both AOVs arrive as `uint32` (bit-packed `float32`). Reinterpret with `data.view(np.float32)` before processing. NormalSD range is `[-1, 1]`, DistanceToCameraSD is distance in world units (with `inf` for background).

See `.agents/scripts/rendering/ovrtx_depth_aovs.py` for the AOV rendering step.

### ovrtx + pxr composite (best quality wireframe)

Combines ovrtx RTX render (color + depth) with pxr-projected mesh edges, using depth testing for proper occlusion. This gives RTX lighting quality with true mesh wireframe — best of both worlds.

**Two-process pipeline** (ovrtx and pxr cannot coexist):
1. **ovrtx process:** render LdrColor + DistanceToCameraSD, save color as PNG and depth as `.npy`
2. **pxr process:** extract triangle edges, project to 2D, depth-test against ovrtx depth buffer, composite onto color image

**Critical: camera projection must match.** USD Camera default `horizontalAperture = 20.955mm`. If the pxr projection assumes 36mm (full-frame), edges will be misaligned (concentrated toward center). Use `SENSOR_WIDTH_MM = 20.955` to match the USD Camera default.

**DistanceToCameraSD units:** Reports Euclidean distance in meters regardless of scene units. Set `metersPerUnit` on the ovrtx root layer to match the sublayer, and convert scene-unit distances to meters for depth comparison.

See `.agents/scripts/rendering/ovrtx_depth_aovs.py` (AOV render) and `.agents/scripts/rendering/pxr_edge_composite.py` (edge projection + depth-tested composite) for full working implementations.

---

## Interactive Viewing

For interactive parameter tuning, two approaches allow real-time 3D inspection instead of (or in addition to) headless screenshots.

### usdview (Standalone USD Viewer)

Launches the OpenUSD viewer for 3D inspection. Viewer-only — no parameter editing UI. Run batch optimization separately, then inspect the result.

- **Requires:** Full OpenUSD build (pip `usd-core` does NOT include usdview)
- **Renderer:** Storm (OpenGL)
- **No auto-reload:** must File > Reopen Stage after re-running batch
- **Display purpose:** usdview shows all purposes by default. Set via **Display > Display Purpose** — uncheck **Proxy** and **Guide** to show only render geometry.
- **Platform notes:** On Windows, always launch via `python <usdview_path>` to avoid hardcoded shebang issues

See `.agents/skills/tune-parameters/SKILL.md` → [usdview Viewer Launch] for probe logic and cross-platform launch commands.