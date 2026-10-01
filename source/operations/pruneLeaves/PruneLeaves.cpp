// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "PruneLeaves.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/JsonUtils.h>
#include <usd_optimize/core/RemovePrims.h>
#include <usd_optimize/core/ResolveSdfPaths.h>
#include <usd_optimize/core/Utils.h>

// USD
#include <pxr/usd/ar/resolverScopedCache.h>
#include <pxr/usd/pcp/node.h>
#include <pxr/usd/pcp/primIndex.h>

// C++
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::PruneLeavesOperation);


namespace usd_optimize
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


// Returns whether a prim directly composes a reference arc.
//
// Two deliberate choices here:
//  - We walk the cached GetPrimIndex() rather than HasAuthoredReferences() (which would also count an empty or
//    fully-deleted "references" list op that composes no arc) or UsdPrimCompositionQuery (which recomputes an
//    expanded prim index on every call - far too costly to run per grouping prim during traversal).
//  - !IsDueToAncestor() restricts us to references introduced at this prim, matching the original
//    GetDirectReferences "Direct" filter, so a prim is not flagged just for living inside a referenced subtree.
static bool _isReference(const UsdPrim& prim)
{
    for (const PcpNodeRef& node : prim.GetPrimIndex().GetNodeRange())
    {
        if (node.GetArcType() == PcpArcTypeReference && !node.IsDueToAncestor())
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


/// Returns whether a prim has an authored payload that has not been loaded.
///
/// A prim with an unloaded payload may compose meaningful content that is not currently present on the stage. Because
/// we cannot see what it would contribute, such a prim must never be treated as an empty leaf and pruned.
static bool _hasUnloadedPayload(const UsdPrim& prim)
{
    return prim.HasAuthoredPayloads() && !prim.IsLoaded();
}


PruneLeavesOperation::PruneLeavesOperation()
    : Operation("pruneLeaves", "Prune Leaves", "Prune unnecessary leaf grouping prims (``Scope``, ``Xform``) from a stage.")
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

    addArgument("preserveUnloadedPayloads",
                "Preserve Unloaded Payloads",
                kDisplayTypeBool,
                "Do not prune leaf prims that carry an unloaded payload (they may contribute content once loaded). "
                "Disable to prune them anyway.",
                m_preserveUnloadedPayloads);
}


std::string PruneLeavesOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion PruneLeavesOperation::getVersion() const
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

    USD_OPTIMIZE_LOG_VERBOSE("Analysis result: %s", result.output);

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
        USD_OPTIMIZE_LOG_ERROR("No usd stage.");
        return { false };
    }

    // 'Ignore' removes nothing, so running it as a real optimization is pointless work. In analysis mode the removal
    // method is irrelevant (we only report the leaves we would prune), so it is allowed there.
    if (m_mode == RemoveMethod::eIgnore && !getContext()->analysisMode)
    {
        USD_OPTIMIZE_LOG_ERROR("pruneMode 'Ignore' would remove nothing. Use Delete, Deactivate, or Hide.");
        return { false };
    }

    std::vector<UsdPrim> leaves;

    // Per-prototype leaf-ness is memoized for the duration of a single run. Clear it up front so that repeated
    // executions (whose predicate/payload settings may differ) never reuse a stale answer.
    m_prototypeLeafCache.clear();

    // Convert paths to prims
    setPrimsFromPaths(m_primPaths);

    // Add a scoped asset resolver cache to improve performance
    ArResolverScopedCache resolverScopedCache;

    // Undefined prims (pure overs) compose to nothing, so they never count as content -- otherwise overs left
    // behind by a removed reference target keep the referencing prim alive. Inactive prims are real defs and
    // stay user-controlled via filterInactive.
    Usd_PrimFlagsPredicate predicate = UsdPrimIsDefined;

    if (m_filterInactive)
    {
        predicate = UsdPrimIsDefined && UsdPrimIsActive;
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
                USD_OPTIMIZE_LOG_VERBOSE("Pruning %s", leaf.GetPrimPath().GetString().c_str());
            }
        }

        const char* verb = "Pruned";
        switch (m_mode)
        {
        case RemoveMethod::eDelete:
            verb = "Deleted";
            break;
        case RemoveMethod::eDeactivate:
            verb = "Deactivated";
            break;
        case RemoveMethod::eHide:
            verb = "Hid";
            break;
        default:
            break;
        }

        std::ostringstream oss;
        oss << verb << " " << leaves.size();
        oss << (leaves.size() == 1 ? " leaf." : " leaves.");

        USD_OPTIMIZE_LOG_INFO(oss.str().c_str());
    }
    else
    {
        USD_OPTIMIZE_LOG_INFO("Did not find any leaves to prune.");
    }

    return { true };
}

