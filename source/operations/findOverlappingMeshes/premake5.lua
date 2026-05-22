project_with_location("findOverlappingMeshes")
    so_build.use_omni_mesh()
    so_build.use_mesh_tools()
    so_build.operation_plugin({ "*.cpp" })
