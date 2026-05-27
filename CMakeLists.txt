cmake_minimum_required(VERSION 3.22)
project(scene-optimizer-core)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# IDE helper only — the real build uses Premake (premake5.lua).
# This file mirrors the Premake compile flags closely enough for CLion /
# clangd to resolve symbols. Keep it in sync with the include / define set
# emitted in _build/<platform>/<config>/compile_commands.json.

# Scope the source glob to the actual source tree. Letting it run from
# CMAKE_SOURCE_DIR would pull in _build/, deps/, _repo/, etc. (slow indexing,
# double-counted headers).
file(GLOB_RECURSE SOURCES
    source/*.h
    source/*.cpp
    source/*.cu
)

add_executable(scene-optimizer-core ${SOURCES})

# The real build only puts the generated _build/<platform>/<config>/include
# tree on the project's own include path — every source file references its
# siblings via the fully-qualified omni/scene.optimizer/core/<Header>.h path
# that resolves through here. Do NOT add source/core/src directly: that
# creates a parallel header tree that confuses clangd's symbol resolution
# (CLion ends up indexing the source-side copy of Utils.h as a separate
# file from the omni/scene.optimizer/core/Utils.h that everyone includes).
target_include_directories(scene-optimizer-core PRIVATE
    ${CMAKE_SOURCE_DIR}/_build/linux-x86_64/release/include
)

# Third-party deps from packman. Marked SYSTEM so clangd suppresses
# warnings from inside them — matches the -isystem treatment Premake uses.
target_include_directories(scene-optimizer-core SYSTEM PRIVATE
    ${CMAKE_SOURCE_DIR}/_build/target-deps
    ${CMAKE_SOURCE_DIR}/_build/target-deps/usd/release/include
    ${CMAKE_SOURCE_DIR}/_build/target-deps/carb_sdk_plugins/include
    ${CMAKE_SOURCE_DIR}/_build/target-deps/pybind11/include
    ${CMAKE_SOURCE_DIR}/_build/target-deps/python/include/python3.12
    ${CMAKE_SOURCE_DIR}/_build/target-deps/doctest/include
    ${CMAKE_SOURCE_DIR}/_build/target-deps/omnimesh_ops_usd/release/include
    ${CMAKE_SOURCE_DIR}/_build/target-deps/mesh_tools/release/include
    ${CMAKE_SOURCE_DIR}/_build/target-deps/autouv-core/include
)

# Match the real release-build defines so PXR / TBB / etc. expand the same
# way clangd sees them as during the actual compile.
target_compile_definitions(scene-optimizer-core PRIVATE NDEBUG)