bool PruneLeavesOperation::findLeaves(const UsdPrim& prim,
                                      const Usd_PrimFlagsPredicate& predicate,
                                      std::vector<UsdPrim>& leafPrims) const
{

    // Collect any leaf group children of this prim. If all the children end up being leaf grouping prims
    // then we can disregard this and instead consider the prim itself a leaf. If not, we will copy this
    // to the output parameter later.
    std::vector<UsdPrim> leaves;
    bool allLeaves = findChildLeaves(prim, predicate, leaves);

    // Once we have finished processing the children we know whether this prim is technically a leaf grouping prim (eg
    // it's an xform with nothing but leaf xforms underneath it). If that's the case we just append this prim to the
    // output. If not, then append the local leaves result.
    // Don't consider a prim a leaf if it carries an unloaded payload (it may compose content we can't see), unless the
    // caller has opted out of that protection. This also guards the case where such a prim is supplied directly as a
    // starting search path.
    const bool preserveForPayload = m_preserveUnloadedPayloads && _hasUnloadedPayload(prim);
    if (allLeaves && _isGroupingPrim(prim) && !preserveForPayload)
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


bool PruneLeavesOperation::prototypeAllLeaves(const UsdPrim& instance, const Usd_PrimFlagsPredicate& predicate) const
{
    const UsdPrim proto = instance.GetPrototype();
    if (!proto)
    {
        return false;
    }

    const SdfPath& protoPath = proto.GetPath();
    auto it = m_prototypeLeafCache.find(protoPath);
    if (it != m_prototypeLeafCache.end())
    {
        return it->second;
    }

    // Evaluate the prototype's subtree once. We discard the collected leaves: they live inside a prototype and are
    // only ever surfaced as non-editable instance proxies, so they can never be pruned. We only need to know whether
    // the prototype is composed entirely of leaf grouping prims, which is what decides whether an instancing prim that
    // points at it is itself an empty leaf group. We classify the prototype's children (findChildLeaves) rather than
    // the prototype root, which is a typeless container.
    std::vector<UsdPrim> discard;
    const bool allLeaves = findChildLeaves(proto, predicate, discard);

    m_prototypeLeafCache[protoPath] = allLeaves;
    return allLeaves;
}


bool PruneLeavesOperation::findChildLeaves(const UsdPrim& prim,
                                           const Usd_PrimFlagsPredicate& predicate,
                                           std::vector<UsdPrim>& leaves) const
{

    bool allLeaves = true;
    auto primRange = prim.GetFilteredChildren(predicate);

    for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
    {
        const auto& child = (*iter);

        bool childIsGroupingPrim = _isGroupingPrim(child);

        // A prim with an unloaded payload may bring in meaningful content that isn't currently composed onto the
        // stage. We can't see inside it and (unless explicitly told otherwise) must not prune it, so treat it as a
        // non-leaf and stop descending here.
        if (m_preserveUnloadedPayloads && _hasUnloadedPayload(child))
        {
            allLeaves = false;
            continue;
        }

        // An instance's descendants live in its prototype and are surfaced only as non-editable instance proxies, so
        // we never collect leaves from inside an instance. The instance prim itself is the only prunable candidate,
        // and it is a leaf iff it is a grouping prim whose prototype contains only leaf groups. Because that answer is
        // identical for every instance of a given prototype, prototypeAllLeaves() evaluates each prototype once and
        // caches it - avoiding a redundant per-instance walk of the prototype's instance proxies.
        if (child.IsInstance())
        {
            if (childIsGroupingPrim && prototypeAllLeaves(child, predicate))
            {
                leaves.push_back(child);
            }
            else
            {
                allLeaves = false;
            }

            continue;
        }

        // Need to handle references a little differently. We don't want to return results from within a reference,
        // but if a reference itself contains only leaves then we can remove it.
        if (childIsGroupingPrim && _isReference(child))
        {
            // Recurse in to this child to see if it is a leaf. Note we don't use the output referenceLeaves, we only
            // care about whether the reference itself is a leaf grouping prim.
            //
            // Reuse the same predicate as the rest of the traversal so that inactive-prim filtering and unloaded
            // payloads inside the reference are handled consistently. The predicate already traverses instance
            // proxies (set at the top-level call), which matters as this child might be an instance.
            std::vector<UsdPrim> referenceLeaves;
            bool isLeafReference = findLeaves(child, predicate, referenceLeaves);

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

    return allLeaves;
}

} // namespace usd_optimize
