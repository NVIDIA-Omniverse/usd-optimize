// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "RemovePrimsPlugin.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/Log.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>

// USD
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>


PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::RemovePrimsOperation);


namespace omni::scene::optimizer
{


// Constants
constexpr const char* s_category = "REMOVE_PRIMS";


typedef std::map<SdfPath, std::set<SdfPath>> ArcGraphMap;
typedef std::shared_ptr<ArcGraphMap> ArcGraphMapPtr;


// builds, adds to, or just returns the composition graph for the given prim and returns a pointer to the node in the
// graph for the prim
//
// note: the knownArcGraphMap parameter is only passed recursively to avoid rebuilding the arc graph map for each prim
//       this is because UsdCompositionQuery returns the entire chain for a given prim. e.g. doing a composition query
//       on prim A could return arcs from A->B and B->C so using this pre-built map avoids redundant queries
static CompositionNode* _buildCompositionGraph(const UsdStageWeakPtr& stage,
                                               const SdfLayerHandle& layer,
                                               const UsdPrim& prim,
                                               CompositionMap& compositionMap,
                                               ArcGraphMapPtr knownArcGraphMap = nullptr)
{
    if (!prim.IsValid())
    {
        return nullptr;
    }

    // prim already in the map? no work to do
    auto findNode = compositionMap.find(prim.GetPath());
    if (findNode != compositionMap.end())
    {
        return &findNode->second;
    }

    // resolve if this prim is concrete
    // note: we have to check the specifier of the PrimSpec from the edit layer since `prim.GetSpecifier()` returns
    //       the composed specifier. Which will return `Def` if any of the composition arcs are concrete. Which
    //       means we can't tell if a particular composition arc of this prim is concrete or not.
    bool concrete = false;
    SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(prim.GetPath());
    if (primSpec)
    {
        concrete = primSpec->GetSpecifier() == SdfSpecifierDef;
    }

    // create the graph node for this prim in the map
    CompositionNode* node =
        &compositionMap.insert(std::make_pair(prim.GetPath(), CompositionNode(prim.GetPath(), concrete))).first->second;

    // either build a new arc graph map or use the known one
    ArcGraphMapPtr arcGraphMap = knownArcGraphMap;
    if (arcGraphMap == nullptr)
    {
        // is there actually anything that would create compositions from this node? If not then we can skip building
        // graph which means we don't need to do an expensive UsdPrimCompositionQuery
        if (!prim.HasAuthoredPayloads() && !prim.HasAuthoredReferences() && !prim.HasAuthoredInherits() &&
            !prim.HasAuthoredSpecializes())
        {
            return node;
        }

        // otherwise get the arc graph information for this prim
        arcGraphMap = std::make_shared<ArcGraphMap>();
        UsdPrimCompositionQuery compositionQuery(prim);
        for (const auto& arc : compositionQuery.GetCompositionArcs())
        {
            // not interested in root arcs since they point to themselves
            if (arc.GetArcType() == PcpArcTypeRoot)
            {
                continue;
            }

            (*arcGraphMap)[arc.GetIntroducingPrimPath()].insert(arc.GetTargetPrimPath());
        }
    }

    // find this node in the arc graph and build or get child nodes for each target
    auto findSourceNode = arcGraphMap->find(prim.GetPath());
    if (findSourceNode != arcGraphMap->end())
    {
        for (const SdfPath& targetPath : findSourceNode->second)
        {
            // avoid empty targets or self-arcs
            if (targetPath.IsEmpty() || targetPath == prim.GetPath())
            {
                continue; // LCOV_EXCL_LINE
            }

            // build the nodes for child prims
            CompositionNode* childNode =
                _buildCompositionGraph(stage, layer, stage->GetPrimAtPath(targetPath), compositionMap, arcGraphMap);
            if (childNode != nullptr)
            {
                node->children.push_back(childNode);
                childNode->parents.push_back(node);
            }
        }
    }
    return node;
}


// Collects all composition arcs to remove going up through parents in the graph
static void _collectArcsToRemoveUp(const CompositionNode* node, std::map<SdfPath, std::set<SdfPath>>& arcsToRemove)
{
    if (node == nullptr || node->resolveConcrete())
    {
        return;
    }

    // remove parent arcs to this and recurse up
    for (const CompositionNode* parent : node->parents)
    {
        if (parent == nullptr)
        {
            continue; // LCOV_EXCL_LINE
        }

        arcsToRemove[parent->path].insert(node->path);
        _collectArcsToRemoveUp(parent, arcsToRemove);
    }
}


// Collects all composition arcs to remove going down through children in the graph
static void _collectArcsToRemoveDown(const CompositionNode* node, std::map<SdfPath, std::set<SdfPath>>& arcsToRemove)
{
    if (node == nullptr || node->resolveConcrete())
    {
        return;
    }

    // remove child arcs and recurse
    for (const CompositionNode* child : node->children)
    {
        if (child == nullptr)
        {
            continue; // LCOV_EXCL_LINE
        }

        arcsToRemove[node->path].insert(child->path);
        _collectArcsToRemoveDown(child, arcsToRemove);
    }
}


/// Collects all composition arcs to remove for the given node and its non-concrete relatives
static void _collectArcsToRemove(const CompositionNode* node, std::map<SdfPath, std::set<SdfPath>>& arcsToRemove)
{
    if (node == nullptr)
    {
        return; // LCOV_EXCL_LINE
    }

    // collect arcs both up and down the graph
    _collectArcsToRemoveUp(node, arcsToRemove);
    _collectArcsToRemoveDown(node, arcsToRemove);
}


/// Returns the strongest remove method between the two given methods
static RemoveMethod _updateRemoveMethod(RemoveMethod a, RemoveMethod b)
{
    if (a == RemoveMethod::eDelete || b == RemoveMethod::eDelete)
    {
        return RemoveMethod::eDelete;
    }
    if (a == RemoveMethod::eDeactivate || b == RemoveMethod::eDeactivate)
    {
        return RemoveMethod::eDeactivate;
    }
    if (a == RemoveMethod::eHide || b == RemoveMethod::eHide)
    {
        return RemoveMethod::eHide;
    }
    if (a == RemoveMethod::eSetAttribute || b == RemoveMethod::eSetAttribute)
    {
        return RemoveMethod::eSetAttribute;
    }
    return RemoveMethod::eIgnore;
}


RemovePrimsOperation::RemovePrimsOperation()
    : Operation("removePrims", "Remove Prims", "Identifies and removes prims from the stage for various reasons.")
{
    addArgument("paths", "Paths", kDisplayTypePrimPaths, "Optional list of prim paths to consider.", m_paths)
        .setPlaceholder("Add paths or entire scene will be processed");

    addArgument("removeInvisible",
                "Remove Invisible",
                kDisplayTypeBool,
                "Whether to remove prims which have computed visibility as invisible.",
                m_removeInvisible);

    Argument& removeInvisibleMethodArg = addArgument("invisibleRemoveMethod",
                                                     "Invisible Remove Method",
                                                     kDisplayTypeEnum,
                                                     "Method that will be used to remove invisible prims.",
                                                     m_invisibleRemoveMethod);
    removeInvisibleMethodArg.setEnumValues<RemoveMethod>({
        { RemoveMethod::eDelete, "Delete" },
        { RemoveMethod::eDeactivate, "Deactivate" },
    });
    addGroup("Invisible Prims", removeInvisibleMethodArg)
        .setVisibleIf("removeInvisible == 1")
        .setEnableIf("removeInvisible == 1");

    addArgument(
        "removeOrphanedOvers",
        "Remove Orphaned Overs",
        kDisplayTypeBool,
        "Whether to remove orphaned overs i.e. overs that do not have any non-concrete arcs, relationships, or connections.",
        m_removeOrphanedOvers);

    Argument& removeOrphanedOversMethodArg = addArgument("orphanedOverRemoveMethod",
                                                         "Orphaned Remove Method",
                                                         kDisplayTypeEnum,
                                                         "Method that will be used to remove orphaned overs.",
                                                         m_removeOrphanedOversMethod);
    removeOrphanedOversMethodArg.setEnumValues<RemoveMethod>({
        { RemoveMethod::eDelete, "Delete" },
        { RemoveMethod::eDeactivate, "Deactivate" },
        { RemoveMethod::eHide, "Hide" },
    });
    addGroup("Orphaned Overs", removeOrphanedOversMethodArg)
        .setVisibleIf("removeOrphanedOvers == 1")
        .setEnableIf("removeOrphanedOvers == 1");

    addArgument("explicitMode",
                "Explicit Mode",
                kDisplayTypeBool,
                "Whether to only remove explicitly specified prims using the explicit path arguments.",
                m_explicitMode)
        .setVisible(false);

    addArgument("explicitInvisiblePaths",
                "Explicit Invisible Paths",
                kDisplayTypePrimPaths,
                "When in explicit mode paths defined by this argument will be considered as invisible for removal.",
                m_explicitInvisiblePaths)
        .setVisible(false);

    addArgument("explicitOrphanedPaths",
                "Explicit Orphaned Paths",
                kDisplayTypePrimPaths,
                "When in explicit mode paths defined by this argument will be considered as orphaned for removal.",
                m_explicitOrphanedPaths)
        .setVisible(false);
}


RemovePrimsOperation::~RemovePrimsOperation() = default;


std::string RemovePrimsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion RemovePrimsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string RemovePrimsOperation::getCategory() const
{
    return s_category;
}


std::string RemovePrimsOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


bool RemovePrimsOperation::getSupportsAnalysis() const
{
    return true;
}


OperationResult RemovePrimsOperation::executeImpl()
{
    // if we're in explicit mode then we only consider the explicitly provided paths
    if (m_explicitMode)
    {
        // ensure lists are cleared
        clear();

        // add the explicit invisible paths
        for (const std::string& pathStr : m_explicitInvisiblePaths)
        {
            m_invisiblePrims.insert(SdfPath(pathStr));
        }
        // add the explicit orphaned paths
        for (const std::string& pathStr : m_explicitOrphanedPaths)
        {
            m_orphanedOvers.insert(SdfPath(pathStr));
        }
    }
    // otherwise traverse the stage to find prims for removal based on the current settings
    else
    {
        findPrimsToRemove();

        // report the number of prims found
        if (m_removeInvisible)
        {
            SO_LOG_INFO("Found %zu invisible prims", m_invisiblePrims.size());
        }
        if (m_removeOrphanedOvers)
        {
            SO_LOG_INFO("Found %zu orphaned overs", m_orphanedOvers.size());
        }
    }

    // get stage and active edit layer
    UsdStageWeakPtr stage = getUsdStage();
    SdfLayerHandle layer = stage->GetEditTarget().GetLayer();

    // removal lists
    std::vector<UsdPrim> primsToDelete;
    std::vector<UsdPrim> primsToDeactivate;
    std::vector<UsdPrim> primsToHide;
    std::vector<UsdPrim> primsToSetAttribute;

    // do an iteration of the stage and check which prims to remove by which method - doing it via a stage iteration
    // ensures we don't process any children of deleted prims or apply any other effect to them
    std::set<RemoveMethod> removeMethods;
    auto primRange = UsdPrimRange(stage->GetPseudoRoot(), UsdPrimAllPrimsPredicate);
    for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
    {
        const UsdPrim& prim = *iter;
        removeMethods.clear();

        // invisible?
        if (m_invisiblePrims.find(prim.GetPath()) != m_invisiblePrims.end())
        {
            removeMethods.insert(m_invisibleRemoveMethod);
        }
        // orphaned over?
        if (m_orphanedOvers.find(prim.GetPath()) != m_orphanedOvers.end())
        {
            removeMethods.insert(m_removeOrphanedOversMethod);
        }

        // if delete is in the remove methods then this is all we should perform and don't consider children since they
        // are also being deleted
        if (removeMethods.find(RemoveMethod::eDelete) != removeMethods.end())
        {
            primsToDelete.push_back(prim);
            iter.PruneChildren();
            continue;
        }
        // otherwise check for other methods
        for (RemoveMethod removeMethod : removeMethods)
        {
            switch (removeMethod)
            {
            case RemoveMethod::eDeactivate:
                primsToDeactivate.push_back(prim);
                break;
            case RemoveMethod::eHide:
                primsToHide.push_back(prim);
                break;
            case RemoveMethod::eSetAttribute:
                primsToSetAttribute.push_back(prim);
                break;
            default:
                break;
            }
        }
    }

    // set attribute on prims
    if (!primsToSetAttribute.empty())
    {
        SO_LOG_INFO("Setting attribute on %zu prims", primsToSetAttribute.size());
        SdfChangeBlock changeBlock;
        _setAttributeOnPrims(primsToSetAttribute, true);
    }

    // hide prims
    if (!primsToHide.empty())
    {
        SO_LOG_INFO("Hiding %zu prims", primsToHide.size());
        SdfChangeBlock changeBlock;
        _hidePrims(primsToHide);
    }

    // deactivate prims
    if (!primsToDeactivate.empty())
    {
        SO_LOG_INFO("Deactivating %zu prims", primsToDeactivate.size());

        // reverse the list to ensure children are deactivated before parents
        std::reverse(primsToDeactivate.begin(), primsToDeactivate.end());

        SdfChangeBlock changeBlock;
        _deactivatePrims(primsToDeactivate);
    }

    // delete prims
    if (!primsToDelete.empty())
    {
        SO_LOG_INFO("Deleting %zu prims", primsToDelete.size());

        // Directly deleting prims causes USD warnings from dangling reference arcs,
        // even inside a change block.  Removing the arcs first is more efficient than
        // deleting in reference-chain order, which would repeatedly update parent children lists.
        std::map<SdfPath, std::set<SdfPath>> arcsToRemove;
        for (const UsdPrim& prim : primsToDelete)
        {
            // don't need to collect relationships if its already been done
            if (arcsToRemove.find(prim.GetPath()) != arcsToRemove.end())
            {
                continue;
            }

            auto findCompositionNode = m_compositionMap.find(prim.GetPath());
            // sanity check
            if (findCompositionNode == m_compositionMap.end())
            {
                continue; // LCOV_EXCL_LINE
            }

            // collect up the compositions from this graph so they can be removed
            _collectArcsToRemove(&findCompositionNode->second, arcsToRemove);
        }

        // now remove the arcs
        {
            SdfChangeBlock changeBlock;

            for (const auto& it : arcsToRemove)
            {
                SdfPrimSpecHandle primSpec = layer->GetPrimAtPath(it.first);
                if (!primSpec)
                {
                    continue; // LCOV_EXCL_LINE
                }

                // note: don't need to remove inherits since they point to classes which are always concrete
                // remove payloads
                if (primSpec->HasPayloads())
                {
                    SdfPayloadsProxy payloads = primSpec->GetPayloadList();
                    for (SdfPayload& payload : payloads.GetAppliedItems())
                    {
                        if (it.second.find(payload.GetPrimPath()) != it.second.end())
                        {
                            payloads.RemoveItemEdits(payload);
                        }
                    }
                }
                // remove specializes
                if (primSpec->HasSpecializes())
                {
                    SdfSpecializesProxy specializes = primSpec->GetSpecializesList();
                    for (SdfPath& path : specializes.GetAppliedItems())
                    {
                        if (it.second.find(path) != it.second.end())
                        {
                            specializes.RemoveItemEdits(path);
                        }
                    }
                }
                // remove references
                if (primSpec->HasReferences())
                {
                    SdfReferencesProxy references = primSpec->GetReferenceList();
                    for (SdfReference& ref : references.GetAppliedItems())
                    {
                        if (it.second.find(ref.GetPrimPath()) != it.second.end())
                        {
                            references.RemoveItemEdits(ref);
                        }
                    }
                }
            }
        }

        // now we can finally delete the prims without warnings - phew
        {
            SdfChangeBlock changeBlock;
            constexpr bool deactivateFallback = true;
            constexpr bool deleteOvers = true;
            _deletePrims(getUsdStage(), primsToDelete, deactivateFallback, deleteOvers);
        }
    }

    clear();

    return { true };
}


OperationResult RemovePrimsOperation::executeAnalysisImpl()
{
    findPrimsToRemove();

    // write the result as json
    JsArray invisiblePathsArray;
    if (m_removeInvisible)
    {
        invisiblePathsArray = _toJson(m_invisiblePrims).GetJsArray();
    }

    JsArray orphanedPathsArray;
    if (m_removeOrphanedOvers)
    {
        orphanedPathsArray = _toJson(m_orphanedOvers).GetJsArray();
    }

    JsObject analysisResult;
    if (!invisiblePathsArray.empty())
    {
        analysisResult["invisiblePrims"] = invisiblePathsArray;
    }
    if (!orphanedPathsArray.empty())
    {
        analysisResult["orphanedOvers"] = orphanedPathsArray;
    }

    // is there anything to write to as a suggested fix?
    if (!invisiblePathsArray.empty() || !orphanedPathsArray.empty())
    {
        JsObject opArgs;
        opArgs["invisibleRemoveMethod"] = _toJson(static_cast<int>(m_invisibleRemoveMethod));
        opArgs["removeOrphanedOversMethod"] = _toJson(static_cast<int>(m_removeOrphanedOversMethod));
        opArgs["explicitMode"] = _toJson(true);
        if (!invisiblePathsArray.empty())
        {
            opArgs["explicitInvisiblePaths"] = invisiblePathsArray;
        }
        if (!orphanedPathsArray.empty())
        {
            opArgs["explicitOrphanedPaths"] = orphanedPathsArray;
        }

        JsObject opConfig;
        opConfig["name"] = _toJson("removePrims");
        opConfig["args"] = opArgs;

        JsArray suggestedOperations;
        suggestedOperations.push_back(opConfig);
        analysisResult["suggestedOperations"] = suggestedOperations;
    }

    // write under the analysis key
    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true, nullptr, getCStr(JsWriteToString(resultJson)) };
    return result;
}


