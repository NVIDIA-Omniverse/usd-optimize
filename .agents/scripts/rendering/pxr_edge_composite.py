"""
Composite pxr-projected mesh wireframe edges onto an ovrtx RTX render.

Uses depth buffer from ovrtx (DistanceToCameraSD) for proper occlusion.
Extracts triangle edges via pxr, projects to 2D, depth-tests, and draws.

Requires: pxr (OpenUSD), Pillow, numpy
IMPORTANT: Run this with pxr Python, NOT the ovrtx Python.

CAMERA MATCHING CRITICAL:
  USD Camera default horizontalAperture = 20.955mm (NOT 36mm full-frame).
  If you use 36mm, projected edges will cluster toward center and not align
  with the ovrtx render. Always use SENSOR_WIDTH_MM = 20.955.

Replace SCENE_PATH, CAM_POS, CAM_TARGET, OUTPUT_DIR, and METERS_PER_UNIT below.
"""

import numpy as np
import time
import os
from pxr import Usd, UsdGeom
from PIL import Image, ImageDraw

# ============================================================
# CONFIGURATION — replace these for your scene
# ============================================================
SCENE_PATH = "REPLACE_WITH_USD_PATH"
OUTPUT_DIR = "REPLACE_WITH_OUTPUT_DIR"
COLOR_INPUT = os.path.join(OUTPUT_DIR, "ovrtx_color.png")
DEPTH_INPUT = os.path.join(OUTPUT_DIR, "ovrtx_depth.npy")

# Camera — must match ovrtx setup EXACTLY
CAM_POS = np.array([0.0, 0.0, 50.0])
CAM_TARGET = np.array([0.0, 0.0, 0.0])

WIDTH, HEIGHT = 1920, 1080
FOCAL_LENGTH_MM = 24.0
SENSOR_WIDTH_MM = 20.955  # USD Camera default horizontalAperture — NOT 36mm!
METERS_PER_UNIT = 0.01    # Scene units to meters (for depth buffer comparison)

# Edge settings
MAX_EDGES = 2000000
DEPTH_TOLERANCE = 0.02    # 2% relative tolerance for depth comparison
SAMPLES_PER_EDGE = 7
EDGE_COLOR = (40, 40, 40, 180)  # dark gray, semi-transparent
EDGE_WIDTH = 1

# ============================================================
# Load ovrtx outputs
# ============================================================
def project_points(pts_3d, cam_pos, right, up, forward, focal_px, width, height):
    """Project 3D points to 2D screen coordinates using a pinhole camera model."""
    rel = pts_3d - cam_pos
    x_cam = rel @ right
    y_cam = rel @ up
    z_cam = rel @ forward
    valid = z_cam > 1.0
    screen_x = np.where(valid, (x_cam / z_cam) * focal_px + width / 2, np.nan)
    screen_y = np.where(valid, height / 2 - (y_cam / z_cam) * focal_px, np.nan)
    return screen_x, screen_y, z_cam, valid


