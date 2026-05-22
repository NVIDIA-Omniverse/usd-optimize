// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "UtilityFunction.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Log.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primCompositionQuery.h>

PXR_NAMESPACE_USING_DIRECTIVE


SO_PLUGIN_INIT(omni::scene::optimizer::UtilityFunctionOperation);


namespace omni::scene::optimizer
{


// Constants
constexpr const char* s_category = "UTILITY_FUNCTION";


UtilityFunctionOperation::UtilityFunctionOperation()
    : Operation(
          "utilityFunction",
          "Utility Function",
          "This operation contains a number of smaller functions that don't necessarily need a full operation of their "
          "own. Generally this would mean they are a simple process that does not require any real configuration.")
{

    // Prim Paths
    addArgument("primPaths", "Prim Paths", kDisplayTypePrimPaths, "A list of prim paths to consider", m_paths);

    // Function Type
    auto& arg = addArgument("function", "Function", kDisplayTypeEnum, "The type of function to run", m_functionType);
    arg.setEnumValues<UtilityFunctionType>({
        { UtilityFunctionType::eDeinstance, "Deinstance" },
        { UtilityFunctionType::eUnbindMaterials, "Unbind Materials" },
        { UtilityFunctionType::eSetInstanceable, "Set Instanceable" },
        { UtilityFunctionType::eFlattenInstance, "Flatten Instances" },
    });
}


std::string UtilityFunctionOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion UtilityFunctionOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string UtilityFunctionOperation::getCategory() const
{
    return s_category;
}


bool UtilityFunctionOperation::deinstance(const std::vector<UsdPrim>& prims)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|Utility|deinstance");

    std::string suffix = prims.size() == 1 ? "" : "s";
    SO_LOG_INFO("Running deinstance on %zu prim%s", prims.size(), suffix.c_str());

    size_t processed = 0;

    SdfChangeBlock changeBlock;

    for (const auto& prim : prims)
    {
        if (prim.IsInstanceable())
        {
            prim.SetInstanceable(false);
            ++processed;
        }
    }

    suffix = processed == 1 ? "" : "s";
    SO_LOG_INFO("Deinstanced %zu prim%s", processed, suffix.c_str());

    return true;
}


bool UtilityFunctionOperation::unbindMaterials(const std::vector<UsdPrim>& prims)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|Utility|unbindMaterials");

    std::string suffix = prims.size() == 1 ? "" : "s";
    SO_LOG_INFO("Running unbindMaterials on %zu prim%s", prims.size(), suffix.c_str());

    size_t processed = 0;

    SdfChangeBlock changeBlock;

    for (const auto& prim : prims)
    {
        UsdShadeMaterialBindingAPI bindingAPI(prim);

        // This abomination is purely just to have a useful "processed" count, since we are just
        // going to blindly call UnbindAllBindings anyway.
        auto directBinding = bindingAPI.GetDirectBinding();
        if (directBinding.GetMaterial())
        {
            ++processed;
        }
        else
        {
            auto collectionBindings = bindingAPI.GetCollectionBindings();
            if (!collectionBindings.empty())
            {
                ++processed;
            }
        }

        // The actual unbinding
        bindingAPI.UnbindAllBindings();
    }

    suffix = processed == 1 ? "" : "s";
    SO_LOG_INFO("Removed %zu material binding%s", processed, suffix.c_str());

    return true;
}


bool UtilityFunctionOperation::makePrimInstanceable(const UsdPrim& prim)
{
    // Check each direct child of this prim. If any of them have more than one opinion
    // then we don't want to set it instanceable - it means something other than the
    // reference is providing an opinion to it, meaning it can't be an instance as we
    // assume it's modified from the original in some way.
    auto primRange = prim.GetChildren();

    // First sanity check there are children. If this is a prim with a reference that
    // contains no children, don't bother setting it instanceable.
    if (primRange.empty())
    {
        return false;
    }

    // Check each immediate child and see if there is more than one opinion on it. If so,
    // we can't make it instanceable.
    for (const auto& child : primRange)
    {
        const SdfPrimSpecHandleVector& primStack = child.GetPrimStack();
        if (primStack.size() > 1)
        {
            return false;
        }
    }

    // No other opinions so we are good to enable.
    prim.SetInstanceable(true);

    if (getContext()->verbose)
    {
        SO_LOG_VERBOSE("Set %s instanceable", prim.GetPrimPath().GetAsString().c_str());
    }

    return true;
}


