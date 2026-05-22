cmake_minimum_required(VERSION 3.22)
project(scene-optimizer-core)

set(CMAKE_CXX_STANDARD 17)

# IDE helper only — the real build uses Premake (premake5.lua).
# This file enables header resolution in CMake-based editors (CLion, VS Code CMake Tools).

file(GLOB_RECURSE SOURCES
    *.h
    *.cpp
    *.cu
)

include_directories(
        ./include
        ./include/omni/sceneoptimizer
        ./source/pch
        ./source/core/src
        ./_build/linux-x86_64/release/include
        ./_build/target-deps/usd/release/include
        ./_build/target-deps/carb_sdk_plugins/include
        ./_build/target-deps/pybind11/include
        ./_build/target-deps/python/include/python3.12
        ./_build/target-deps/doctest/include
        ./_build/target-deps/omnimesh_ops_usd/release/include
        ./_build/target-deps/mesh_tools/release/include
        ./_build/target-deps/autouv-core/include
        ./_build/target-deps
)

add_executable(scene-optimizer-core ${SOURCES})
