usd_optimize_build = require("tools/premake/usd-optimize-public")


target_build_dir = target_build_dir or repo_build.target_dir()
target_bin_dir = target_build_dir.."/bin"

project_with_location("usd_optimize_cli")

    dependson("usd_optimize.core")

    kind "ConsoleApp"
    staticruntime "Off"

    exceptionhandling "On"
    rtti "On"
    language "C++"

    targetdir (target_bin_dir)
    targetname ("usdOptimize")

    -- Define the runtime to match the build configuration
    filter { "configurations:debug" }
        runtime "Debug"
    filter  { "configurations:release" }
        runtime "Release"
    filter {}

    includedirs {
        "%{root}/_build/%{platform}/%{config}/include",
    }

    externalincludedirs {
        "%{root}/_build/target-deps/usd/%{config}/include",
        -- USD 26.08+ only; absent for 25.x.
        "%{root}/_build/target-deps/tbb/%{config}/include",
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
            {"%{root}/source/cli/src/usdOptimize.bat", "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/bin"},
        }
    end

    enable_gcov()

    -- source code to compile
    files { "src/**.cpp" }

    -- Use the copied libs to link against as well to ensure we are linking against the
    -- same thing.
    local extra_dir = "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/extraLibs"
    repo_build.prebuild_copy {
        {target_deps.."/python/lib/libpython*", extra_dir},
    }
    libdirs { extra_dir }
    
    -- RPATH the lib dir for linux.
    runpathdirs("$O../extraLibs")

    -- Linux-specific compile information
    filter { "system:linux" }
        exceptionhandling "On"
        removeflags { "UndefinedIdentifiers" }

        -- Use older runpath behavior
        -- Without, some (not all) of the USD libs will not be found
        -- correctly, even though they're in the correct dir.
        linkoptions { "-Wl,--disable-new-dtags" }
    filter {}

    usd_optimize_build.use_python()
    usd_optimize_build.use_usd_optimize_core()

    -- Link against the actual usd optimize shared lib
    links {'usd_optimize.core'}

    libdirs {
        "%{root}/_build/target-deps/tbb/%{config}/lib",
    }

    add_usd {"ar","vt", "gf", "pcp", "sdf", "arch", "usd", "tf", "js", "trace", "usdUtils", "usdGeom", "usdPhysics", "usdShade", "usdSkel", "work", "kind"}
    add_usd {"usdLux", "plug", "python"}

    filter { "configurations:debug" }
        links {"tbb_debug"}
    filter  { "configurations:release" }
        links {"tbb"}
    filter {}