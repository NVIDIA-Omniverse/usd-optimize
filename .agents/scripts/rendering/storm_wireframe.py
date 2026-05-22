"""
Storm wireframe renderer using UsdImagingGLEngine (HdStorm).

Produces wireframe or wireframe-on-surface renders via offscreen OpenGL.
Bypasses usdrecord (which does NOT expose drawMode).

Requires: PySide6, pxr (OpenUSD), PyOpenGL, Pillow, numpy

Usage: Replace SCENE_PATH, CAM_POS, CAM_TARGET, and OUTPUT_DIR below,
       then run with the Python that has pxr installed.

Camera matching: uses view/projection matrices computed from bbox.
To match ovrtx renders, ensure the same camera position and FOV.
"""

import sys
import os
import math
import numpy as np
from PIL import Image

# ============================================================
# CONFIGURATION — replace these for your scene
# ============================================================
SCENE_PATH = "REPLACE_WITH_USD_PATH"
OUTPUT_DIR = "REPLACE_WITH_OUTPUT_DIR"
WIDTH, HEIGHT = 1920, 1080

# Camera — pre-compute from bbox (see Auto-Camera Positioning in screenshot-generation.md)
CAM_POS = None     # Gf.Vec3d, set below after stage open
CAM_TARGET = None   # Gf.Vec3d, set below after stage open

# ============================================================
# OpenGL context setup (PySide6 offscreen)
# ============================================================
from PySide6.QtWidgets import QApplication
from PySide6.QtGui import QOffscreenSurface, QOpenGLContext, QSurfaceFormat
from OpenGL import GL
from pxr import Usd, UsdGeom, UsdImagingGL, Gf, CameraUtil

def _setup_gl_context():
    """Create an offscreen OpenGL context via PySide6."""
    app = QApplication.instance() or QApplication(sys.argv)

    fmt = QSurfaceFormat()
    fmt.setVersion(4, 5)
    fmt.setProfile(QSurfaceFormat.OpenGLContextProfile.CompatibilityProfile)
    fmt.setDepthBufferSize(24)
    fmt.setStencilBufferSize(8)
    QSurfaceFormat.setDefaultFormat(fmt)

    surface = QOffscreenSurface()
    surface.setFormat(fmt)
    surface.create()

    gl_ctx = QOpenGLContext()
    gl_ctx.setFormat(fmt)
    if not gl_ctx.create():
        print("ERROR: Failed to create OpenGL context")
        sys.exit(1)
    if not gl_ctx.makeCurrent(surface):
        print("ERROR: Failed to make OpenGL context current")
        sys.exit(1)

    return app, surface, gl_ctx


# ============================================================
# FBO helpers
# ============================================================
def create_fbo(width, height):
    """Create an FBO with color and depth attachments."""
    fbo = GL.glGenFramebuffers(1)
    GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, fbo)

    color_tex = GL.glGenTextures(1)
    GL.glBindTexture(GL.GL_TEXTURE_2D, color_tex)
    GL.glTexImage2D(GL.GL_TEXTURE_2D, 0, GL.GL_RGBA8, width, height, 0,
                    GL.GL_RGBA, GL.GL_UNSIGNED_BYTE, None)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MIN_FILTER, GL.GL_LINEAR)
    GL.glTexParameteri(GL.GL_TEXTURE_2D, GL.GL_TEXTURE_MAG_FILTER, GL.GL_LINEAR)
    GL.glFramebufferTexture2D(GL.GL_FRAMEBUFFER, GL.GL_COLOR_ATTACHMENT0,
                              GL.GL_TEXTURE_2D, color_tex, 0)

    depth_rb = GL.glGenRenderbuffers(1)
    GL.glBindRenderbuffer(GL.GL_RENDERBUFFER, depth_rb)
    GL.glRenderbufferStorage(GL.GL_RENDERBUFFER, GL.GL_DEPTH_COMPONENT24, width, height)
    GL.glFramebufferRenderbuffer(GL.GL_FRAMEBUFFER, GL.GL_DEPTH_ATTACHMENT,
                                 GL.GL_RENDERBUFFER, depth_rb)

    status = GL.glCheckFramebufferStatus(GL.GL_FRAMEBUFFER)
    if status != GL.GL_FRAMEBUFFER_COMPLETE:
        print(f"ERROR: FBO incomplete, status=0x{status:X}")
        sys.exit(1)
    return fbo, color_tex, depth_rb


def read_fbo_pixels(width, height):
    """Read pixels from the currently bound FBO."""
    GL.glPixelStorei(GL.GL_PACK_ALIGNMENT, 1)
    data = GL.glReadPixels(0, 0, width, height, GL.GL_RGBA, GL.GL_UNSIGNED_BYTE)
    arr = np.frombuffer(data, dtype=np.uint8).reshape(height, width, 4)
    return arr[::-1].copy()  # flip vertically (OpenGL origin is bottom-left)


