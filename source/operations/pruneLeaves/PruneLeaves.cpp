// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "PruneLeaves.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/usd/ar/resolverScopedCache.h>
#include <pxr/usd/usd/primCompositionQuery.h>

// C++
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::PruneLeavesOperation);


namespace omni::scene::optimizer
{

// Constants
constexpr const char* s_pruneLeaves = "PRUNE_LEAVES";

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (Xform)
    (Scope)
);
// LCOV_EXCL_STOP
// clang-format on


// Returns whether a prim has a reference composition ARC attached to it
static bool _isReference(const UsdPrim& prim)
{
    UsdPrimCompositionQuery compositionQuery = UsdPrimCompositionQuery::GetDirectReferences(prim);
    for (const auto& arc : compositionQuery.GetCompositionArcs())
    {
        if (arc.GetArcType() == PcpArcTypeReference)
        {
            return true;
        }
    }

    return false;
}


/// Returns whether a prim is a grouping primitive.
static bool _isGroupingPrim(const UsdPrim& prim)
{
    const TfToken& typeName = prim.GetTypeName();
    if (typeName == _tokens->Xform || typeName == _tokens->Scope)
    {
        return true;
    }

    return false;
}


PruneLeavesOperation::PruneLeavesOperation()
    : Operation("pruneLeaves",
                "Prune Leaves",
                "This operation finds and prunes any leaf grouping primitives found in a stage (for example "
                "Xform, Scope).")
{

    addArgument("paths", "Prim Paths to Search", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_primPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("pruneMode", "Method", kDisplayTypeEnum, "How to prune any leaf prims that are found", m_mode)
        .setEnumValues<RemoveMethod>({ { RemoveMethod::eDelete, "Delete" },
                                       { RemoveMethod::eDeactivate, "Deactivate" },
                                       { RemoveMethod::eHide, "Hide" } });

    addArgument("filterInactive",
                "Filter Inactive Prims",
                kDisplayTypeBool,
                "Do not consider inactive prims empty",
                m_filterInactive);
}


std::string PruneLeavesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion PruneLeavesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string PruneLeavesOperation::getCategory() const
{
    return s_pruneLeaves;
}


std::string PruneLeavesOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


bool PruneLeavesOperation::getSupportsAnalysis() const
{
    return true;
}


void PruneLeavesOperation::setPrimsFromPaths(const std::vector<std::string>& primPaths)
{
    m_prims.clear();

    // If there are no paths, then don't resolve the entire stage.
    if (primPaths.empty())
    {
        return;
    }

    // Consider parent paths before children to ensure correct deletion order.
    std::vector<std::string> sortedPaths(primPaths);
    std::sort(sortedPaths.begin(), sortedPaths.end());

    bool meshesOnly = false;
    m_prims = _resolveExpressionsToPrims(getUsdStage(), sortedPaths, meshesOnly);
}


static OperationResult reportAnalysis(const std::vector<UsdPrim>& leaves)
{

    JsObject resultJson;
    resultJson["analysis"] = _toJson(leaves);

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    SO_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult PruneLeavesOperation::executeAnalysisImpl()
{
    return executeImpl();
}


OperationResult PruneLeavesOperation::executeImpl()
{

    if (!getUsdStage())
    {
        SO_LOG_ERROR("No usd stage.");
        return { false };
    }

    std::vector<UsdPrim> leaves;

    // Convert paths to prims
    setPrimsFromPaths(m_primPaths);

    // Add a scoped asset resolver cache to improve performance
    ArResolverScopedCache resolverScopedCache;

    // Default to all prims, but optionally require them to be active.
    Usd_PrimFlagsPredicate predicate = UsdPrimAllPrimsPredicate;

    if (m_filterInactive)
    {
        predicate = UsdPrimIsActive;
    }

    if (!m_prims.empty())
    {
        // Find all leaves from the provided starting paths. Searching will start at these paths and continue onwards.
        // Leaf paths that remain after pruning above these paths will not be considered for removal.
        for (const UsdPrim& prim : m_prims)
        {
            findLeaves(prim, UsdTraverseInstanceProxies(predicate), leaves);
        }
    }
    else
    {
        // Find any unnecessary leaf primitives, using the pseudo root as the start point.
        findLeaves(getUsdStage()->GetPseudoRoot(), UsdTraverseInstanceProxies(predicate), leaves);
    }

    // We now have the leaves so for analysis we can return a report.
    if (getContext()->analysisMode)
    {
        return reportAnalysis(leaves);
    }

    // If not in analysis, then we can prune any leaves according to the specified option.
    if (!leaves.empty())
    {
        // Filter out any instance proxies
        leaves.erase(
            std::remove_if(leaves.begin(), leaves.end(), [](const UsdPrim& prim) { return prim.IsInstanceProxy(); }),
            leaves.end());

        SdfChangeBlock _changeBlock;

        _removePrims(m_mode, getUsdStage(), leaves);

        if (getContext()->verbose)
        {
            for (const UsdPrim& leaf : leaves)
            {
                SO_LOG_VERBOSE("Pruning %s", leaf.GetPrimPath().GetString().c_str());
            }
        }

        std::ostringstream oss;
        oss << (m_mode == RemoveMethod::eDelete ? "Deleted" : "Deactivated") << " " << leaves.size();
        oss << (leaves.size() == 1 ? " leaf." : " leaves.");

        SO_LOG_INFO(oss.str().c_str());
    }
    else
    {
        SO_LOG_INFO("Did not find any leaves to prune.");
    }

    return { true };
}

bool PruneLeavesOperation::findLeaves(const UsdPrim& prim,
                                      Usd_PrimFlagsPredicate predicate,
                                      std::vector<UsdPrim>& leafPrims) const
{

    // Collect any leaf group children of this prim. If all the children end up being leaf grouping prims
    // then we can disregard this and instead consider the prim itself a leaf. If not, we will copy this
    // to the output parameter later.
    std::vector<UsdPrim> leaves;

    bool allLeaves = true;
    auto primRange = prim.GetFilteredChildren(predicate);

    for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
    {
        const auto& child = (*iter);

        bool childIsGroupingPrim = _isGroupingPrim(child);

        // Need to handle references a little differently. We don't want to return results from within a reference,
        // but if a reference itself contains only leaves then we can remove it.
        if (childIsGroupingPrim && _isReference(child))
        {
            // Recurse in to this child to see if it is a leaf. Note we don't use the output referenceLeaves, we only
            // care about whether the reference itself is a leaf grouping prim.
            //
            // Also need to traverse in to instance proxies as this might be an instance.
            std::vector<UsdPrim> referenceLeaves;
            bool isLeafReference = findLeaves(child, UsdTraverseInstanceProxies(), referenceLeaves);

            // If the child reference only contains other leaf grouping prims then we can remove the reference itself.
            if (isLeafReference)
            {
                leaves.push_back(child);
            }
            else
            {
                // The reference contains something other than xforms. Don't do anything with its contents, just
                // mark that not all the children are leaves and carry on.
                allLeaves = false;
            }

            continue;
        }

        // Recursively find any leaf grouping prims of this child
        std::vector<UsdPrim> childLeaves;
        bool allChildrenLeaves = findLeaves(child, predicate, childLeaves);

        // If all the children are leaf grouping prims then we can treat this child as a leaf itself (an xform
        // with _only_ other leaf xforms underneath it).
        if (allChildrenLeaves && childIsGroupingPrim)
        {
            leaves.push_back(child);
        }
        else
        {
            // If the children are not all leaf grouping prims, or this child isn't one, then collect whatever is
            // in our local leaves and mark that this prim is not overall a leaf.
            leaves.insert(leaves.end(), childLeaves.begin(), childLeaves.end());
            allLeaves = false;
        }
    }

    // Once we have finished processing the children we know whether this prim is technically a leaf grouping prim (eg
    // it's an xform with nothing but leaf xforms underneath it). If that's the case we just append this prim to the
    // output. If not, then append the local leaves result.
    if (allLeaves && _isGroupingPrim(prim))
    {
        leafPrims.push_back(prim);
    }
    else
    {
        leafPrims.insert(leafPrims.end(), leaves.begin(), leaves.end());

        // Adjust return value as not everything is a leaf
        allLeaves = false;
    }

    return allLeaves;
}

} // namespace omni::scene::optimizer
