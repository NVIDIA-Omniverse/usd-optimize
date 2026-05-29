so_build = require("tools/premake/scene-optimizer-public")

local function to_env_paths(paths, buildpath)
    local str = "";
    for k, v in ipairs(paths) do
        if os.target() == "windows" then
            str = str..buildpath.."/"..v..";";
        else
            str = str..buildpath.."/"..v..":";
        end
    end
    return str
end

function create_test_runner(name, config, paths)

    local platform_name
    if os.target() == "windows" then
        platform_name = "windows-x86_64"
    else
        platform_name = _OPTIONS["platform-target"] or platform_host or _OPTIONS["platform-host"]
        if platform_name == nil then
            error("--platform-target, --platform-host or platform_host must be specified")
        end
    end

    local config_paths = {}
    for k, v in ipairs(paths) do
        local p = string.gsub(v, "$CONFIG", config)
        p = string.gsub(p, "$PLATFORM", platform_name)
        config_paths[k] = p
    end

    if os.target() == "windows" then

        local bat_file_dir = root.."/_build/"..platform_name.."/"..config
        local bat_file_path = bat_file_dir.."/"..name..".bat"
        local env_file_path = bat_file_dir.."/"..name..".env.bat"
        -- Prepend (rather than append) so build artifacts beat any system-wide
        -- USD/Python install on PATH. Some developer machines have e.g.
        -- C:\USD_<ver>\lib on PATH, whose tf.dll would otherwise shadow our
        -- target-deps copy and break Python extension loading.
        local env_batch = string.format([[
set TEST_RUN_PATH=%s
set TEST_RUN_PATH=%%TEST_RUN_PATH:/=\%%
set TEST_RUN_PATH=%%TEST_RUN_PATH:\\=\%%
echo Prepending PATH env with: %%TEST_RUN_PATH%%
set PATH=%%TEST_RUN_PATH%%;%%PATH%%
        ]], to_env_paths(config_paths, "%~dp0"))

        -- the actual test runner
        local f = io.open(bat_file_path, 'w')
        f:write(string.format([[
@echo off
setlocal
%s
cd "%%~dp0"
"%%~dp0\%s.exe" %%*
        ]], env_batch, name))
        f:close()

        -- the env setup script (for use with profilers, etc that want to launch separately)
        local f = io.open(env_file_path, 'w')
        f:write(string.format([[
@echo off
%s
%%*
        ]], env_batch))
        f:close()
    else
        local sh_file_dir = root.."/_build/"..platform_name.."/"..config
        local sh_file_path = sh_file_dir.."/"..name..".sh"
        local env_file_path = sh_file_dir.."/"..name..".env.sh"
        -- Prepend (rather than append) so build artifacts beat any system-wide
        -- USD/Python install on LD_LIBRARY_PATH.
        local env_sh = string.format([[
#!/bin/bash
set -e
SCRIPT_DIR=$(realpath $(dirname ${BASH_SOURCE}))
export LD_LIBRARY_PATH=%s${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
        ]], to_env_paths(config_paths, "$SCRIPT_DIR"))

        -- the actual test runner
        local f = io.open(sh_file_path, 'w')
        f:write(string.format([[
%s
cd "$SCRIPT_DIR"
"$SCRIPT_DIR/%s" "$@"
        ]], env_sh, name))
        f:close()
        os.chmod(sh_file_path, 755)

        -- the env setup script (for use with profilers, etc that want to launch separately)
        local f = io.open(env_file_path, 'w')
        f:write(string.format([[
%s
"$@"
        ]], env_sh))
        f:close()
        os.chmod(env_file_path, 755)
    end
end

local extra_env_paths = {
    "lib",
    "extraLibs",
    "../../target-deps/python",
    "../../target-deps/python/lib",
    "../../target-deps/usd/$CONFIG/bin",
    "../../target-deps/usd/$CONFIG/lib",
}

create_test_runner("test.cpp", "debug", extra_env_paths)
create_test_runner("test.cpp", "release", extra_env_paths)


function create_python_test_runner(name, config, lib_paths, python_paths)

    -- Replace $CONFIG in any of the library and python paths with the configuration which
    -- is now available.
    local lib_config_paths = {}
    for k, v in ipairs(lib_paths) do
        lib_config_paths[k] = string.gsub(v, "$CONFIG", config)
    end
    local python_config_paths = {}
    for k, v in ipairs(python_paths) do
        python_config_paths[k] = string.gsub(v, "$CONFIG", config)
    end

    if os.target() == "windows" then

        local bat_file_dir = root.."/_build/windows-x86_64/"..config
        local bat_file_path = bat_file_dir.."/"..name..".bat"
        local env_file_path = bat_file_dir.."/"..name..".env.bat"
        -- Prepend (rather than append) so build artifacts beat any system-wide
        -- USD/Python install on PATH/PYTHONPATH. Some developer machines have
        -- e.g. C:\USD_<ver>\lib on PATH, whose tf.dll would otherwise shadow
        -- our target-deps copy and break _tf.pyd loading.
        local env_batch = string.format([[
set TEST_RUN_PATH=%s
set TEST_RUN_PATH=%%TEST_RUN_PATH:/=\%%
set TEST_RUN_PATH=%%TEST_RUN_PATH:\\=\%%
echo Prepending PATH env with: %%TEST_RUN_PATH%%
set PATH=%%TEST_RUN_PATH%%;%%PATH%%
set TEST_PY_PATH=%s
set TEST_PY_PATH=%%TEST_PY_PATH:/=\%%
set TEST_PY_PATH=%%TEST_PY_PATH:\\=\%%
echo Prepending PYTHONPATH env with: %%TEST_PY_PATH%%
set PYTHONPATH=%%TEST_PY_PATH%%;%%PYTHONPATH%%
        ]], to_env_paths(lib_config_paths, "%~dp0"), to_env_paths(python_config_paths, "%~dp0"))

        -- the actual test runner
        local win_test_dir = "%~dp0/tests/"..name
        local win_python_bin = '"%~dp0/../../target-deps/python/python.exe"'
        -- The asset_validator integration tests need omniverse-asset-validator
        -- importable from the bundled Python. Install on first run only.
        local win_ensure_av = string.format([[
%s -c "import omni.asset_validator" >NUL 2>&1
if errorlevel 1 (
    %s -m pip install --quiet --disable-pip-version-check "omniverse-asset-validator>=1.15.1"
)
]], win_python_bin, win_python_bin)
        -- Forward extra args (%*) to run_discover.py so callers can run
        -- individual tests, e.g.:
        --     test.python.bat test_operation_pivot
        --     test.python.bat -k pivot
        local win_discover_cmd = string.format(
            '%s "%s/run_discover.py" "%s" %%*',
            win_python_bin, win_test_dir, win_test_dir)
        local f = io.open(bat_file_path, 'w')
        f:write("@echo off\nsetlocal\n"
            ..env_batch.."\n"
            .."cd \"%~dp0\"\n"
            ..win_ensure_av.."\n"
            ..win_discover_cmd.."\n")
        f:close()

        -- the env setup script (for use with profilers, etc that want to launch separately)
        local f = io.open(env_file_path, 'w')
        f:write(string.format([[
@echo off
%s
%%*
        ]], env_batch))
        f:close()

    else

        -- platform-target is preferred, but repo_build doesn't always pass this.
        -- use platform-host as a fallback
        local platform = _OPTIONS["platform-target"] or platform_host or _OPTIONS["platform-host"]
        if platform == nil then
            error("--platform-target, --platform-host or platform_host must be specified")
        end

        local sh_file_dir = root.."/_build/"..platform.."/"..config
        local sh_file_path = sh_file_dir.."/"..name..".sh"
        local run_script_file_path = sh_file_dir.."/run_python_script.sh"
        local env_file_path = sh_file_dir.."/"..name..".env.sh"

        -- Prepend (rather than append) so build artifacts beat any system-wide
        -- USD/Python install on LD_LIBRARY_PATH/PYTHONPATH.
        local env_sh = string.format([[
#!/bin/bash
set -e
SCRIPT_DIR=$(realpath $(dirname ${BASH_SOURCE}))
export LD_LIBRARY_PATH=%s${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PYTHONPATH=%s${PYTHONPATH:+:$PYTHONPATH}
]],
            to_env_paths(lib_config_paths, "$SCRIPT_DIR"),
            to_env_paths(python_config_paths, "$SCRIPT_DIR")
        )

        -- test runner
        local test_dir = "$SCRIPT_DIR/tests/"..name
        local python_bin = string.format(
            '"$SCRIPT_DIR/../../target-deps/python/bin/python%s"',
            PYTHON_VERSION)
        -- The asset_validator integration tests need omniverse-asset-validator
        -- importable from the bundled Python. Install on first run only.
        local ensure_av = string.format([[
%s -c "import omni.asset_validator" >/dev/null 2>&1 || \
    %s -m pip install --quiet --disable-pip-version-check "omniverse-asset-validator>=1.15.1"
]], python_bin, python_bin)
        -- Forward extra args ("$@") to run_discover.py so callers can run
        -- individual tests, e.g.:
        --     ./test.python.sh test_operation_pivot
        --     ./test.python.sh -k pivot
        local discover_cmd = string.format(
            '%s "%s/run_discover.py" "%s" "$@"',
            python_bin, test_dir, test_dir)
        local f = io.open(sh_file_path, 'w')
        f:write(string.format([[
%s
cd "$SCRIPT_DIR"
%s
%s
        ]], env_sh, ensure_av, discover_cmd))
        f:close()
        os.chmod(sh_file_path, 755)

        -- helpful run python script, that sets up the python env and uses the
        -- correct python executable to run the passed in script
        local f = io.open(run_script_file_path, 'w')
        f:write(string.format([[
%s
cd "$SCRIPT_DIR"
%s
%s $@
        ]], env_sh, ensure_av, python_bin))
        f:close()
        os.chmod(run_script_file_path, 755)

        -- the env setup script (for use with profilers, etc that want to launch separately)
        local f = io.open(env_file_path, 'w')
        f:write(string.format([[
%s
"$@"
        ]], env_sh))
        f:close()
        os.chmod(env_file_path, 755)

    end

end

-- USD's lib/ directory must be on PATH (Windows) / LD_LIBRARY_PATH (Linux)
-- so that the dynamically loaded _tf, _usd, etc. Python extensions can resolve
-- their dependencies on tf.dll / libtf.so etc. The python_py_paths entry alone
-- only handles import resolution; it does not put the native libs on the
-- linker's search path.
local python_lib_paths = {
    "lib",
    "extraLibs",
    "../../target-deps/usd/$CONFIG/lib",
}

local python_py_paths = {
    "python",
    "../../target-deps/usd/release/lib/python",
}

create_python_test_runner("test.python", "release", python_lib_paths, python_py_paths)
create_python_test_runner("test.python", "debug", python_lib_paths, python_py_paths)


group "tests"
    project_common "test.data"
        dependson("omni.scene.optimizer.core")

        kind "Utility"

        so_build.symlink_folder({
            target_dir = "tests/data",
            source_dir = "data",
        })

    project_common "test.python"
        dependson("omni.scene.optimizer.core")

        kind "Utility"

        so_build.symlink_folder({
            target_dir = "tests/test.python",
            source_dir = "test.python",
        })

    project_common "test.cpp"
        dependson("omni.scene.optimizer.core")

        kind "ConsoleApp"

        includedirs {
            "%{target_deps}/doctest/include",
            "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/include",
        }

        externalincludedirs {
            "%{target_deps}/usd/%{config}/include",
        }

        libdirs {
            "%{target_deps}/usd/%{config}/lib"
        }

        files {
            "test.cpp/**.h",
            "test.cpp/**.cpp"
        }

        add_usd { "arch", "gf", "js", "sdf", "tf", "usd", "usdGeom", "usdPhysics", "usdShade", "vt", "usdUtils" }
        add_usd { "usdLux", "plug", "python" }

        so_build.use_python()
        so_build.use_pybind()

        -- Link against the actual scene optimizer shared lib
        so_build.use_so_core()

        -- Turn on some options for doctest
        defines { "DOCTEST_CONFIG_TREAT_CHAR_STAR_AS_STRING", "DOCTEST_CONFIG_SUPER_FAST_ASSERTS" }

        -- Inform the tests which python version we're building with
        defines { "SCENE_OPTIMIZER_PYTHON_VERSION=\""..PYTHON_VERSION.."\"" }

        filter { "system:windows" }
            -- Given we compile all our plugins with the exact same compiler as the
            -- core library, every time, we disable this in order to simplify some
            -- code. For example, in the OmniOperation derived classes, this lets us
            -- simplify some code instead of hacking around dll boundary issues.
            disablewarnings { "4251" } -- (warning C4251: needs to have a dll-interface)
        filter {}

        targetdir ("%{bin_dir}")
        location (workspace_dir.."/%{prj.name}")
        language "C++"
        staticruntime "Off"
        exceptionhandling "On"
        rtti "On"

        enable_gcov()

        filter { "configurations:debug" }
            runtime "Debug"
        filter { "configurations:release" }
            runtime "Release"
        filter {}

        filter { "system:windows" }
            debugenvs { "PATH=%PATH%;"..to_env_paths(extra_env_paths, root.."/_build/windows-x86_64/"..config) }
        filter { "system:linux" }
            buildoptions { "-pthread" }
            links { "pthread" }
            add_usd { "ar", "pcp", "js", "trace", "usdSkel", "work", "kind", "sdr", "usdLux", "usdVol" }
            removeflags { "FatalCompileWarnings", "UndefinedIdentifiers" }

            filter { "configurations:debug" }
                links {"tbb_debug", "tbbmalloc_debug"}
            filter { "configurations:release" }
                links {"tbb", "tbbmalloc"}
            filter {}

        filter {}

    -- Test helper shared library for testing isCudaAvailable() threading
    project_common "TestCudaUtils"
        dependson("omni.scene.optimizer.core")

        kind "SharedLib"

        includedirs {
            "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/include",
        }

        files {
            "test.cuda.utils/TestCudaUtils.h",
            "test.cuda.utils/TestCudaUtils.cpp"
        }


        -- Link against the actual scene optimizer shared lib
        so_build.use_so_core()

        externalincludedirs {
            "%{target_deps}/usd/%{config}/include", -- for TBB
        }

        libdirs {
            "%{target_deps}/usd/%{config}/lib" -- for TBB
        }

        targetdir ("%{bin_dir}")
        location (workspace_dir.."/%{prj.name}")
        language "C++"
        staticruntime "Off"
        exceptionhandling "On"
        rtti "On"

        enable_gcov()

        filter { "configurations:debug" }
            runtime "Debug"
        filter { "configurations:release" }
            runtime "Release"
        filter {}

        filter { "system:linux" }
            buildoptions { "-pthread" }
            links { "pthread" }
            removeflags { "FatalCompileWarnings", "UndefinedIdentifiers" }

            filter { "configurations:debug" }
                links {"tbb_debug", "tbbmalloc_debug"}
            filter { "configurations:release" }
                links {"tbb", "tbbmalloc"}
            filter {}
        filter {}
