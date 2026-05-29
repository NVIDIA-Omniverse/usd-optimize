local m = {} -- The main module table

repo_build = require("omni/repo/build")

include("scene-optimizer-options")
print("Building with usd_flavor="..USD_FLAVOR.." and usd_ver="..USD_VERSION.." and python_ver="..PYTHON_VERSION)


-- Common Global Variables

target_deps = target_deps or repo_build.target_deps_dir()
target_build_dir = target_build_dir or repo_build.target_dir()
target_lib_dir = target_build_dir.."/lib"
target_python_dir = target_python_dir or target_build_dir.."/python"
operation_dir = operation_dir or target_lib_dir.."/operations"

function m.link_operations_dir()
    -- On CI the operations directory may not exist when we try to link it, even after building
    -- the python operations. Force it to be created by copying a file into it first.
    repo_build.prebuild_copy {
        {"%{root}/VERSION.md", operation_dir.."/VERSION.md"},
    }
    repo_build.prebuild_link {
        {operation_dir, "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/operations" },
    }
end


-- Use functions
-- Windows: VS / MSVC / Windows SDK are applied via [repo_build.msbuild] link_host_toolchain in repo.toml
-- (omni repo_build + premake), not Packman msvc/winsdk. See: repo_build toolchains docs.
function m.use_host_toolchain()
end


function m.use_pybind()
    externalincludedirs { target_deps.."/pybind11/include" }
end

function m.use_python()
    local python_folder = target_deps.."/python"

    filter { "system:windows" }
        externalincludedirs { python_folder.."/include" }
        syslibdirs { python_folder.."/libs" }
    filter { "system:linux" }
        externalincludedirs { python_folder.."/include/python"..PYTHON_VERSION }
        syslibdirs { python_folder.."/lib" }
        links { "python"..PYTHON_VERSION }
    filter {}
end

-- Convenience function to include USD headers and libs
function m.use_usd()

    externalincludedirs {
        target_deps.."/usd/%{config}/include",
    }

    libdirs {
        target_deps.."/usd/%{config}/lib",
    }

    add_usd {"ar","vt", "gf", "pcp", "sdf", "arch", "usd", "tf", "js", "trace", "usdUtils", "usdGeom", "usdPhysics", "usdShade", "usdSkel", "work", "kind"}
    add_usd {"usdLux", "plug", "python"}

    filter { "configurations:debug" }
        links {"tbb_debug"}
    filter  { "configurations:release" }
        links {"tbb"}
    filter {}

end

-- Convenience function to build against OmniMesh
function m.use_omni_mesh()

    externalincludedirs {
        target_deps.."/omnimesh_ops_usd/%{config}/include",
    }

    libdirs {
        target_deps.."/omnimesh_ops_usd/%{config}/lib",
    }


    links {"omnimesh_ops", "omnimesh_ops_usd"}

end

-- Convenience function to build against Mesh Tools
function m.use_mesh_tools()

    externalincludedirs {
        target_deps.."/mesh_tools/%{config}/include",
    }

    libdirs {
        target_deps.."/mesh_tools/%{config}/lib",
    }

    links {"mesh_tools"}

    -- mesh_tools.lib was built with /MT (static CRT), which injects LIBCMT as a default lib.
    -- Our plugins use /MD (dynamic CRT). Tell the linker to drop the conflicting LIBCMT
    -- default lib to silence LNK4098 "defaultlib 'LIBCMT' conflicts with use of other libs".
    filter { "system:windows" }
        linkoptions { "/NODEFAULTLIB:LIBCMT" }

    -- mesh_tools (static) contains CUDA device code compiled against the static CUDA runtime.
    -- Link cudart_static so the runtime is baked in and no libcudart.so is needed at runtime.
    -- dl/pthread/rt are required system deps of cudart_static on Linux.
    filter { "system:linux" }
        libdirs { target_deps.."/cuda/lib64" }
        links { "cudart_static", "dl", "pthread", "rt" }
    filter {}

end


-- Convenience function to build against the standalone core scene optimizer library
function m.use_so_core()

    includedirs { "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/include" }
    libdirs { target_lib_dir }
    links { "omni.scene.optimizer.core" }

end


