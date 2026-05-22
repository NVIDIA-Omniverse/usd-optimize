so_build = require("tools/premake/scene-optimizer-public")


project_with_location("validators_python")

    kind "Utility"

    so_build.python_module({
        module_path = "omni/scene/optimizer/validators",
        python_sources = "python/omni/scene/optimizer/validators/*.py",
    })