---
name: tune-parameters
description: Interactive parameter tuning for a Scene Optimizer operation. Use to iterate on op parameters, or to author a tuning guide.
version: "1.0.0"
allowed-tools: Read, Glob, Edit, Write, Bash
metadata:
  author: NVIDIA Corporation
  tags: [tuning, parameters, interactive]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# /tune-parameters — Interactive Operation Tuning

Operation guides live in `.agents/operations/`. Use the Glob tool to list them when needed.

You are a tuning assistant for Scene Optimizer operations. Follow this protocol.

---

## What this skill covers

Search this doc for keywords like `Tier 1`, `Tier 2`, `Tier 3`, `session log`, `screenshot`, `usdview`, `guide authoring`, `INVOCATION.md` to jump.

- **Step 0** — detect mode (tune vs. author a guide).
- **Step 1** — identify the operation (when no name given).
- **Step 2** — load operation knowledge (Tier 1 full guide, Tier 2 session logs, Tier 3 C++ source fallback).
- **Step 3** — gather context (USD inspection via `pxr` or fallback).
- **Step 4** — generate a starting config from the guide's Starting Configs.
- **Step 5** — iterate with the user (run, view, adjust).
- **Guide Authoring Mode** — for developers adding a new `.agents/operations/<key>.md`.
- **usdview Viewer Launch** — cross-platform usdview path probing.
- **Rules** — protocol invariants.

Companion docs: `.agents/operations/<key>.md` (per-operation guides), `.agents/operations/_template.md` (guide skeleton), `.agents/operations/INVOCATION.md` (how to actually run an operation), `.agents/operations/PIPELINES.md` (multi-op chains by bottleneck). Companion skills: `run-operations` (executes the tuned config), `interpret-validators` (decides which op to tune in the first place).

---

## Step 0: Detect mode

Check what the user is asking for:

- **"create a guide for X"** or **"author a guide"** → go to [Guide Authoring Mode](#guide-authoring-mode)
- **Operation name provided** → go to Step 2
- **No operation specified or user is unsure** → go to Step 1

---

## Step 1: Identify the operation

Read `.agents/operations/INDEX.md` and help the user pick an operation. The index lists operations sorted by argument count — operations with more arguments generally benefit more from guided tuning.

If the user has a goal rather than a specific operation (e.g., "reduce polycount", "close holes"), consult the operation guides in `.agents/operations/` and `PLUGINS.md` to recommend the right operation for their goal.

---

## Step 2: Load operation knowledge

Use a three-tier fallback.

### Tier 1 — Full guide (best experience)

Guide exists at `.agents/operations/<operation>.md`:

1. Read the guide
2. Check `.agents/docs/sessions/<operation>.md` for session logs
3. Proceed to Step 3

### Tier 2 — Session logs (medium experience)

No guide, but `.agents/docs/sessions/<operation>.md` exists:

1. Read the session logs
2. Tell the user: *"There's no tuning guide for this operation, but I have session logs from previous tuning runs. I can provide real-world configs and known pitfalls, but tuning order and visual diagnosis will be best-effort."*
3. Proceed to Step 3

### Tier 3 — C++ source only (baseline)

No guide and no session logs:

1. Read `source/operations/<operation>/` — extract `addArgument()` calls to build a parameter table
2. Tell the user: *"There's no tuning guide or session logs for this operation. I'm working from the C++ source. Parameter descriptions are accurate, but tuning order and visual diagnosis are best-effort. Consider creating a guide with `/tune-parameters create a guide for <operation>`."*
3. Proceed to Step 3

---

## Step 3: Gather context

Ask the user for:
1. **Input/output USD paths**
2. **Screenshot** if available — the right kind depends on the operation:
   - **Most geometry operations** (shrinkwrap, decimateMeshes, meshCleanup, fitPrimitives, etc.): screenshot of the **3D model** in the viewport. An input screenshot is useful even before running.
   - **generateAtlasUVs:** screenshot of the **UV atlas** (2D UV layout), not the 3D mesh. A UV editor view or checkerboard texture render works well. Don't ask for this upfront — the UV layout is the output of the operation and won't exist yet on the first pass. Ask for it starting from Step 5.
   - If the operation guide specifies a screenshot type, follow that.
   - If the operation guide specifies **prerequisites** (e.g., merge before shrinkwrap), include them in the config automatically and explain why.
3. **Goal** — close holes, reduce polycount, simplify, etc.

**If the user provides a USD file path**, auto-inspect it to extract scene units, bounding box, and prim hierarchy — do not ask the user for these manually.

### USD file inspection

Use a 3-tier approach to inspect the USD file. Try each tier in order and use the first one that works.

#### Tier 1 — Standalone pxr (preferred, fastest)

**Probe:** run `python -c "from pxr import Usd; print('ok')"` (Windows) or `python3 -c "from pxr import Usd; print('ok')"` (Linux/macOS). If it prints `ok`, standalone pxr is available.

**Write a temp script** to `./_tmp_usd_traverse.py` in the current working directory:

```python
from pxr import Usd, UsdGeom

stage = Usd.Stage.Open('<input.usdc>')
lines = []
lines.append(f'metersPerUnit: {UsdGeom.GetStageMetersPerUnit(stage)}')
lines.append(f'upAxis: {UsdGeom.GetStageUpAxis(stage)}')
bb = UsdGeom.BBoxCache(Usd.TimeCode.Default(), [UsdGeom.Tokens.default_])
rng = bb.ComputeWorldBound(stage.GetPseudoRoot()).GetRange()
lines.append(f'bbox size: {rng.GetMax() - rng.GetMin()}')
lines.append('Top-level prims:')
for p in stage.GetPseudoRoot().GetChildren():
    lines.append(f'  {p.GetPath()} ({p.GetTypeName()})')
lines.append('All Mesh prims:')
for p in stage.Traverse():
    if p.GetTypeName() == 'Mesh':
        lines.append(f'  {p.GetPath()}')
print('\n'.join(lines))
```

**Run:** `python _tmp_usd_traverse.py` (Windows) or `python3 _tmp_usd_traverse.py` (Linux/macOS). Read output directly from bash stdout.

#### Tier 2 — Neither available

If the standalone pxr probe fails, tell the user:

*"USD inspection requires `usd-core` (`pip install usd-core`). Install it for the fastest experience."*

Skip to Step 4 with whatever context the user can provide manually.

#### Interpreting results

Use `metersPerUnit` to determine scene units (0.001 = mm, 0.01 = cm, 1.0 = m) and scale world-space parameters accordingly.

### Screenshot generation

After inspecting the USD file, offer to generate a rendered screenshot:

*"I can generate a rendered preview of the scene. By default I'll auto-position a camera based on the bounding box. Would you like:*
- *Auto-camera or specify a camera prim?*
- *Shaded render, wireframe, or wireframe-on-shaded?*
- *Skip the screenshot?"*

**Execution flow:**

1. **Camera choice:** auto-camera (default) vs user-specified camera prim vs skip
2. **Auto-camera:** compute position from bbox (1.5x diagonal distance, looking at bbox center, up-axis aligned)
3. **Probe for renderer** — check in order, use the first available (do not assume any tool is present):
   - **ovrtx:** `python3 -c "import ovrtx; print('ok')"` (Linux/macOS) or `python -c "import ovrtx; print('ok')"` (Windows)
   - **usdrecord:** `which usdrecord` (Linux/macOS) or `where usdrecord` (Windows)
   - If none found: tell the user to install `usd-core` (`pip install usd-core`) or `ovrtx`
4. **Render:** use the first available renderer. For usdrecord, always pass `--purposes render` by default (renders the full-detail geometry). Only use `--purposes proxy` if the user explicitly requests proxy geometry.
5. **Open:** `open <path>` (macOS), `xdg-open <path>` (Linux), `powershell.exe -Command "Invoke-Item '<path>'"` (Windows)
6. **Save to:** `<output_dir>/screenshot_<operation>_v<iteration>.png`
7. **Display:** read the image file to show the user

**Wireframe rendering:** If the user requests wireframe or wireframe-on-shaded, read `.agents/docs/screenshot-generation.md#wireframe-rendering` for the full approach comparison and implementation details. Quick summary:

- **Wireframe-on-shaded (best):** Storm `UsdImagingGLEngine` with `DRAW_WIREFRAME_ON_SURFACE` — single-pass, requires PySide6 + pxr
- **Wireframe-on-shaded (RTX):** ovrtx two-pass — render shaded (mode 0) + wireframe (mode 1) in **separate subprocesses** (Carbonite locks settings after first init), composite in Python
- **Wireframe only (RTX):** ovrtx native — modify `ovrtx.config.json` to set `/rtx/wireframe/mode` to 1, render, restore config
- **Wireframe only (no GPU):** matplotlib — extract triangle edges via pxr, project to 2D, render with `LineCollection`

**Reference scripts:** all rendering approaches have full working implementations committed at `.agents/scripts/rendering/`. Use these as templates — replace `REPLACE_WITH_*` placeholders with scene-specific values.

See `.agents/docs/screenshot-generation.md` for full implementation details, environment setup, and code snippets.

### Interactive viewing probes

After screenshot generation probes, also probe for interactive viewing tools. Cache these results for Steps 4–5.

**usdview:** Do NOT hardcode paths. usdview is only in full OpenUSD builds or system packages — pip `usd-core` does NOT include it.

Probe in order:
1. **System PATH:** `where usdview` (Windows) or `which usdview` (Linux/macOS)
2. **Derive from pxr:** `python -c "import pxr, pathlib; p = pathlib.Path(pxr.__file__).parent.parent.parent / 'bin' / 'usdview'; print(p) if p.exists() else print('')"` (Windows) or same with `python3` (Linux/macOS)
3. **Environment variable:** check `$USD_INSTALL_ROOT/bin/usdview` or `$USD_PATH/bin/usdview`
4. **Verify:** `python <usdview_path> --help 2>&1 | head -1` (Linux/macOS) or `python <usdview_path> --help 2>&1 | Select-Object -First 1` (PowerShell) — should print usage info

Cache: `usdview_available` (bool), `usdview_path` (path), `usdview_python` (the Python interpreter that can import pxr — use the same one to launch usdview)

---

## Step 4: Generate a starting config

Explain what the key parameters do in plain language (use the guide's Overview, or C++ source descriptions for Tier 2/3) so the user can iterate independently. Then pick a starting config from the guide (Tier 1), session logs (Tier 2), or construct a conservative default (Tier 3), and provide:

1. JSON config: `[{"operation": "<key>", "param1": value1, ...}]`
2. CLI command using the `sceneOptimizer` binary from the build output.

### Offer interactive mode

After presenting the CLI command, check the cached interactive viewing probes and offer:

**If usdview available:**
> *"After running the batch optimization, I can open the result in usdview for 3D inspection. Would you like that?"*

If user accepts: run the batch command first, then launch usdview on the output (see [usdview Viewer Launch](#usdview-viewer-launch)).

**If neither available:** proceed with headless batch + screenshots (current behavior).

---

## Step 5: Iterate

1. Ask for a **screenshot of the output** (same type as Step 3 — UV atlas for generateAtlasUVs, 3D viewport for most others). If the user provides output parameters but no screenshot, proactively offer: *"Would you like me to render a screenshot of the output to compare?"* (only when context makes it useful, e.g., user is describing visual symptoms without a reference image).
2. Diagnose using the guide's **Visual Diagnosis** table (Tier 1), session learnings (Tier 2), or general principles (Tier 3).
3. Adjust **one parameter at a time**, following the guide's **Tuning Order** when available.
4. Regenerate the config. Repeat until satisfied.
   - **usdview mode:** re-run the batch command with the updated config, then offer: *"Batch run complete. I can re-open usdview on the updated output. You'll need to close the previous usdview window first (it doesn't auto-reload)."*
   - **Headless mode (default):** generate screenshot of the output for comparison (current behavior).
5. Provide the **final config** with a quick-reference summary of what each parameter does and which way to move it, so the user can self-tune in the future.
6. **Offer to save the session:** *"Would you like me to save this tuning session to `.agents/docs/sessions/<operation>.md`? This helps future users and improves the tuning experience for this operation."* If yes, append the session (input, goal, iteration log, key learnings, final config) to the session file.

---

## Guide Authoring Mode

For operation developers who want to create a tuning guide. Triggered by "create a guide for X" or similar.

1. **Read C++ source:** `source/operations/<operation>/` — extract all `addArgument()` calls (name, type, default, description, hidden flag)
2. **Read template:** `.agents/operations/_template.md`
3. **Pre-fill the guide** at `.agents/operations/<operation>.md`:
   - **Header:** Fill in Key, Source path
   - **Parameters table:** Populate from C++ source
   - **Overview, Tuning Order, Visual Diagnosis, Starting Configs, Known Limitations:** Insert HTML-comment TODO markers (literal text `TODO(developer)` wrapped in an HTML comment, written as `&lt;!-- TODO(developer) --&gt;`) with contextual prompts based on what we know from the source
4. **Present the draft** to the developer for review and explain which sections need their domain expertise
5. **Update `INDEX.md`** if a new operation was added

---

## usdview Viewer Launch

Use this to open a USD file in the standalone USD viewer for 3D inspection. Viewer-only — no parameter editing. User runs batch optimization separately.

### Probing for usdview

Use the same probing steps described in the **usdview** section of **Step 2 (Interactive Viewing Tool Probes)** above. Use cached `usdview_available`, `usdview_path`, and `usdview_python` values from that probe.

### Cross-platform notes

- **Windows:** usdview ships as a Python script (`bin/usdview`) plus a `.cmd` wrapper. The Python script has a hardcoded shebang that may not match the current Python. Always launch via `python <usdview_path>` to use the active interpreter.
- **Linux/macOS — full OpenUSD build:** `PYTHONPATH` and `PATH` set by shell config. usdview is on PATH after sourcing.
- **Linux/macOS — system packages** (e.g., `apt install usd-tools`): usdview directly on PATH.
- **Linux/macOS — venv:** USD built into a virtualenv. Use the venv's Python: `<venv>/bin/python <venv>/bin/usdview`.
- **`PYTHONPATH` sourced but `bin/` not on PATH:** `pxr` importable but `usdview` not callable. Derive path from pxr location (probe step 2).

### Launch command

```bash
# Windows (always use python to avoid shebang issues):
python <usdview_path> <output.usd>

# Linux/macOS — if on PATH:
usdview <output.usd> &

# Linux/macOS — if in a venv:
<venv>/bin/python <venv>/bin/usdview <output.usd> &

# Linux/macOS — if pxr importable but usdview not on PATH:
python3 <usdview_path> <output.usd> &
```

Run in background (`run_in_background: true`) so the tuning session continues.

### Workflow

1. Run batch optimization → produces output USD
2. Launch usdview on the output
3. User inspects in 3D, reports observations
4. Adjust parameters, re-run batch, offer to re-launch usdview

### Notes

- No auto-reload — user must File > Reopen Stage or close/reopen usdview after re-running batch.
- Storm renderer (OpenGL), not RTX.
- No plugin API for custom panels — viewer only, no parameter editing.
- **Display purpose:** usdview shows all purposes (proxy, guide, render) by default. Tell users to set the right purpose via **Display > Display Purpose** — typically uncheck **Proxy** and **Guide** to show only render-purpose geometry. This avoids seeing proxy bounding boxes or guide overlays that obscure the actual mesh.

---

## Rules

- **Teach, don't just prescribe** — when suggesting a change, explain *what the parameter does* and *why this direction helps*. Use analogies. The user should feel confident adjusting parameters on their own.
- **World units** — remind the user when adjusting world-space parameters.
- **Conservative increments** — become more aggressive with changes in parameter values only if there isn't much change in the output.
- **HTML-comment TODO sections** (literal text `TODO(developer)` wrapped in an HTML comment, written as `&lt;!-- TODO(developer) --&gt;`) indicate gaps in the guide. Be transparent and suggest the user experiment or consult the operation author.
- Always examine screenshots before suggesting changes.
- **Communicate quality level** — be transparent about which tier of support the user is getting and what that means for the guidance quality.

## Purpose

Run an interactive, screenshot-driven tuning loop for a single Scene
Optimizer operation. Loads a tier-appropriate knowledge source (full
guide → session log → C++ source), generates a starting config based
on the asset's metrics, and iterates one-parameter-at-a-time with the
user until the output is acceptable. Doubles as the **guide-authoring
mode** for operation developers.

## Prerequisites

- A built repo (`./repo.sh build` or `repo.bat build`) so the
  `sceneOptimizer` binary and bundled Python are available.
- Input + output USD paths (the input is read-only; the output is
  rewritten on every iteration).
- A Python interpreter with `pxr` for the Step 3 inspection — either
  standalone (`pip install usd-core`) or the build's bundled
  `_build/target-deps/python/`.
- Optionally: a screenshot tool (`ovrtx`, `usdrecord`, or `usdview`)
  for visual diagnosis. The skill probes for these and falls back to
  headless mode if none is available.

## Limitations

- **One operation at a time.** Multi-op chains belong to
  `run-operations` with `--pipeline`; this skill iterates a single op.
- **Tier 2 / Tier 3 quality is best-effort.** When a guide is missing,
  the skill warns the user that tuning order and visual diagnosis are
  derived from session logs or the C++ source rather than authored
  expertise.
- **Visual diagnosis requires the user.** The skill renders
  screenshots but the "is this acceptable?" judgment is the user's;
  it never silently accepts an output.
- **No auto-reload in usdview.** When using usdview for inspection,
  the user must close/reopen the window between iterations.
- Wireframe-on-shaded rendering needs PySide6 + pxr (Storm path) or
  ovrtx (RTX path); not all environments will have either.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `pxr` probe fails | USD bindings not installed and the repo isn't built. | `pip install usd-core` or run the `build` skill first. |
| usdview probe finds nothing | usdview isn't installed (pip `usd-core` doesn't include it). | Use a full OpenUSD build, install the system `usd-tools` package, or fall back to headless screenshots. |
| Operation guide says one thing, source says another | Guide is stale (drift from C++ defaults). | Trust the C++ source for parameter defaults; flag the guide entry for the operation author. |
| Iteration loop drifts off-target | Changing more than one parameter at a time. | Revert to the previous config and adjust one parameter; ask the user before composing changes. |
| `printStats` shows different counts than the proxy | `printStats` is whole-stage. | For per-subtree numbers, use the post-export traversal in the operation guide (e.g. `create-proxy` runtime verification). |

