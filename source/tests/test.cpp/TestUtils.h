// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>

// USD
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/stage.h>

// Doctest
#include <doctest/doctest.h>


// Workaround for ambiguous operator<< overload when using REQUIRE(TfRefPtr<T>)
// with doctest.
template <>
struct doctest::StringMaker<PXR_NS::UsdStageRefPtr>
{
    static String convert(PXR_NS::UsdStageRefPtr)
    {
        return "nullptr";
    }
};


namespace omni::scene::optimizer
{

namespace testutils
{

/// Get the path for a file within the test data directory of omni.scene.optimizer.core extension
std::string _getTestDataFilePath(const std::string& name);


/// Open the stage and return it if succeeded
PXR_NS::UsdStageRefPtr _openStage(const std::string& name);

/// Get the number of prims in a stage.
size_t _getPrimCount(const PXR_NS::UsdStageWeakPtr& stage);

/// Get an execution context, ensuring the stage is added to the stage cache
ExecutionContext _getContext(const PXR_NS::UsdStageWeakPtr& stage);

/// Count prims in a stage matching a given IsA type
template <typename T>
size_t _countPrimsOfType(const PXR_NS::UsdStageWeakPtr& stage)
{
    size_t result = 0;
    for (const auto& prim : stage->TraverseAll())
    {
        if (prim.IsA<T>())
        {
            ++result;
        }
    }

    return result;
}

} // namespace testutils

} // namespace omni::scene::optimizer
