// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Test Utils
#include "TestUtils.h"

// Carbonite
#include <carb/Framework.h>
#include <carb/extras/Library.h>

#include <filesystem>

// USD
#include <pxr/usd/usdUtils/stageCache.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace omni::scene::optimizer
{

namespace testutils
{

std::string _getTestDataFilePath(const std::string& name)
{
    static bool unused = false;
    static std::string const kLibraryDirectory = carb::extras::getLibraryDirectory(&unused);

    carb::extras::Path path(kLibraryDirectory);
    path /= "tests/data";
    path /= name;

    if (std::filesystem::exists(path.c_str()))
    {
        return path.getString();
    }

    return "";
}


UsdStageRefPtr _openStage(const std::string& name)
{
    UsdStageRefPtr stage;

    if (std::string const path = _getTestDataFilePath(name); !path.empty())
    {
        stage = PXR_NS::UsdStage::Open(path);
    }

    return stage;
}


size_t _getPrimCount(const UsdStageWeakPtr& stage)
{
    size_t prims = 0;
    for (const UsdPrim& prim : stage->TraverseAll())
    {
        ++prims;
    }

    return prims;
}


ExecutionContext _getContext(const UsdStageWeakPtr& stage)
{

    auto stageId = UsdUtilsStageCache::Get().Insert(stage);

    ExecutionContext context;
    context.usdStageId = stageId.ToLongInt();

    return context;
}


} // namespace testutils

} // namespace omni::scene::optimizer