def main():
    """Composite projected mesh edges onto an ovrtx color render with depth testing."""
    color_img = Image.open(COLOR_INPUT).convert("RGBA")
    depth_buf = np.load(DEPTH_INPUT)

    # Camera setup
    forward = CAM_TARGET - CAM_POS
    forward = forward / np.linalg.norm(forward)
    stage = Usd.Stage.Open(SCENE_PATH)
    up_axis = UsdGeom.GetStageUpAxis(stage)
    world_up = np.array([0.0, 0.0, 1.0]) if up_axis == "Z" else np.array([0.0, 1.0, 0.0])
    right = np.cross(forward, world_up)
    right = right / np.linalg.norm(right)
    up = np.cross(right, forward)

    focal_px = (FOCAL_LENGTH_MM / SENSOR_WIDTH_MM) * WIDTH

    # Extract mesh edges from USD
    print("Extracting triangle edges from USD...")
    t0 = time.time()
    xform_cache = UsdGeom.XformCache()

    all_p1 = []
    all_p2 = []

    for prim in stage.Traverse():
        if not prim.IsA(UsdGeom.Mesh):
            continue

        mesh = UsdGeom.Mesh(prim)
        points_attr = mesh.GetPointsAttr().Get()
        fvc_attr = mesh.GetFaceVertexCountsAttr().Get()
        fvi_attr = mesh.GetFaceVertexIndicesAttr().Get()

        if not points_attr or not fvc_attr or not fvi_attr:
            continue

        xform = xform_cache.GetLocalToWorldTransform(prim)
        mat = np.array([list(xform.GetRow(i)) for i in range(4)])

        pts = np.array(points_attr, dtype=np.float64)
        ones = np.ones((pts.shape[0], 1), dtype=np.float64)
        pts_world = (np.hstack([pts, ones]) @ mat.T)[:, :3]

        edge_set = set()
        idx = 0
        for count in fvc_attr:
            face_indices = [fvi_attr[idx + j] for j in range(count)]
            for j in range(1, count - 1):
                tri = (face_indices[0], face_indices[j], face_indices[j + 1])
                for k in range(3):
                    a, b = tri[k], tri[(k + 1) % 3]
                    edge_set.add((min(a, b), max(a, b)))
            idx += count

        for a, b in edge_set:
            all_p1.append(pts_world[a])
            all_p2.append(pts_world[b])

    p1 = np.array(all_p1, dtype=np.float64)
    p2 = np.array(all_p2, dtype=np.float64)
    total_edges = len(p1)
    print(f"  Edges: {total_edges} ({time.time() - t0:.1f}s)")

    if total_edges > MAX_EDGES:
        rng = np.random.default_rng(42)
        indices = rng.choice(total_edges, size=MAX_EDGES, replace=False)
        p1 = p1[indices]
        p2 = p2[indices]

    # Project edges to 2D
    x1, y1, z1, v1 = project_points(p1, CAM_POS, right, up, forward, focal_px, WIDTH, HEIGHT)
    x2, y2, z2, v2 = project_points(p2, CAM_POS, right, up, forward, focal_px, WIDTH, HEIGHT)

    margin = 50
    valid_mask = v1 & v2
    in_viewport = (
        ((x1 >= -margin) & (x1 < WIDTH + margin) & (y1 >= -margin) & (y1 < HEIGHT + margin)) |
        ((x2 >= -margin) & (x2 < WIDTH + margin) & (y2 >= -margin) & (y2 < HEIGHT + margin))
    )
    valid_mask = valid_mask & in_viewport

    sx1, sy1, sz1 = x1[valid_mask], y1[valid_mask], z1[valid_mask]
    sx2, sy2, sz2 = x2[valid_mask], y2[valid_mask], z2[valid_mask]

    # Depth test
    t_vals = np.linspace(0, 1, SAMPLES_PER_EDGE).reshape(1, -1)
    sample_x = sx1[:, None] + (sx2 - sx1)[:, None] * t_vals
    sample_y = sy1[:, None] + (sy2 - sy1)[:, None] * t_vals
    sample_z = sz1[:, None] + (sz2 - sz1)[:, None] * t_vals

    px = np.clip(sample_x.astype(np.int32), 0, WIDTH - 1)
    py = np.clip(sample_y.astype(np.int32), 0, HEIGHT - 1)

    sample_x_cam = (sample_x - WIDTH / 2) * sample_z / focal_px
    sample_y_cam = (HEIGHT / 2 - sample_y) * sample_z / focal_px
    sample_dist_scene = np.sqrt(sample_x_cam**2 + sample_y_cam**2 + sample_z**2)
    sample_dist_m = sample_dist_scene * METERS_PER_UNIT

    buf_depth = depth_buf[py, px]
    valid_depth = np.isfinite(buf_depth) & (buf_depth > 1e-4)
    safe_denom = np.where(valid_depth, buf_depth, 1.0)
    relative_diff = np.where(valid_depth,
                             np.abs(sample_dist_m - buf_depth) / safe_denom,
                             np.inf)
    edge_visible = (relative_diff < DEPTH_TOLERANCE).sum(axis=1) >= 1

    n_visible = edge_visible.sum()
    print(f"  Visible edges: {n_visible}")

    # Draw and composite
    edge_img = Image.new("RGBA", (WIDTH, HEIGHT), (0, 0, 0, 0))
    draw = ImageDraw.Draw(edge_img)

    vis_x1, vis_y1 = sx1[edge_visible], sy1[edge_visible]
    vis_x2, vis_y2 = sx2[edge_visible], sy2[edge_visible]

    for i in range(n_visible):
        draw.line([(vis_x1[i], vis_y1[i]), (vis_x2[i], vis_y2[i])],
                  fill=EDGE_COLOR, width=EDGE_WIDTH)

    composite = Image.alpha_composite(color_img, edge_img)
    output_path = os.path.join(OUTPUT_DIR, "wireframe_on_rtx.png")
    composite.convert("RGB").save(output_path)
    print(f"Saved: {output_path}")


if __name__ == "__main__":
    main()
