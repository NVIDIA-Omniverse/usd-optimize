"""
Two-pass ovrtx wireframe-on-shaded renderer.

Renders shaded (mode 0) and wireframe (mode 1) in SEPARATE PROCESSES
(Carbonite locks settings after first Renderer init), then composites.

Usage:
  python ovrtx_two_pass_wireframe.py              # orchestrator (runs all 3 steps)
  python ovrtx_two_pass_wireframe.py shaded        # render shaded pass only
  python ovrtx_two_pass_wireframe.py wireframe     # render wireframe pass only
  python ovrtx_two_pass_wireframe.py composite     # composite only (no ovrtx needed)

Requires: ovrtx + Pillow (for render passes), numpy + Pillow (for composite)
IMPORTANT: ovrtx cannot coexist with pxr in the same process.

Replace SCENE_PATH, CAM_POS, OVRTX_PKG, OVRTX_PYTHON, and OUTPUT_DIR below.
"""

import sys
import os
import json
import subprocess
import numpy as np

# ============================================================
# CONFIGURATION — replace these for your scene
# ============================================================
SCENE_PATH = "REPLACE_WITH_USD_PATH"
OUTPUT_DIR = "REPLACE_WITH_OUTPUT_DIR"
CAM_POS = (0, 0, 50)   # Pre-computed from bbox
CAM_ROT = None         # Set to (-90, 0, 0) for Z-up scenes
RESOLUTION = (1920, 1080)
METERS_PER_UNIT = 0.01  # Match scene units

# ovrtx paths — adjust for your install
OVRTX_PKG = "REPLACE_WITH_OVRTX_SITE_PACKAGES/ovrtx"
OVRTX_PYTHON = "REPLACE_WITH_OVRTX_VENV_PYTHON"
# ============================================================

CONFIG_PATH = os.path.join(OVRTX_PKG, "bin", "ovrtx.config.json")

def build_usda_layer():
    """Build an inline USDA string that sublayers the scene and injects camera, light, and render product."""
    camera_xform = '''
    double3 xformOp:translate = ''' + str(CAM_POS)
    if CAM_ROT:
        camera_xform += '''
    float3 xformOp:rotateXYZ = ''' + str(CAM_ROT) + '''
    token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]'''
    else:
        camera_xform += '''
    token[] xformOpOrder = ["xformOp:translate"]'''

    return '''
#usda 1.0
(
    subLayers = [@''' + SCENE_PATH + '''@]
    metersPerUnit = ''' + str(METERS_PER_UNIT) + '''
)

def Camera "AutoCamera" {
    float focalLength = 24''' + camera_xform + '''
}

def DomeLight "AutoDomeLight" {
    float inputs:intensity = 1000
}

def "Render" {
    def RenderProduct "Camera" {
        rel camera = </AutoCamera>
        int2 resolution = ''' + str(RESOLUTION) + '''
        rel orderedVars = [<LdrColor>]
        def RenderVar "LdrColor" {
            string sourceName = "LdrColor"
        }
    }
}
'''


def write_config(wireframe_mode=0, thickness=1.5):
    """Write ovrtx.config.json with specified wireframe mode.

    CRITICAL: Carbonite locks settings after first Renderer init.
    Must write config BEFORE creating Renderer in each subprocess.
    """
    config = {
        "log": {"level": "Info", "outputStreamLevel": "Warning"},
        "app": {"graphics": {"api": "vulkan", "raytracing": True}},
    }
    if wireframe_mode > 0:
        config["rtx"] = {
            "wireframe": {
                "mode": wireframe_mode,
                "globalWireframeThickness": thickness
            }
            # NOTE: wireframeColor is a FLAT key at /rtx/wireframeColor,
            # NOT nested under /rtx/wireframe/color
        }
    with open(CONFIG_PATH, "w") as f:
        json.dump(config, f, indent=4)


def do_render(output_name):
    """Render one frame using ovrtx. Must be called in a fresh process."""
    import ovrtx
    from PIL import Image
    renderer = ovrtx.Renderer()
    renderer.add_usd_layer(build_usda_layer())
    products = renderer.step(render_products={"/Render/Camera"}, delta_time=1/60)
    for _, product in products.items():
        for frame in product.frames:
            with frame.render_vars["LdrColor"].map(device=ovrtx.Device.CPU) as var:
                img = var.tensor.numpy().copy()
                Image.fromarray(img).save(os.path.join(OUTPUT_DIR, output_name))
                print(f"Saved {output_name}: {img.shape}", file=sys.stderr)


