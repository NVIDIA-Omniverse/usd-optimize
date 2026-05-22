"""
Render a shaded screenshot via ovrtx (RTX quality).

Injects auto-camera + DomeLight + RenderProduct via inline USDA layer.
Does NOT modify the original USD file.

Requires: ovrtx, Pillow
IMPORTANT: ovrtx cannot coexist with pxr in the same process.
           Compute camera position separately (see screenshot-generation.md).

Usage: Replace SCENE_PATH, CAM_POS, OUTPUT_PATH, and METERS_PER_UNIT below.
"""

import sys

# ============================================================
# CONFIGURATION — replace these for your scene
# ============================================================
SCENE_PATH = "REPLACE_WITH_USD_PATH"
OUTPUT_PATH = "REPLACE_WITH_OUTPUT_PATH"
CAM_POS = (0, 0, 50)   # Pre-computed from bbox (see Auto-Camera Positioning)
CAM_ROT = None         # Set to (-90, 0, 0) for Z-up scenes
RESOLUTION = (1920, 1080)
METERS_PER_UNIT = 0.01  # Set to match scene units (0.001=mm, 0.01=cm, 1.0=m)

# ============================================================
# Render
# ============================================================
def main():
    """Render a shaded image of a USD scene using ovrtx with an auto-camera."""
    import ovrtx
    from PIL import Image

    renderer = ovrtx.Renderer()

    # Inline USDA: sublayer the scene, inject camera + light + render product.
    # Use string concatenation (NOT f-strings) to avoid brace escaping issues.
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
                Image.fromarray(var.tensor.numpy()).save(OUTPUT_PATH)

    print(f"Saved: {OUTPUT_PATH}", file=sys.stderr)


if __name__ == "__main__":
    main()
