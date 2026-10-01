usd_optimize_build = require("tools/premake/usd-optimize-public")


project_with_location("usd_optimize.core")

    -- build the shared library for usd optimize core
    usd_optimize_build.shared_library({
        library_name = "core",
        headers = { "src/**/*.h" },
        sources = { "src/**.cpp" }
    })

    local lib_dir = "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/lib"

    -- Copy third-party libs into the lib dir so they will be picked up at runtime.
    repo_build.prebuild_copy {
        {"%{root}/_build/target-deps/autouv-core/%{config}/lib/*", lib_dir},
        {"%{root}/_build/target-deps/omnimesh_ops_usd/%{config}/lib/*", lib_dir},
        {"%{root}/_build/target-deps/mesh_tools/%{config}/lib/*", lib_dir},
        -- Operation name/attribute mapping
        {"config/operation_mapping.json", lib_dir},
    }

    -- Copy USD and Python libs to an extraLibs dir for testing
    local extra_dir = "%{root}/_build/%{cfg.system}-%{cfg.platform}/%{config}/extraLibs"
    repo_build.prebuild_copy{
        {target_deps.."/usd/%{config}/lib/*", extra_dir},
        {target_deps.."/usd/%{config}/lib/usd", extra_dir.."/usd"}
    }

    -- USD 26.08+ only: separate oneTBB/MaterialX packages, missed by the USD glob above.
    repo_build.prebuild_copy{
        {target_deps.."/tbb/%{config}/lib/*", extra_dir},
        {target_deps.."/materialx/%{config}/lib/*", extra_dir},
    }

    -- A couple of extra libs that are found in different places on windows.
    -- Glob tbb*.dll: USD 25.11 ships oneTBB (tbb12.dll), USD 25.05 classic TBB (tbb.dll).
    -- Glob MaterialX*.dll: usd_usdMtlx.dll comes from usd/lib, its MaterialX runtime from
    -- usd/bin; without both the plugin cannot load from extraLibs.
    if os.target() == "windows" then
        repo_build.prebuild_copy{
            {target_deps.."/usd/%{config}/bin/tbb*.dll", extra_dir},
            {target_deps.."/usd/%{config}/bin/MaterialX*.dll", extra_dir},
            -- 26.08+ puts the .lib in lib/ but the .dll in bin/; without these, 0xC0000135.
            {target_deps.."/tbb/%{config}/bin/tbb*.dll", extra_dir},
            {target_deps.."/materialx/%{config}/bin/MaterialX*.dll", extra_dir},
            {target_deps.."/python/python"..string.gsub(PYTHON_VERSION, "%.", "")..".dll", extra_dir},
        }
    end


project_with_location("core_python")

    dependson( "usd_optimize.core" )

    -- build the python bindings for usd optimize core
    usd_optimize_build.use_python()
    usd_optimize_build.use_usd()
    usd_optimize_build.use_mesh_tools()
    usd_optimize_build.use_omni_mesh()
    usd_optimize_build.use_usd_optimize_core()

    -- `module_name` is set explicitly because the helper's auto-derivation
    -- (`bindings_module_name:gsub("_", ".")`) would split `usd_optimize`
    -- into `usd.optimize` — the package is a single dotted segment.
    usd_optimize_build.python_bindings({
        module_name = "usd_optimize.impl.core",
        bindings_module_name = "usd_optimize_impl_core",
        bindings_sources = "bindings/BindingsPython.cpp",
        python_sources = "python/usd_optimize/impl/core/*.py",
    })

    usd_optimize_build.symlink_folder({
        target_dir = "python/usd_optimize/core",
        source_dir = "python/usd_optimize/core",
    })

    -- Back-compat shim: `import omni.scene.optimizer.<sub>` is transparently
    -- aliased to `usd_optimize.<sub>` by a sys.meta_path finder installed in
    -- the shim's __init__.py. Drop this symlink once the deprecation window
    -- closes.
    usd_optimize_build.symlink_folder({
        target_dir = "python/omni/scene/optimizer",
        source_dir = "python/omni/scene/optimizer",
    })