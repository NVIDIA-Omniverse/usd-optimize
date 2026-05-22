"""
Render LdrColor and DistanceToCameraSD AOVs via ovrtx.

Used for depth-tested wireframe compositing (pxr edge projection).
Saves color as PNG and depth as .npy (float32, meters).

Requires: ovrtx, Pillow, numpy
IMPORTANT: ovrtx cannot coexist with pxr in the same process.

Replace SCENE_PATH, CAM_POS, OUTPUT_DIR, and METERS_PER_UNIT below.
"""

import sys
import os
import numpy as np

# ============================================================
# CONFIGURATION — replace these for your scene
# ============================================================
SCENE_PATH = "REPLACE_WITH_USD_PATH"
OUTPUT_DIR = "REPLACE_WITH_OUTPUT_DIR"
CAM_POS = (0, 0, 50)   # Pre-computed from bbox
CAM_ROT = None         # Set to (-90, 0, 0) for Z-up scenes
RESOLUTION = (1920, 1080)
METERS_PER_UNIT = 0.01  # Match scene units (0.001=mm, 0.01=cm, 1.0=m)

# ============================================================
# Render with AOVs
# ============================================================
def main():
    """Render a USD scene with ovrtx and export color, depth, and normal AOVs."""
    import ovrtx
    from PIL import Image

    renderer = ovrtx.Renderer()

    # NOTE: Set metersPerUnit on the root layer to match the sublayer.
    # DistanceToCameraSD reports Euclidean distance in meters regardless of
    # scene units — setting metersPerUnit ensures correct camera positioning.
    renderer.add_usd_layer('''
#usda 1.0
(
    subLayers = [@''' + SCENE_PATH + '''@]
    metersPerUnit = ''' + str(METERS_PER_UNIT) + '''
)

def Camera "AutoCamera" {
    float focalLength = 24
    double3 xformOp:translate = ''' + str(CAM_POS) + ('''
    float3 xformOp:rotateXYZ = ''' + str(CAM_ROT) + '''
    token[] xformOpOrder = ["xformOp:translate", "xformOp:rotateXYZ"]''' if CAM_ROT else '''
    token[] xformOpOrder = ["xformOp:translate"]''') + '''
}

def DomeLight "AutoDomeLight" {
    float inputs:intensity = 1000
}

def "Render" {
    def RenderProduct "Camera" {
        rel camera = </AutoCamera>
        int2 resolution = ''' + str(RESOLUTION) + '''
        rel orderedVars = [<LdrColor>, <DistanceToCameraSD>]
        def RenderVar "LdrColor" {
            string sourceName = "LdrColor"
        }
        def RenderVar "DistanceToCameraSD" {
            string sourceName = "DistanceToCameraSD"
        }
    }
}
''')

    products = renderer.step(render_products={"/Render/Camera"}, delta_time=1/60)

    color_output = os.path.join(OUTPUT_DIR, "ovrtx_color.png")
    depth_output = os.path.join(OUTPUT_DIR, "ovrtx_depth.npy")
    os.makedirs(OUTPUT_DIR, exist_ok=True)

    for _, product in products.items():
        for frame in product.frames:
            with frame.render_vars["LdrColor"].map(device=ovrtx.Device.CPU) as var:
                Image.fromarray(var.tensor.numpy()).save(color_output)
                print(f"Color saved: {color_output}", file=sys.stderr)

            # Data arrives as uint32 (bit-packed float32). Reinterpret before use.
            with frame.render_vars["DistanceToCameraSD"].map(device=ovrtx.Device.CPU) as var:
                depth_raw = var.tensor.numpy()
                if depth_raw.dtype == np.uint32:
                    depth_float = depth_raw.view(np.float32)
                else:
                    depth_float = depth_raw.astype(np.float32)
                if depth_float.ndim == 3:
                    depth_float = depth_float[:, :, 0]
                np.save(depth_output, depth_float)
                print(f"Depth saved: {depth_output} range=[{np.nanmin(depth_float):.4f}, {np.nanmax(depth_float):.4f}]", file=sys.stderr)

    print("Done.", file=sys.stderr)


if __name__ == "__main__":
    main()
