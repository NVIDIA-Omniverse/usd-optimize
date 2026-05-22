<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Generate Normals

**Key:** `generateNormals`
**Source:** `source/operations/generateNormals/GenerateNormals.cpp`

## Overview

Generate Normals computes and authors vertex normals for meshes. Proper normals are essential for correct shading — missing or incorrect normals cause flat shading or lighting artifacts.

The operation supports both angle-weighted and area-weighted normal computation. **`sharpnessAngle`** is the primary control: edges with a dihedral angle above this threshold get sharp (split) normals, creating visible creases. Edges below the threshold get smooth normals. This is the classic smooth/sharp shading threshold.

**`binding`** controls the interpolation of the generated normals (faceVarying or vertex). **`existingNormals`** determines what to do when normals already exist.

**Key insight:** a sharpness angle of 30–60 degrees works well for most mechanical objects. Lower values create more hard edges; higher values produce smoother shading.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `paths` | string[] | `[]` (all meshes) | Prim paths to process. Empty = all meshes. |
| `binding` | enum | `Auto` (3) | Normal interpolation: `Corners` (0), `Faces` (1), `Vertices` (2), `Auto` (3). |
| `existingNormals` | enum | `Fix` (0) | How to handle meshes with existing normals: `Fix` (0), `Replace` (1). |
| `sharpnessAngle` | float | `60.0` | Crease angle threshold in degrees. Edges sharper than this get split normals. |
| `weightmode` | enum | `Angle` (0) | Vertex normal weighting mode: `Angle` (0), `Area` (1). |
| `gpuThreshold` | int | `500000` | Vertex count above which GPU is used. Hidden. |

## Tuning Order

1. **`sharpnessAngle` first** — Start at 60. Decrease for more hard edges (mechanical look). Increase for smoother shading (organic look).
2. **`existingNormals` second** — Set to `Replace` (1) if you want to regenerate all normals.
3. **`binding` third** — `Corners` (0) is more flexible (per-face-vertex normals); `Vertices` (2) is more compact (shared normals). `Auto` (3) chooses automatically.
4. **`weightmode` fourth** — `Angle` (0) is usually the best quality. `Area` (1) weights by face size.

## Visual Diagnosis

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Surface looks faceted/flat | `sharpnessAngle` | Increase | More edges will be smoothed |
| Edges that should be sharp are smooth | `sharpnessAngle` | Decrease | Lower threshold for more creases |
| Existing normals not updated | `existingNormals` | Set to Replace (1) | Meshes with normals are skipped by default |
| Shading looks uneven | `weightmode` | Try Angle (0) | Better normal quality for irregular meshes |

## Starting Configs

**Standard normals**:
```json
[{"operation": "generateNormals", "sharpnessAngle": 60.0}]
```

**Mechanical/hard-surface**:
```json
[{"operation": "generateNormals", "sharpnessAngle": 30.0, "existingNormals": 1}]
```

**Smooth organic**:
```json
[{"operation": "generateNormals", "sharpnessAngle": 80.0}]
```

## Prerequisites & Workflows

- Works standalone on any mesh.
- Often needed after `merge` or `shrinkwrap` which may not preserve normals.

## Known Limitations

- GPU acceleration requires CUDA and is used automatically above the threshold.
- Normal quality depends on mesh topology — degenerate faces may produce incorrect normals.