# ============================================================
# Camera matrices from position + target
# ============================================================
def compute_camera_matrices(eye, target, up_axis, bbox_size, width, height):
    """Compute view and projection matrices for UsdImagingGLEngine."""
    up = Gf.Vec3d(0, 1, 0) if up_axis == "Y" else Gf.Vec3d(0, 0, 1)

    forward = (target - eye).GetNormalized()
    right = Gf.Cross(forward, up).GetNormalized()
    true_up = Gf.Cross(right, forward).GetNormalized()

    # OpenGL-style view matrix (looking down -Z)
    view = Gf.Matrix4d(
        right[0],    true_up[0],   -forward[0],  0,
        right[1],    true_up[1],   -forward[1],  0,
        right[2],    true_up[2],   -forward[2],  0,
        -Gf.Dot(right, eye), -Gf.Dot(true_up, eye), Gf.Dot(forward, eye), 1
    )

    # Perspective projection
    max_extent = max(bbox_size[0], bbox_size[1], bbox_size[2])
    distance = (eye - target).GetLength()
    fov_y_rad = 2.0 * math.atan(max_extent / (2.0 * distance))
    fov_y_deg = max(20.0, min(math.degrees(fov_y_rad), 60.0))

    aspect = width / height
    near = distance * 0.01
    far = distance * 10.0
    f = 1.0 / math.tan(math.radians(fov_y_deg) / 2.0)

    proj = Gf.Matrix4d(
        f / aspect, 0,  0,                              0,
        0,          f,  0,                              0,
        0,          0,  -(far + near) / (far - near),  -1,
        0,          0,  -2 * far * near / (far - near),  0
    )
    return view, proj


# ============================================================
# Render function
# ============================================================
def render_wireframe(stage, draw_mode, view_matrix, proj_matrix, output_path):
    """Render the stage with the given draw mode and save to PNG."""
    fbo, color_tex, depth_rb = create_fbo(WIDTH, HEIGHT)
    GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, fbo)
    GL.glViewport(0, 0, WIDTH, HEIGHT)
    GL.glClearColor(0.0, 0.0, 0.0, 1.0)
    GL.glClear(GL.GL_COLOR_BUFFER_BIT | GL.GL_DEPTH_BUFFER_BIT)

    engine = UsdImagingGL.Engine()
    engine.SetRenderViewport(Gf.Vec4d(0, 0, WIDTH, HEIGHT))
    engine.SetRenderBufferSize(Gf.Vec2i(WIDTH, HEIGHT))
    engine.SetCameraState(view_matrix, proj_matrix)
    engine.SetWindowPolicy(CameraUtil.MatchVertically)

    params = UsdImagingGL.RenderParams()
    params.drawMode = draw_mode
    params.enableLighting = False
    params.enableSceneMaterials = False
    params.enableSceneLights = False
    params.clearColor = Gf.Vec4f(0.0, 0.0, 0.0, 1.0)
    params.frame = Usd.TimeCode.Default()
    params.complexity = 1.0
    params.showGuides = False
    params.showProxy = False   # CRITICAL: proxy boxes occlude render geometry
    params.showRender = True

    if draw_mode == UsdImagingGL.DrawMode.DRAW_WIREFRAME:
        params.wireframeColor = Gf.Vec4f(1.0, 1.0, 1.0, 1.0)  # white on black
    else:
        params.wireframeColor = Gf.Vec4f(0.0, 0.0, 0.0, 1.0)  # black on shaded

    engine.Render(stage.GetPseudoRoot(), params)
    GL.glFinish()

    GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, fbo)
    pixels = read_fbo_pixels(WIDTH, HEIGHT)

    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    Image.fromarray(pixels[:, :, :3], 'RGB').save(output_path)
    print(f"Saved: {output_path}")

    GL.glBindFramebuffer(GL.GL_FRAMEBUFFER, 0)
    GL.glDeleteFramebuffers(1, [fbo])
    GL.glDeleteTextures(1, [color_tex])
    GL.glDeleteRenderbuffers(1, [depth_rb])


# ============================================================
# Main
# ============================================================
def main():
    """Render wireframe and flat-shaded views of a USD scene using Storm (OpenGL)."""
    _setup_gl_context()

    stage = Usd.Stage.Open(SCENE_PATH)
    if not stage:
        print(f"ERROR: Failed to open {SCENE_PATH}")
        sys.exit(1)

    up_axis = UsdGeom.GetStageUpAxis(stage)

    # Auto-camera from bounding box
    bb = UsdGeom.BBoxCache(Usd.TimeCode.Default(), [UsdGeom.Tokens.default_, UsdGeom.Tokens.render, UsdGeom.Tokens.proxy])
    rng = bb.ComputeWorldBound(stage.GetPseudoRoot()).GetRange()
    bbox_min, bbox_max = rng.GetMin(), rng.GetMax()
    center = (bbox_min + bbox_max) / 2.0
    bbox_size = bbox_max - bbox_min
    diagonal = math.sqrt(bbox_size[0]**2 + bbox_size[1]**2 + bbox_size[2]**2)
    distance = diagonal * 1.5

    if up_axis == "Y":
        cam_pos = Gf.Vec3d(center[0], center[1], center[2] + distance)
    else:
        cam_pos = Gf.Vec3d(center[0], center[1] - distance, center[2])

    view_matrix, proj_matrix = compute_camera_matrices(cam_pos, center, up_axis, bbox_size, WIDTH, HEIGHT)

    render_wireframe(
        stage,
        UsdImagingGL.DrawMode.DRAW_WIREFRAME,
        view_matrix, proj_matrix,
        os.path.join(OUTPUT_DIR, "storm_wireframe.png")
    )

    render_wireframe(
        stage,
        UsdImagingGL.DrawMode.DRAW_WIREFRAME_ON_SURFACE,
        view_matrix, proj_matrix,
        os.path.join(OUTPUT_DIR, "storm_wireframe_on_surface.png")
    )

    print("Done!")


if __name__ == "__main__":
    main()
