// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "OrganizePrototypes.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Log.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/sdf/reference.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdUtils/stitch.h>

// Carbonite
#include <carb/profiler/Profile.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::OrganizePrototypesOperation);


namespace omni::scene::optimizer
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((Xform, "Xform"))
);
// LCOV_EXCL_STOP
// clang-format on

/// Constants
constexpr const char* s_categoryOrganizeProtos = "ORGANIZE_PROTOTYPES";

using SdfPathMap = std::map<SdfPath, SdfPath>;
using PrimVector = std::vector<UsdPrim>;

// If the given path already exists on the given stage, return the path with
// a numerical suffix appended to the name that ensures the path is unique. If
// the path does not exist on the stage, it will be returned unchanged.
static SdfPath _getUniquePath(const UsdStageRefPtr& stage, const std::string& path)
{
    std::string unique_path = path;
    int suffix = 1;
    while (stage->GetPrimAtPath(SdfPath(unique_path)).IsValid())
    {
        unique_path = path + std::to_string(suffix++);
    }
    return SdfPath(unique_path);
}

// Return true if the prim at the given path and all its ancestors
// have the SdfSpecifierDef.  Return false otherwise.  We don't
// call prim.IsDefined() because we want to exclude class prims.
static bool _isDefined(const UsdStageRefPtr& stage, const SdfPath& path)
{
    UsdPrim prim = stage->GetPrimAtPath(path);

    if (!prim.IsValid())
    {
        return false;
    }

    while (prim)
    {
        if (prim.GetSpecifier() != SdfSpecifierDef)
        {
            return false;
        }
        prim = prim.GetParent();
    }

    return true;
}

// Create or return the prim at the given namespace which will be the parent of copied
// prototypes.  Return an invalid prim if there was an error.
static UsdPrim _getOrCreateProtosRoot(const UsdStageRefPtr& stage, const SdfPath& protosRootPath)
{
    if (!stage)
    {
        SO_LOG_ERROR("Unable to create prototypes prim due to an invalid Stage.");
        return UsdPrim();
    }

    if (protosRootPath.IsEmpty())
    {
        SO_LOG_ERROR("Unable to create prototypes prim because namespace path is empty.");
        return UsdPrim();
    }

    if (!protosRootPath.IsPrimPath())
    {
        SO_LOG_ERROR("Unable to create prototypes prim because namespace path is not a prim path.");
        return UsdPrim();
    }

    if (!protosRootPath.IsAbsolutePath())
    {
        SO_LOG_ERROR("Unable to create prototypes prim because namespace path is not absolute.");
        return UsdPrim();
    }

    // If the prim at the given path already exists and is not an abstract class, error.
    UsdPrim prim = stage->GetPrimAtPath(protosRootPath);

    if (prim)
    {
        if (prim.GetSpecifier() != SdfSpecifierDef)
        {
            return prim;
        }
        SO_LOG_ERROR("Prim %s cannot be used for the prototypes root because it is a concrete prim.",
                     prim.GetPath().GetAsString().c_str());
        return UsdPrim();
    }

    // Create the root prim
    prim = stage->CreateClassPrim(protosRootPath);
    if (!prim)
    {
        SO_LOG_ERROR("Couldn't create class prim %s.", protosRootPath.GetAsString().c_str());
        return UsdPrim();
    }

    return prim;
}

