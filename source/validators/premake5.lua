so_build = require("tools/premake/scene-optimizer-public")


project_with_location("validators_python")

    kind "Utility"

    so_build.symlink_folder({
        target_dir = "python/omni/scene/optimizer/validators",
        source_dir = "python/omni/scene/optimizer/validators",
    })