def main():
    """Dispatch render passes or orchestrate the two-pass wireframe composite pipeline."""
    mode = sys.argv[1] if len(sys.argv) > 1 else "orchestrate"

    if mode == "shaded":
        do_render("ovrtx_pass1_shaded.png")

    elif mode == "wireframe":
        do_render("ovrtx_pass2_wireframe.png")

    elif mode == "composite":
        from PIL import Image

        shaded_img = np.array(Image.open(os.path.join(OUTPUT_DIR, "ovrtx_pass1_shaded.png")))
        wireframe_img = np.array(Image.open(os.path.join(OUTPUT_DIR, "ovrtx_pass2_wireframe.png")))

        shaded_f = shaded_img[:, :, :3].astype(np.float32) / 255.0
        wire_f = wireframe_img[:, :, :3].astype(np.float32) / 255.0

        corners = [wire_f[0, 0], wire_f[0, -1], wire_f[-1, 0], wire_f[-1, -1]]
        bg_color = np.mean(corners, axis=0)
        bg_gray = np.mean(bg_color)

        diff = np.linalg.norm(wire_f - bg_color, axis=2)
        object_mask = np.clip(diff * 5, 0, 1)

        wire_gray = np.mean(wire_f, axis=2)
        bg_gray_safe = bg_gray if bg_gray > 1e-6 else 1e-6
        wire_normalized = np.clip(wire_gray / bg_gray_safe, 0, 1)

        blend_factor = wire_normalized * object_mask + (1.0 - object_mask)
        composite = shaded_f * blend_factor[:, :, np.newaxis]

        composite_uint8 = (np.clip(composite, 0, 1) * 255).astype(np.uint8)
        Image.fromarray(composite_uint8).save(
            os.path.join(OUTPUT_DIR, "ovrtx_wireframe_on_shaded.png")
        )
        print("Saved: ovrtx_wireframe_on_shaded.png", file=sys.stderr)

    else:
        SCRIPT = os.path.abspath(__file__)

        env = os.environ.copy()
        env["PYTHONPATH"] = ""
        ovrtx_bin = os.path.join(OVRTX_PKG, "bin")
        if sys.platform == "win32":
            ovrtx_paths = f"{ovrtx_bin};{ovrtx_bin}/plugins;{ovrtx_bin}/plugins/rtx"
            env["PATH"] = ovrtx_paths + ";" + env.get("PATH", "")
        else:
            env["LD_LIBRARY_PATH"] = f"{ovrtx_bin}:{ovrtx_bin}/plugins:{ovrtx_bin}/plugins/rtx"

        try:
            with open(CONFIG_PATH, "r") as f:
                original_config_text = f.read()
        except FileNotFoundError:
            original_config_text = None

        try:
            print("Pass 1: Shaded render...")
            write_config(wireframe_mode=0)
            r = subprocess.run([OVRTX_PYTHON, SCRIPT, "shaded"], env=env,
                              capture_output=True, text=True, timeout=120)
            print(r.stderr.strip())
            if r.returncode != 0:
                sys.exit(1)

            print("Pass 2: Wireframe render...")
            write_config(wireframe_mode=1, thickness=1.5)
            r = subprocess.run([OVRTX_PYTHON, SCRIPT, "wireframe"], env=env,
                              capture_output=True, text=True, timeout=120)
            print(r.stderr.strip())
            if r.returncode != 0:
                sys.exit(1)
        finally:
            if original_config_text is not None:
                with open(CONFIG_PATH, "w") as f:
                    f.write(original_config_text)
                print("Config restored.")
            else:
                try:
                    os.remove(CONFIG_PATH)
                    print("Config removed (did not exist before script).")
                except FileNotFoundError:
                    pass

        print("Pass 3: Compositing...")
        r = subprocess.run([sys.executable, SCRIPT, "composite"],
                          capture_output=True, text=True, timeout=60)
        print(r.stderr.strip())
        if r.returncode != 0:
            sys.exit(1)

        print("Done!")


if __name__ == "__main__":
    main()