// Create a placeholder Xform prim where the given prototype will be copied under
// protosRootPath.  Optionally create abstract prims for the prototype's ancestors
// if hierarchyLevels > 0.
static UsdPrim _getProtoDestinationPrim(const UsdStageRefPtr& stage,
                                        const SdfPath& protosRootPath,
                                        const SdfPath& protoPath,
                                        size_t hierarchyLevels)
{
    if (protosRootPath.IsEmpty())
    {
        SO_LOG_WARN("Empty prototype path.");
        return UsdPrim();
    }

    SdfPath copyPath = protosRootPath;
    SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();

    if (hierarchyLevels > 0)
    {
        // Collect the parent elements of the prototype path.
        TfTokenVector parents;
        std::vector<std::string> displayNames;
        size_t curLevel = 0;
        SdfPath parentPath = protoPath.GetParentPath();
        while (!parentPath.IsEmpty() && !parentPath.IsRootPrimPath() && curLevel < hierarchyLevels)
        {
            TfToken token = parentPath.GetNameToken();
            parents.push_back(token);

            displayNames.push_back(stage->GetPrimAtPath(parentPath).GetDisplayName());

            parentPath = parentPath.GetParentPath();
            ++curLevel;
        }

        // Create abstract prim place holders for the parents.
        for (TfTokenVector::reverse_iterator riter = parents.rbegin(); riter != parents.rend(); ++riter)
        {
            copyPath = copyPath.AppendChild(*riter);
            UsdPrim parentPrim = stage->CreateClassPrim(copyPath);
            if (!parentPrim)
            {
                SO_LOG_ERROR("Couldn't create class prim %s.", copyPath.GetAsString().c_str());
                return UsdPrim();
            }

            // set display name, if any
            if (!displayNames.empty())
            {
                std::string& displayName = displayNames.back();
                if (!displayName.empty())
                {
                    auto spec = editLayer->GetPrimAtPath(copyPath);
                    if (spec)
                    {
                        spec->SetField(SdfFieldKeys->DisplayName, displayName);
                    }
                }
                displayNames.pop_back();
            }
        }
    }

    copyPath = copyPath.AppendChild(protoPath.GetNameToken());
    copyPath = _getUniquePath(stage, copyPath.GetAsString());

    // Create the placeholder.
    UsdPrim destPrim = stage->DefinePrim(copyPath, _tokens->Xform);
    if (!destPrim)
    {
        SO_LOG_ERROR("Couldn't create destination prim %s for copying protoype %s.",
                     copyPath.GetAsString().c_str(),
                     protoPath.GetAsString().c_str());
    }

    return destPrim;
}

// Copy the given prim to the destination path on the given layer.
static bool _copyFlattenedPrimToLayer(const UsdStageRefPtr& stage,
                                      SdfLayerHandle& layer,
                                      const UsdPrim& prim,
                                      const SdfPath& dstPath)
{
    if (!prim)
    {
        return false;
    }

    const SdfPath& srcPath = prim.GetPath();

    // Flatten the prim stack layers to a temporary anonymous layer.
    auto flattenedLayer = SdfLayer::CreateAnonymous();

    for (const auto& specHandle : prim.GetPrimStack())
    {
        if (!specHandle.GetSpec().GetPrimAtPath(srcPath))
        {
            continue;
        }

        auto specLayer = specHandle.GetSpec().GetLayer();
        if (!specLayer)
        {
            continue;
        }

        // Sanity check
        if (!specLayer->HasSpec(srcPath))
        {
            continue;
        }

        auto anonLayer = SdfLayer::CreateAnonymous();
        SdfCreatePrimInLayer(anonLayer, prim.GetPath());

        if (!SdfCopySpec(specLayer, srcPath, anonLayer, srcPath))
        {
            SO_LOG_WARN("Couldn't copy prim % to anonymous layer.", srcPath.GetAsString().c_str());
            continue;
        }
        UsdUtilsStitchLayers(flattenedLayer, anonLayer);
    }

    return SdfCopySpec(flattenedLayer, srcPath, layer, dstPath);
}

// Convert the given prototype prim to an instance by deleting its children and making
// it an instanceable reference to the prim at refPath.
static void _convertProtoToInstance(const UsdStageRefPtr& stage, const UsdPrim& protoPrim, const SdfPath& refPath)
{
    if (!protoPrim)
    {
        return;
    }

    if (refPath.IsEmpty())
    {
        return;
    }

    // Delete any children of the prototype.

    UsdPrimSiblingRange children = protoPrim.GetFilteredChildren(Usd_PrimFlagsPredicate());
    PrimVector primsToDelete(children.begin(), children.end());

    _deletePrims(stage, primsToDelete, true /*deactivate if can't delete*/);

    protoPrim.GetReferences().AddInternalReference(refPath);
    protoPrim.SetInstanceable(true);
}

