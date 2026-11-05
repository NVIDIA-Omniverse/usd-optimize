-- Shared build scripts from repo_build package
repo_build = require("omni/repo/build")
repo_build.setup_options()

usd_optimize_build = require("tools/premake/usd-optimize-public")

-- Repo root
root = repo_build.get_abs_path(".")

-- Global paths
local build_dir = root.."/_build"
config = config or "%{cfg.buildcfg}"
workspace_dir = workspace_dir or "%{root}/_compiler/".._ACTION
host_deps = host_deps or build_dir.."/host-deps"
bin_dir = bin_dir or build_dir.."/%{cfg.system}-%{cfg.platform}/%{config}"

-- Setup where to write generated prebuild.toml file
repo_build.set_prebuild_file(root..'/_build/generated/prebuild.toml')


function enable_gcov()
    if _OPTIONS["enable-gcov"] then
        print("Enabling gcov")
        buildoptions { "--coverage -DGCOV_ENABLED -fno-inline-small-functions" }
        linkoptions { "--coverage" }
        optimize "Off"
    end
end


-- Helper: link a list of USD library names (stock USD uses usd_ prefix)
function add_usd(lib_names)
    for _, name in ipairs(lib_names) do
        links { "usd_"..name }
    end
end


-- Project helper that sets common defaults
function project_common(project_name)
    project(project_name)
    cppdialect "c++17"

    filter { "system:windows" }
        buildoptions { "/Zc:__cplusplus" }
    filter {}
end


-- Project helper that also sets the location
function project_with_location(project_name)
    project_common(project_name)
    location("%{root}/_compiler/".._ACTION.."/%{prj.name}")

    filter { "system:windows" }
        linkoptions { "/Brepro" }
    filter {}
end


--  Starting from here we define a structure of actual solution to be generated.
workspace(_OPTIONS["solution-name"])

    configurations { "debug", "release" }
    location (workspace_dir)
    targetdir (bin_dir)
    objdir ("%{root}/_build/intermediate/%{cfg.system}-%{cfg.platform}/%{prj.name}")

    -- Setup include paths.
    externalincludedirs {
        "_build/target-deps",
        "_build/target-deps/carb_sdk_plugins/include",
    }

    -- Link all licenses
    if not os.isdir(root.."/_build/PACKAGE-LICENSES") then
        os.mkdir(root.."/_build/PACKAGE-LICENSES")
    end
    repo_build.prebuild_link {
        { "_build/PACKAGE-LICENSES", bin_dir.."/PACKAGE-LICENSES" },
    }

    flags { "FatalCompileWarnings", "MultiProcessorCompile", "UndefinedIdentifiers", "NoIncrementalLink" }

    cppdialect "c++17"

    -- Windows platform settings
    filter { "system:windows" }
        platforms { "x86_64" }

        files {".editorconfig"}
        editandcontinue "Off"

        usd_optimize_build.use_host_toolchain()

        buildoptions {"/utf-8", "/bigobj"}
        buildoptions {"/permissive-", "/Zc:twoPhase-"}
        buildoptions { "/D _SILENCE_CXX17_UNCAUGHT_EXCEPTION_DEPRECATION_WARNING /D THRUST_HOST_SYSTEM=THRUST_HOST_SYSTEM_TBB" }
        disablewarnings { "4244", "4267", "4273", "4305" }

    -- Linux platform settings
    filter { "system:linux" }
        platforms { "x86_64", "aarch64" }
        defaultplatform "x86_64"

        buildoptions {"-Wconversion -Wno-float-conversion"}
        buildoptions { "-fvisibility=hidden -D_FILE_OFFSET_BITS=64 -DTHRUST_HOST_SYSTEM=THRUST_HOST_SYSTEM_TBB" }
        linkoptions { "-Wl,-rpath,'$$ORIGIN' -Wl,--export-dynamic" }
        enablewarnings { "all" }

    filter { "platforms:x86_64" }
        architecture "x86_64"

    -- carb/AlignSize.h assumes 64-byte cache lines; GCC's generic aarch64 tuning reports 256.
    -- GCC 12+ only: older toolchains (e.g. gcc-11) do not recognize this --param.
    -- The probe queries $CC (falling back to "gcc"); for aarch64 cross-builds, point $CC at the
    -- aarch64 cross-compiler so the version gate matches the toolchain premake actually invokes.
    -- os.outputof returns nil when the binary is missing, so guard against a nil dereference.
    local host_gcc = os.getenv("CC") or "gcc"
    local ver_output = os.outputof(host_gcc .. " -dumpversion") or ""
    local host_gcc_major = tonumber(ver_output:match("^(%d+)")) or 0
    if host_gcc_major >= 12 then
        filter { "system:linux", "platforms:aarch64" }
            buildoptions { "--param=destructive-interference-size=64" }
        filter {}
    end

    -- Debug configuration settings
    filter { "configurations:debug" }
        defines { "DEBUG" }
        optimize "Off"

    -- Release configuration settings
    filter  { "configurations:release" }
        defines { "NDEBUG" }
        optimize "Speed"

    filter {}

include("source/core/premake5.lua")
include("source/operations/premake5.lua")
include("source/validators/premake5.lua")
include("source/cli/premake5.lua")

-- Set up project for C++ unit tests. It must be included inside a workspace scope.
include("source/tests/premake5.lua")

newoption {
    trigger     = "enable-gcov",
    description = "(Optional) enable GCC code coverage tracking"
}
