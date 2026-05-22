project_with_location("shrinkwrap")

    externalincludedirs {
        target_deps.."/shrinkwrap_openvdb/%{config}/include",
    }

    -- Static OpenVDB suppresses dllimport/dllexport (Platform.h auto-enables OPENVDB_DLL
    -- under /MD CRT). TBB is already linked via use_usd() from operation_plugin.
    defines { "OPENVDB_STATICLIB" }

    filter { "system:windows" }
        links { "libopenvdb" }
    filter { "system:linux" }
        links { "openvdb" }
    filter {}

    libdirs {
        target_deps.."/shrinkwrap_openvdb/%{config}/lib",
    }

    so_build.operation_plugin({ "*.cpp" })

    -- Exclude from gcov: coverage instrumentation causes ~1000x slowdown on
    -- OpenVDB's heavily-templated TBB-parallel code, making tests time out.
    -- The --coverage flag is added once by enable_gcov() in operation_plugin()
    -- and a second time by premake5 internally (possibly from string splitting);
    -- both must be removed.
    removebuildoptions { "--coverage -DGCOV_ENABLED -fno-inline-small-functions" }
    removebuildoptions { "--coverage" }
    removelinkoptions { "--coverage" }
