// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "FindFlatHierarchiesOperation.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>


PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::FindFlatHierarchiesOperation);


namespace omni::scene::optimizer
{


// Constants
constexpr const char* s_category = "FIND_FLAT_HIERARCHIES";


FindFlatHierarchiesOperation::FindFlatHierarchiesOperation()
    : Operation("findFlatHierarchies",
                "Find Flat Hierarchies",
                "Finds prims that have more than a specified number of children.")
{
    addArgument("paths", "Paths", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_paths)
        .setPlaceholder("Add paths or entire scene will be processed");

    addArgument("maxChildren",
                "Max Children",
                kDisplayTypeInt,
                "The maximum number of children a prim can have until it is considered a flat hierarchy.",
                m_maxChildren);

    addArgument("considerAllChildren",
                "Consider All Children",
                kDisplayTypeBool,
                "Whether to consider all children or only active, loaded, defined, non-abstract children.",
                m_considerAllChildren);
}


FindFlatHierarchiesOperation::~FindFlatHierarchiesOperation() = default;


std::string FindFlatHierarchiesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion FindFlatHierarchiesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string FindFlatHierarchiesOperation::getCategory() const
{
    return s_category;
}


std::string FindFlatHierarchiesOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


bool FindFlatHierarchiesOperation::getSupportsAnalysis() const
{
    return true;
}


OperationResult FindFlatHierarchiesOperation::executeImpl()
{
    const std::map<std::string, int> flatHierarchies = findFlatHierarchies();

    // Create result
    OperationResult result{ true, nullptr, nullptr };
    if (!flatHierarchies.empty())
    {
        result.output = _toJsonStr(flatHierarchies);
    }

    return result;
}

OperationResult FindFlatHierarchiesOperation::executeAnalysisImpl()
{
    const std::map<std::string, int> flatHierarchies = findFlatHierarchies();

    // Create result
    OperationResult result{ true, nullptr, nullptr };
    JsObject analysisResult;
    analysisResult["flatHierarchies"] = _toJson(flatHierarchies);
    JsObject resultJson;
    resultJson["analysis"] = analysisResult;
    result.output = getCStr(JsWriteToString(resultJson));

    return result;
}


std::map<std::string, int> FindFlatHierarchiesOperation::findFlatHierarchies()
{
    std::map<std::string, int> flatHierarchies;

    // callback function for _resolveExpressionsToPrims that finds prims with too many children
    ResolveFilter resolveFilter = [&](const UsdPrim& prim, UsdPrimRange::iterator&)
    {
        // resolve the number of children this prim has
        int numChildren = 0;
        if (m_considerAllChildren)
        {
            numChildren = static_cast<int>(prim.GetAllChildrenNames().size());
        }
        else
        {
            numChildren = static_cast<int>(prim.GetChildrenNames().size());
        }

        // does this prim have over the threshold number of children?
        if (numChildren >= m_maxChildren)
        {
            SO_LOG_INFO("Found flat hierarchy at %s with %d children", prim.GetPath().GetAsString().c_str(), numChildren);
            flatHierarchies[prim.GetPath().GetAsString()] = numChildren;
        }

        // always keep iterating
        return false;
    };

    // return the results of _resolveExpressionsToPrims using a custom resolveFilter
    constexpr bool meshesOnly = false;
    constexpr bool reverse = false;
    static const Usd_PrimFlagsPredicate predicate = UsdPrimIsActive && UsdPrimIsLoaded;
    _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(), m_paths, meshesOnly, reverse, predicate, resolveFilter);

    return flatHierarchies;
}


} // namespace omni::scene::optimizer