static bool _processSceneGraphInstances(const UsdStageRefPtr& stage,
                                        const std::string& protosNamespace,
                                        size_t hierarchyLevels)
{
    if (!stage)
    {
        SO_LOG_ERROR("No usd stage.");
        return false;
    }

    if (protosNamespace.empty())
    {
        SO_LOG_ERROR("No namespace provided.");
        return false;
    }

    SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();

    // Collect paths to references and prototypes.

    // Paths to the protoypes to be moved.
    SdfPathSet protos;
    // Paths to all references that will need to be updated after the move.
    SdfPathSet references;

    UsdPrimRange range(stage->GetPseudoRoot());
    for (UsdPrim prim : range)
    {
        UsdPrimCompositionQuery query = UsdPrimCompositionQuery::GetDirectReferences(prim);
        for (const auto& arc : query.GetCompositionArcs())
        {
            if (arc.GetTargetLayer() != editLayer)
            {
                // Skip external references.
                continue;
            }

            SdfPath targetPath = arc.GetTargetPrimPath();
            // We only move prototypes that are not already class prims or overs.
            if (_isDefined(stage, targetPath))
            {
                if (prim.IsInstanceable())
                {
                    // Only move a prototype if it's the target of at least one
                    // instanceable reference.
                    protos.insert(targetPath);
                }
                references.insert(prim.GetPath());
            }
        }
    }

    if (protos.empty())
    {
        SO_LOG_INFO("No prototypes to process.");
        return true;
    }

    // Map an original prototype path to the location where it will be copied.
    SdfPathMap protoToCopyMap;

    SdfPath protosRootPath(protosNamespace);

    if (!protosRootPath.IsAbsolutePath())
    {
        protosRootPath = SdfPath::AbsoluteRootPath().AppendPath(protosRootPath);
    }

    UsdPrim protosRoot = _getOrCreateProtosRoot(stage, protosRootPath);
    if (!protosRoot)
    {
        SO_LOG_ERROR("Couldn't get or create prototypes root prim.");
        return false;
    }

    protosRootPath = protosRoot.GetPath();

    // For each original prototype, create a placeholder Xform prim under
    // the root where the prototype will be copied.
    for (const SdfPath& path : protos)
    {
        UsdPrim dstPrim = _getProtoDestinationPrim(stage, protosRootPath, path, hierarchyLevels);

        if (!dstPrim)
        {
            SO_LOG_ERROR("Couldn't create destination prim for prototype %s.", path.GetAsString().c_str());
            continue;
        }

        // Record where original prototype path will be copied.
        protoToCopyMap.insert(std::make_pair(path, dstPrim.GetPath()));
    }

    // Update all references (instanceable or not) to point to new prototype locations.

    for (const SdfPath& path : references)
    {
        UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim)
        {
            SO_LOG_WARN("Couldn't get prim for instance %s.", path.GetText());
            continue;
        }
        SdfReferenceVector refsToRemove;
        SdfReferenceVector refsToAdd;

        UsdPrimCompositionQuery query = UsdPrimCompositionQuery::GetDirectReferences(prim);
        for (const auto& arc : query.GetCompositionArcs())
        {
            if (arc.GetTargetLayer() != editLayer)
            {
                // Skip external references.
                continue;
            }

            SdfPath targetPath = arc.GetTargetPrimPath();

            SdfPathMap::const_iterator iter = protoToCopyMap.find(targetPath);
            if (iter != protoToCopyMap.end())
            {
                refsToRemove.push_back(SdfReference("", targetPath));
                refsToAdd.push_back(SdfReference("", iter->second));
            }
        }

        if (!refsToRemove.empty())
        {
            UsdReferences refs = prim.GetReferences();
            for (const SdfReference& ref : refsToRemove)
            {
                if (!refs.RemoveReference(ref))
                {
                    SO_LOG_WARN("Couldn't remove ref %s from prim %s.", ref.GetPrimPath(), path.GetAsString().c_str());
                }
            }
            for (const SdfReference& ref : refsToAdd)
            {
                if (!refs.AddReference(ref))
                {
                    SO_LOG_WARN("Couldn't add ref %s to prim %s.", ref.GetPrimPath(), path.GetAsString().c_str());
                }
            }
        }
    }

    // Update relationships (e.g., material bindings) that have targets in the prototypes.
    for (auto iter = range.begin(); iter != range.end(); ++iter)
    {
        const UsdPrim& prim = *iter;
        if (protos.find(prim.GetPath()) != protos.end())
        {
            // This is a prototype, so we skip this subtree.
            iter.PruneChildren();
            continue;
        }

        UsdRelationshipVector rels = prim.GetAuthoredRelationships();
        for (UsdRelationship& rel : rels)
        {
            bool targetsUpdated = false;
            SdfPathVector targets;
            rel.GetTargets(&targets);
            for (SdfPath& targetPath : targets)
            {
                // Determinee if this target is in a prototype by checking if its prefix is
                // a prototype path.  Since prototypes might be nested, we compare against
                // the most nested prototypes first by iterating backward over the sorted
                // prototype map.
                for (SdfPathMap::reverse_iterator riter = protoToCopyMap.rbegin(); riter != protoToCopyMap.rend(); ++riter)
                {
                    if (targetPath.HasPrefix(riter->first))
                    {
                        targetPath = targetPath.ReplacePrefix(riter->first, riter->second);
                        targetsUpdated = true;
                        // Not sure if this is needed: make sure a placeholder for the target exists in the
                        // prototype before updating.
                        stage->DefinePrim(targetPath);
                    }
                }
            }
            if (targetsUpdated)
            {
                if (!rel.SetTargets(targets))
                {
                    SO_LOG_WARN("Couldn't update relationships on prim %s.", prim.GetPath());
                }
            }
        }
    }

    // Copy the original prototypes to their new locations and update
    // the original prototype roots to be references to the new locations.

    // Keep track of the number of prototypes moved for logging.
    size_t numProtosCopied = 0;

    // Since prototypes may be nested, we must copy the most nested prototypes
    // first by iterating backwards through the sorted prototype map.
    for (SdfPathMap::reverse_iterator riter = protoToCopyMap.rbegin(); riter != protoToCopyMap.rend(); ++riter)
    {
        UsdPrim protoPrim = stage->GetPrimAtPath(riter->first);
        if (!protoPrim)
        {
            SO_LOG_WARN("Couldn't get prim for prototype %s.", riter->first);
            continue;
        }

        SdfPath dstPath = riter->second;

        if (!_copyFlattenedPrimToLayer(stage, editLayer, protoPrim, dstPath))
        {
            SO_LOG_WARN("Couldn't copy prototype prim %s to %s.",
                        riter->first.GetAsString().c_str(),
                        dstPath.GetAsString().c_str());
            continue;
        }

        // Confirm we succeeded.
        UsdPrim copiedPrim = stage->GetPrimAtPath(dstPath);
        if (!copiedPrim)
        {
            SO_LOG_WARN("Couldn't access copied prim %s to %s.", dstPath.GetAsString().c_str());
            continue;
        }

        ++numProtosCopied;
        _convertProtoToInstance(stage, protoPrim, dstPath);
    }

    SO_LOG_INFO("Copied %zu prototypes to namespace %s.", numProtosCopied, protosRootPath.GetAsString().c_str());

    return true;
}