static bool _hasDirectComposition(const UsdPrim& prim)
{
    // Query the composition.
    UsdPrimCompositionQuery compositionQuery(prim);

    for (const auto& arc : compositionQuery.GetCompositionArcs())
    {
        // Skip anything ancestral - we only care to know about direct references.
        if (arc.IsAncestral())
        {
            continue;
        }

        // Currently support reference and payload
        if (arc.GetArcType() == PcpArcTypeReference || arc.GetArcType() == PcpArcTypePayload)
        {
            return true;
        }
    }

    return false;
}


bool UtilityFunctionOperation::setInstanceable(const std::vector<UsdPrim>& prims)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|Utility|setInstanceable");

    std::string suffix = prims.size() == 1 ? "" : "s";
    SO_LOG_INFO("Running setInstanceable on %zu prim%s", prims.size(), suffix.c_str());

    size_t processed = 0;

    // Iterate each prim and check if we can set instanceable.
    // Note: This does NOT happen within a changeBlock. If we set something instanceable we want
    // the change to be reflected so that we can skip its children etc.
    for (const auto& prim : prims)
    {
        // As we traverse we may have set a prim to instanceable meaning that subsequent prims in the
        // list are now expired as they've been replaced with instance proxies.
        if (!prim)
        {
            continue;
        }

        // Skip already instanceable prims or instance proxies
        if (prim.IsInstanceable() || prim.IsInstanceProxy())
        {
            continue;
        }

        // Check if this prim has some kind of direct composition (reference, payload). If so, try and
        // make it instanceable.
        if (_hasDirectComposition(prim))
        {
            if (makePrimInstanceable(prim))
            {
                ++processed;
            }
        }
    }

    suffix = processed == 1 ? "" : "s";
    SO_LOG_INFO("Set %zu prim%s to instanceable", processed, suffix.c_str());

    return true;
}


bool UtilityFunctionOperation::flattenInstances(const std::vector<UsdPrim>& prims)
{

    CARB_PROFILE_ZONE(0, "SceneOptimizer|Utility|flattenInstances");

    std::string suffix = prims.size() == 1 ? "" : "s";
    SO_LOG_INFO("Running flatten on %zu prim%s", prims.size(), suffix.c_str());

    size_t flattened = 0;

    for (const auto& prim : prims)
    {
        if (prim.IsInstance())
        {
            _flattenInstance(prim);
            SO_LOG_VERBOSE("Flattened %s", prim.GetPrimPath().GetAsString().c_str());
            ++flattened;
        }
    }

    suffix = flattened == 1 ? "" : "s";
    SO_LOG_INFO("Flattened %d instance%s", flattened, suffix.c_str());

    return true;
}


OperationResult UtilityFunctionOperation::executeImpl()
{
    // Resolve paths
    bool meshesOnly = false;
    bool reverse = false;
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(getUsdStage(), m_paths, meshesOnly, reverse);

    // Default pessimism
    OperationResult result{ false, nullptr, nullptr };

    switch (m_functionType)
    {
    case UtilityFunctionType::eDeinstance:
        result.success = deinstance(prims);
        break;
    case UtilityFunctionType::eUnbindMaterials:
        result.success = unbindMaterials(prims);
        break;
    case UtilityFunctionType::eSetInstanceable:
        result.success = setInstanceable(prims);
        break;
    case UtilityFunctionType::eFlattenInstance:
        result.success = flattenInstances(prims);
        break;
    default:
        SO_LOG_WARN("Unknown utility specified: %d", static_cast<int>(m_functionType));
    }

    return result;
}


} // namespace omni::scene::optimizer