void RemovePrimsOperation::findPrimsToRemove()
{
    // ensure lists are cleared
    clear();

    // get stage and active edit layer
    UsdStageWeakPtr stage = getUsdStage();
    SdfLayerHandle layer = stage->GetEditTarget().GetLayer();

    // if we're removing orphaned overs we need to do a first pass of the entire stage to discover existing arcs,
    // relationships, and, connections
    if (m_removeOrphanedOvers)
    {
        // build the composition graph for all prims in the stage
        auto primRange = UsdPrimRange(stage->GetPseudoRoot(), UsdPrimAllPrimsPredicate);
        for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
        {
            _buildCompositionGraph(stage, layer, (*iter), m_compositionMap);
        }

        // we now have to do a second pass to find all the relationships/connections in the scene that come from
        // prims
        std::set<SdfPath> connectedPaths;
        for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
        {
            const UsdPrim& prim = (*iter);

            // should relationships/connections on this prim be considered?
            if (!prim.IsDefined())
            {
                // not defined - look it up in the composition map to see if it resolves concrete
                auto findCompositionNode = m_compositionMap.find(prim.GetPath());
                if (findCompositionNode == m_compositionMap.end() || !findCompositionNode->second.resolveConcrete())
                {
                    continue;
                }
            }

            // process relationships
            for (const UsdRelationship& rel : prim.GetRelationships())
            {
                SdfPathVector targets;
                rel.GetTargets(&targets);
                for (const SdfPath& targetPath : targets)
                {
                    connectedPaths.insert(targetPath.GetPrimPath());
                }
            }

            // process connections on attributes
            for (const UsdAttribute& attr : prim.GetAttributes())
            {
                SdfPathVector connectedPathsVec;
                attr.GetConnections(&connectedPathsVec);
                for (const SdfPath& targetPath : connectedPathsVec)
                {
                    connectedPaths.insert(targetPath.GetPrimPath());
                }
            }
        }

        // now mark all composition nodes that are connected to from relationships/connections - this will also
        // update their cached resolved state and that of their children since being connected means they are not
        // orphaned
        for (const SdfPath& connectedPath : connectedPaths)
        {
            auto findCompositionNode = m_compositionMap.find(connectedPath);
            if (findCompositionNode != m_compositionMap.end())
            {
                findCompositionNode->second.connect();
            }
        }
    }

    // callback function for _resolveExpressionsToPrims that finds prims to remove
    ResolveFilter resolveFilter = [&](const UsdPrim& prim, UsdPrimRange::iterator& iter)
    {
        // track the remove method for this prim
        RemoveMethod removeMethod = RemoveMethod::eIgnore;

        // find invisible prims?
        if (m_removeInvisible && prim.IsActive())
        {
            // get the visibility attribute if this is an imageable prim - note: we don't use ComputeVisibility()
            // because we don't want to factor in inherited visibility - only remove prims that are explicitly invisible
            // which will in itself manage the state of the prim's children
            const UsdGeomImageable imageable(prim);
            if (imageable)
            {
                UsdAttribute visibilityAttr = imageable.GetVisibilityAttr();
                if (visibilityAttr)
                {
                    TfToken visibility;
                    visibilityAttr.Get(&visibility);
                    if (visibility == UsdGeomTokens->invisible)
                    {
                        removeMethod = _updateRemoveMethod(removeMethod, m_invisibleRemoveMethod);
                        m_invisiblePrims.insert(prim.GetPath());
                    }
                }
            }
        }

        // find orphaned overs?
        if (m_removeOrphanedOvers && !prim.IsDefined())
        {
            auto findCompositionNode = m_compositionMap.find(prim.GetPath());
            // not in the composition map or doesn't resolve to concrete? - must be orphaned
            if (findCompositionNode == m_compositionMap.end() || !findCompositionNode->second.resolveConcrete())
            {
                removeMethod = _updateRemoveMethod(removeMethod, m_removeOrphanedOversMethod);
                m_orphanedOvers.insert(prim.GetPath());
            }
        }

        // if the remove method is delete we can prune children since they will be removed too
        if (removeMethod == RemoveMethod::eDelete)
        {
            iter.PruneChildren();
        }

        return removeMethod != RemoveMethod::eIgnore;
    };

    // use _resolveExpressionsToPrims to iterate the scene and find prims to remove
    constexpr bool meshesOnly = false;
    constexpr bool reverse = false;
    _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(),
                               m_paths,
                               meshesOnly,
                               reverse,
                               UsdPrimAllPrimsPredicate,
                               resolveFilter);
}


void RemovePrimsOperation::clear()
{
    m_compositionMap.clear();
    m_invisiblePrims.clear();
    m_orphanedOvers.clear();
}

} // namespace omni::scene::optimizer