-- Create a C++ shared library project
-- @param library_name: The base name of the library to build (without "lib" prefix, "omni.scene.optimizer" prefix, or file extension)
-- @param headers: A list of header files to add to the project
-- @param sources: A list of source files to add to the project
function m.shared_library(options)
    -- check options
    if type(options.library_name) ~= "string" then
        error("`library_name` must be specified")
    end

    local library_name = options.library_name
    local headers = options.headers or {}
    local sources = options.sources or {}

    -- copy headers to prebuild include dir
    for _, header in ipairs(headers) do
        repo_build.prebuild_copy({ header, "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/include/omni/scene.optimizer/"..library_name.."/" })
    end

    kind "SharedLib"
    language "C++"
    exceptionhandling "On"
    rtti "On"
    -- Note: for Windows we need to have staticruntime off otherwise there's problems across DLL boundaries
    --       For example environment variables set at the runtime level can't be accessed within the library
    staticruntime "Off"

    -- Define the runtime to match the build configuration
    filter { "configurations:debug" }
        runtime "Debug"
    filter  { "configurations:release" }
        runtime "Release"
    filter {}

    filter { "system:windows" }
        -- Given we compile all our plugins with the exact same compiler as the
        -- core library, every time, we disable this in order to simplify some
        -- code. For example, in the OmniOperation derived classes, this lets us
        -- simplify some code instead of hacking around dll boundary issues.
        disablewarnings { "4251" } -- (warning C4251: needs to have a dll-interface)
    filter {}

    -- include from the copied headers so that the hierarchy is correct
    includedirs { "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/include" }

    files { headers, sources }

    filter { "system:windows" }
        if os.isfile("version.rc") then
            files{ "version.rc" }
        end
    filter {}

    -- Linux-specific compile information
    filter { "system:linux" }
        removeflags { "UndefinedIdentifiers" }
        linkoptions { "-Wl,--disable-new-dtags" }
    filter {}

    targetdir(target_lib_dir)
    targetname("omni.scene.optimizer."..library_name)

    m.use_python()
    m.use_usd()
    -- mesh_tools must come before omni_mesh so that CUDA symbols from the
    -- static lib are resolved by the CUDA libs omnimesh brings in (Linux
    -- single-pass linker requires providers to follow consumers).
    m.use_mesh_tools()
    m.use_omni_mesh()
end


function m.python_bindings(options)
    -- check options
    if type(options.module_name) ~= "string" and type(options.bindings_module_name) ~= "string" then
        error("One of `module_name` or `bindings_module_name` must be specified")
    end

    local bindings_module_name = options.bindings_module_name
    local python_module_name = options.module_name or bindings_module_name:gsub("_", ".")
    local module_dir = python_module_name:gsub("%.", "/")
    local python_sources = options.python_sources or {}
    local bindings_sources = options.bindings_sources or {}

    local target_bindings_dir = target_python_dir.."/"..module_dir

    repo_build.prebuild_copy({ python_sources, target_bindings_dir })

    if bindings_sources then

        m.use_host_toolchain()
        m.use_pybind()

        local python_dep = target_deps.."/python"..PYTHON_VERSION

        defines { "MODULE_NAME="..bindings_module_name }
        includedirs { "include" }
        libdirs { target_build_dir.."/lib" }
        files { python_sources, bindings_sources }
        targetdir(target_bindings_dir)

        repo_build.define_bindings_python("_"..bindings_module_name, python_dep, PYTHON_VERSION)

        includedirs {
            "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/include",
            "%{root}/source/pch"
        }

        -- repo_build adds "-Wl,--no-undefined" which would require us to explicitly
        -- link every upstream dependency; remove it to allow lazy symbol resolution
        removelinkoptions { "-Wl,--no-undefined" }

        -- Linux-specific compile information
        filter { "system:linux" }
            linkoptions { "-Wl,--disable-new-dtags" }
        filter {}

        -- _DEBUG triggers a compilation error in the TBB headers when used with pybind11;
        -- removing it avoids the conflict without observable side effects.
        removedefines {"_DEBUG"}

    end

end

-- Creates a folder that is symlinked from the source directory to the target
-- directory in the build directory.
function m.symlink_folder(options)
    -- check options
    if type(options.source_dir) ~= "string" and type(options.target_dir) ~= "string" then
        error("`source_dir` and `target_dir` must be specified")
    end

    local source_dir = options.source_dir
    local target_dir = options.target_dir

    local build_target_dir = target_build_dir.."/"..target_dir
    repo_build.prebuild_link({ source_dir, build_target_dir })
end

-- Create a C++ operation plugin
-- @param sources: A list of source files to add to the project
function m.operation_plugin(sources)
    dependson("omni.scene.optimizer.core")

    defines {"SO_PLUGIN_AUTHOR="..plugin_author}

    enable_gcov()

    kind "SharedLib"
    language "C++"
    exceptionhandling "On"
    rtti "On"
    staticruntime "Off"

    -- Define the runtime to match the build configuration
    filter { "configurations:debug" }
        runtime "Debug"
    filter  { "configurations:release" }
        runtime "Release"
    filter {}

    -- Add RC info file for windows
    filter { "system:windows" }
    files("%{root}/_build/scene-optimizer.rc")
    filter {}

    includedirs {
        "%{root}/source/operations",
        "%{root}/source/pch"
    }

    -- Linux-specific compile information
    filter { "system:linux" }
        exceptionhandling "On"
        removeflags { "UndefinedIdentifiers" }
        linkoptions { "-Wl,--disable-new-dtags" }
    filter {}

    -- Link against the scene optimizer core
    m.use_so_core()

    targetdir (operation_dir)
    targetname (ProjectName)

    files { sources }

    -- External libraries to link against
    m.use_python()
    m.use_usd()

    filter { "system:windows" }
        -- See the shared library above for why
        disablewarnings { "4251" } -- (warning C4251: needs to have a dll-interface)
    filter {}

end

-- Create a Python operation plugin
-- @param dir : The source directory of the operation
-- @param name : The name of the operation
function m.operation_plugin_python(dir, name)
    repo_build.prebuild_link {{ dir, operation_dir.."/"..name },}
end

return m