OrganizePrototypesOperation::OrganizePrototypesOperation()
    : Operation("organizePrototypes",
                "Organize Prototypes",
                "Reparent internal scene-graph instance prototypes under "
                "a user-specified namespace.")
    , m_protosNamespace("Prototypes")
    , m_hierarchyLevels(0)
{
    addArgument("prototypesNamespace",
                "Prototypes Namespace",
                kDisplayTypePrimPath,
                "Namespace where prototypes will be reparented",
                m_protosNamespace)
        .setPlaceholder("Prototypes Namespace");

    addArgument("hierarchyLevels",
                "Preserve Hierarchy Levels",
                kDisplayTypeInt,
                "The number of the prototype's immediate ancestors to retain when reparenting.",
                m_hierarchyLevels);
}


OrganizePrototypesOperation::~OrganizePrototypesOperation() = default;


std::string OrganizePrototypesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion OrganizePrototypesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string OrganizePrototypesOperation::getCategory() const
{
    return s_categoryOrganizeProtos;
}


std::string OrganizePrototypesOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


OperationResult OrganizePrototypesOperation::executeImpl()
{
    if (m_protosNamespace.empty())
    {
        SO_LOG_ERROR("No namespace provided.");
        return { false };
    }

    if (m_hierarchyLevels < 0)
    {
        SO_LOG_ERROR("Hierarchy levels value may not be negative.");
        return { false };
    }

    bool success = _processSceneGraphInstances(getUsdStage(), m_protosNamespace, m_hierarchyLevels);

    return { success };
}

} // namespace omni::scene::optimizer
