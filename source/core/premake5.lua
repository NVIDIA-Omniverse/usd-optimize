so_build = require("tools/premake/scene-optimizer-public")

-- Set up the location names for the plugin
local namespace = "omni/scene/optimizer/core"
local plugin_source_path = "plugins"


project_with_location("omni.scene.optimizer.core")

    -- build the shared library for scene optimizer core
    so_build.shared_library({
        library_name = "core",
        headers = { "src/**/*.h" },
        sources = { "src/**.cpp" }
    })
    removefiles { "src/SceneOptimizerInterface.cpp", "src/sceneOptimizer.cpp" }

    local lib_dir = "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/lib"
    local ext_dir = "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/exts/omni.scene.optimizer.core"

    -- Copy third-party libs into the lib dir so they will be picked up at runtime.
    repo_build.prebuild_copy {
        {"%{root}/_build/target-deps/autouv-core/%{config}/lib/*", lib_dir},
        {"%{root}/_build/target-deps/omnimesh_ops_usd/%{config}/lib/*", lib_dir},
        {"%{root}/_build/target-deps/mesh_tools/%{config}/lib/*", lib_dir},
        -- Operation name/attribute mapping
        {"config/operation_mapping.json", lib_dir},
    }
    -- Link test data dir into the expected location
    repo_build.prebuild_link {
        { "%{root}/source/tests/data", ext_dir.."/data" },
    }

    -- Copy USD and Python libs to an extraLibs dir for testing
    local extra_dir = "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/extraLibs"
    repo_build.prebuild_copy{
        {target_deps.."/usd/%{config}/lib/*", extra_dir},
        {target_deps.."/usd/%{config}/lib/usd", extra_dir.."/usd"}
    }

    -- A couple of extra libs that are found in different places on windows
    if os.target() == "windows" then
        repo_build.prebuild_copy{
            {target_deps.."/usd/%{config}/bin/tbb12*.dll", extra_dir},
            {target_deps.."/python/python"..string.gsub(PYTHON_VERSION, "%.", "")..".dll", extra_dir},
        }
    end


project_with_location("core_python")

    dependson( "omni.scene.optimizer.core" )

    -- build the python bindings for scene optimizer core
    so_build.use_python()
    so_build.use_usd()
    so_build.use_mesh_tools()
    so_build.use_omni_mesh()
    so_build.use_so_core()

    so_build.python_bindings({
        bindings_module_name = "omni_scene_optimizer_impl_core",
        bindings_sources = "bindings/BindingsPython.cpp",
        python_sources = "python/omni/scene/optimizer/impl/core/*.py",
    })

    so_build.python_module({
        module_path = "omni/scene/optimizer/core",
        python_sources = "python/omni/scene/optimizer/core/*.py",
    })

    so_build.python_module({
        module_path = "omni/scene/optimizer/core/operation",
        python_sources = "python/omni/scene/optimizer/core/operation/*.py",
    })


-- Build standalone CLI tool
-- Currently this is only built locally, for dev purposes
if not os.getenv("CI_PIPELINE_ID") then

    project_with_location("sceneOptimizer")

        dependson("omni.scene.optimizer.core")

        kind "ConsoleApp"
        staticruntime "Off"

        -- Standard way of building in Omniverse
        exceptionhandling "On"
        rtti "On"
        language "C++"

        -- Define the runtime to match the build configuration
        filter { "configurations:debug" }
            runtime "Debug"
        filter  { "configurations:release" }
            runtime "Release"
        filter {}

        includedirs {
            "%{root}/_build/%{platform}/%{config}/include",
            "%{root}/source/pch"
        }

        externalincludedirs {
            "%{root}/_build/target-deps/usd/%{config}/include",
            "%{root}/_build/target-deps/python/include/python"..PYTHON_VERSION,
        }

        -- Disable manifest generation, so our custom one is used
        -- This enables long path support on windows
        filter { "system:windows" }
        flags {"NoManifest"}
        filter {}

        -- Copy batch file to set environment for execution
        if os.target() == "windows" then
            repo_build.prebuild_copy{
                {"%{root}/source/core/src/sceneOptimizer.bat", "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}"},
            }
        end

        enable_gcov()

        -- source code to compile
        files { "src/SceneOptimizerInterface.cpp", "src/sceneOptimizer.cpp" }

        repo_build.prebuild_copy {
            {target_deps.."/python/lib/libpython*", extra_dir},
        }

        -- RPATH the lib dir for linux.
        runpathdirs("$OextraLibs")

        -- Use the copied libs to link against as well to ensure we are linking against the
        -- same thing.
        local extra_dir = "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/extraLibs"
        libdirs { extra_dir }

        -- Linux-specific compile information
        filter { "system:linux" }
            exceptionhandling "On"
            removeflags { "UndefinedIdentifiers" }

            -- Use older runpath behavior
            -- Without, some (not all) of the USD libs will not be found
            -- correctly, even though they're in the correct dir.
            linkoptions { "-Wl,--disable-new-dtags" }
        filter {}

        so_build.use_python()
        so_build.use_so_core()

        -- Link against the actual scene optimizer shared lib
        links {'omni.scene.optimizer.core'}

        add_usd {"ar","vt", "gf", "pcp", "sdf", "arch", "usd", "tf", "js", "trace", "usdUtils", "usdGeom", "usdPhysics", "usdShade", "usdSkel", "work", "kind"}
        add_usd {"hd", "usdLux", "usdImaging", "pxOsd", "plug", "python"}

        filter { "configurations:debug" }
            links {"tbb_debug"}
        filter  { "configurations:release" }
            links {"tbb"}
        filter {}

        -- Extra libs required by linux, but not win
        filter { "system:linux" }
            add_usd {"hdx"}
        filter {}
end