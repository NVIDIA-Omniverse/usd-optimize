// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/SpatialClustering.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"
#include "omni/scene.optimizer/core/ResolveSdfPaths.h"

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/kind/registry.h>
#include <pxr/usd/usd/modelAPI.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>
#include <pxr/usd/usdSkel/root.h>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((clustered, "clustered"))
    ((references, "references"))
);
// LCOV_EXCL_STOP
// clang-format on


// Given a string that describes the naming of output prims split it into name and parent path.
// If the parent path is set we guarantee that it will hold a relative path that can be appended to another path.
static void _getParentAndName(const std::string& value, SdfPath& parentPath, TfToken& name)
{
    // Do nothing if we do not have a valid input string.
    if (!SdfPath::IsValidPathString(value))
    {
        return;
    }

    // Do nothing if we do not have a valid prim path.
    SdfPath path = SdfPath(value);
    if (!path.IsPrimPath())
    {
        return;
    }

    // Get the name and parent path.
    name = path.GetNameToken();
    parentPath = path.GetParentPath();

    // Ensure that the parent path is relative.
    if (parentPath.IsAbsolutePath())
    {
        parentPath = parentPath.MakeRelativePath(SdfPath::AbsoluteRootPath());
    }
}


/// Check if this prim must be part of the hierarchy for child prims to maintain their behavior through merge.
///
/// As well as functional merge boundaries we also consider the elective logic described by the merge boundary option.
/// These are prims that are significant to the scene description in terms of means rather than function.
///
/// \param prim The prim to consider
/// \param instancedPrims Any prims in the stage that are instanced
/// \param mergePoint Point in the hierarchy where merged meshes should be parented
/// \return Whether the prim is a merge boundary.
static bool _isMergeBoundary(const UsdPrim& prim, const std::set<UsdPrim>& instancedPrims, MergePointOption mergePoint)
{
    // The order of checks in this function should not change behavior.
    // Therefore we can order them so cheap and common matches are performed first.

    // The pseudo root is always a merge boundary.
    if (prim.GetPath().IsAbsoluteRootPath())
    {
        return true;
    }

    // Any prim is considered a merge boundary if the merge point method is "parent" or "Original Prim".
    if (mergePoint == MergePointOption::eParentPrim || mergePoint == MergePointOption::eOriginalPrim)
    {
        return true;
    }

    // Any prim that does not have a define specifier is a merge boundary because the output mesh will use a define
    // specifier but should still inherit this one to maintain the effective abstract state.
    if (prim.GetSpecifier() != SdfSpecifierDef)
    {
        return true;
    }

    // A prim who's path is used in the composition of an instanceable prim is a merge boundary.
    // Merging prims below it into another hierarchy would remove the prim from the scope of the instance prototype.
    if (instancedPrims.count(prim))
    {
        return true;
    }

    // Any xform is considered a merge boundary if the merge point method is "xform".
    if (mergePoint == MergePointOption::eXform && prim.GetTypeName() == UsdGeomTokens->Xform)
    {
        return true;
    }

    // Any prim whose parent is the absolute root is considered a merge boundary if the merge point method is "Root
    // Prim".
    if (mergePoint == MergePointOption::eRootPrim)
    {
        if (prim.GetPath().IsRootPrimPath())
        {
            return true;
        }
    }

    // Any prim with a kind covered by the given merge point method is considered a merge boundary.
    TfToken kind;
    if (UsdModelAPI(prim).GetKind(&kind))
    {
        // Populate a vector of the kinds that should be accepted in each mode so that we can match against them.
        TfTokenVector matchingKinds;
        switch (mergePoint)
        {
        case MergePointOption::eKindAssembly:
            matchingKinds = { KindTokens->assembly };
            break;
        case MergePointOption::eKindGroup:
            matchingKinds = { KindTokens->group };
            break;
        case MergePointOption::eKindComponent:
            // Components logically live below groups so match as a fallback.
            matchingKinds = { KindTokens->component, KindTokens->group };
            break;
        case MergePointOption::eKindModel:
            matchingKinds = { KindTokens->model };
            break;
        case MergePointOption::eKindSubcomponent:
            // Subcomponents logically live below components and groups so match as a fallback.
            matchingKinds = { KindTokens->subcomponent, KindTokens->component, KindTokens->group };
            break;
        case MergePointOption::eDefault:
        case MergePointOption::eXform:
        case MergePointOption::eRootPrim:
        case MergePointOption::eParentPrim:
        default:
            break;
        }

        // Check if the kind matches of inherits from any of the matching kinds.
        for (const auto& baseKind : matchingKinds)
        {
            if (KindRegistry::IsA(kind, baseKind))
            {
                return true;
            }
        }
    }

    // A prim that resets the xform stack is a merge boundary.
    // Merging prims below it into another hierarchy would inherit transformation not currently inherited.
    UsdGeomXformable xformable(prim);

    // NOTE: We only really need to treat this as a merge boundary if there is a parent with a time sampled xform, as
    // the merge process will match the points in worldspace. Post merge however the mesh faces would behave differently
    // when new transformations are made to the parent. This edge case workflow and the additional cost of tracking
    // time sampled parents makes the simple (if over zealous) solution more appealing.
    if (xformable && xformable.GetResetXformStack())
    {
        return true;
    }

    // A prim with a time sampled transform is a merge boundary.
    // Merging prims below it into another hierarchy would discard the inherited transformation.
    if (xformable && xformable.TransformMightBeTimeVarying())
    {
        return true;
    }

    // A prim with a time sampled visibility is a merge boundary.
    UsdGeomImageable imageable(prim);
    if (imageable && imageable.GetVisibilityAttr().ValueMightBeTimeVarying())
    {
        return true;
    }

    return false;
}


/// Check if this prim has child prims that cannot be handled during merge.
///
/// \param prim The prim to consider
/// \return Whether the prim has unmergeable children
static bool _hasUnmergeableChildren(const UsdPrim& prim)
{
    // Iterate all active children.
    for (const auto& child : prim.GetChildren())
    {
        // We handle UsdGeom Subsets so we don't need to worry about them.
        if (child.IsA<UsdGeomSubset>())
        {
            continue;
        }

        // At this point we have an unhandled child.
        return true;
    }

    return false;
}


/// Find meshes that can be merged.
///
/// This function replaces _resolveSdfPaths for meshes. It traverses from a specified \p rootPrim
/// looking for any meshes and recording them against the \p rootPath that they could be merged
/// under.
///
/// It is intended to be a little smarter about what can be merged and where. For example, we can't
/// merge meshes that are inside an instance. Instead, what we want to do is find the thing that is
/// being instanced and see what can be merged inside it, allowing us to preserve the referencing
/// and not breaking the stage.
///
/// Meshes are organized in groups based on the path they could be merged at (eg, from a reference)
/// along with the prims that can be merged there.
///
/// Generally speaking invisible prims are skipped and the traversal pruned. If an explicit path to
/// a prim is specified, however, we can enable traversing it. This is to aid in supporting geometry
/// or prototype libraries that contain meshes to be referenced elsewhere and may be hidden.
//
/// \param usdStage The USD stage
/// \param startPrim The prim to start traversing from
/// \param rootPrim The prim that should be the common ancestor of input prims and their merged output prim
/// \param instancedPrims Any prims in the stage that are instanced
/// \param lookup MergeBoundaryLookup object to accumulate meshes and their parent path mappings in
/// \param resolvedPrimSet Set to check for uniqueness when traversing explicit paths (optional)
/// \param checkParentsForCamera Whether to check paths for cameras in their ancestors
/// \param mergePoint Point in the hierarchy where merged meshes should be parented
/// \param skipInvisible Whether to skip invisible prims
static void _findMeshes(const UsdStageWeakPtr& usdStage,
                        const UsdPrim& startPrim,
                        const UsdPrim& rootPrim,
                        const std::set<UsdPrim>& instancedPrims,
                        MergeBoundaryLookup& lookup,
                        std::set<UsdPrim>* resolvedPrimsSet,
                        bool checkParentsForCamera,
                        MergePointOption mergePoint,
                        bool skipInvisible)
{
    // If the root prim is a mesh then we know there's only one mesh and we can't find anything
    // underneath it. This deals with things like a PointInstancer that contain a prototype which
    // is a mesh - in that case we don't want to do anything.
    if (rootPrim.GetTypeName() == UsdGeomTokens->Mesh)
    {
        // if we're using original prim merge point we need to ensure any root mesh is explicitly included in the lookup
        // since meshes map to themselves as their own merge boundary
        if (mergePoint == MergePointOption::eOriginalPrim)
        {
            resolvedPrimsSet->insert(rootPrim);
            lookup.primToParent[rootPrim.GetPath()] = rootPrim.GetPath();
        }
        return;
    }

    // Output vector. We will set this if we find anything valid to do the one lookup. The output vector
    // is cached per rootPrim path, so one lookup is good for any prims we traverse here.
    std::vector<UsdPrim>* resolvedPrims = nullptr;

    // Get the prim and iterate over its range.
    // We define a custom predicate here. It's the default predicate minus the check for non-defined and abstract
    // prims. This means we can traverse into Overs and Classes, which is useful for stages that use a "geometry
    // library" that is Over'd in to the stage, with other prims referencing back in to it.
    static const Usd_PrimFlagsPredicate predicate = UsdPrimIsActive && UsdPrimIsLoaded;
    auto primRange = UsdPrimRange(startPrim, predicate);
    for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
    {
        const auto& prim = (*iter);

        // The pseudo root cannot be edited so skip straight past it during traversal
        if (prim.GetPath().IsAbsoluteRootPath())
        {
            continue;
        }

        // Need to explicitly check the specifier here because for a prim that is an Over or Class, we want to prune it
        // from the traversal. However we cannot use the IsAbstract or IsDefined check as that checks all parents and
        // if we may have entered this traversal via a reference to something below an Over or Clas prim.
        if (prim.GetSpecifier() != SdfSpecifierDef)
        {
            iter.PruneChildren();
            continue;
        }

        // Perform Mesh related checks.
        if (prim.IsA<UsdGeomMesh>())
        {
            // Prune children as we never want to try and merge anything that lives below a Mesh.
            iter.PruneChildren();

            // If the mesh has children that are not understood by Merge it should be skipped. Otherwise the state of
            // the scene may change when the mesh is removed.
            if (_hasUnmergeableChildren(prim))
            {
                continue;
            }

            // If this mesh has a time sampled transform or resets the xform stack then it cannot be merged.

            // NOTE: We could in fact merge meshes that reset the xform stack with other meshes that reset the xform
            // stack. However we'd need to make that a component of the bucketing hash, and change the output parent of
            // any buckets that reset the xform stack. This is not something we do in any other cases and feels over
            // engineered for an uncommon case such like reset xform stack.
            UsdGeomXformable xformable(prim);
            if (xformable)
            {
                if (xformable.TransformMightBeTimeVarying() || xformable.GetResetXformStack())
                {
                    continue;
                }
            }

            // If this mesh has a time sampled visibility it cannot be merged.
            UsdGeomImageable imageable(prim);
            if (imageable && imageable.GetVisibilityAttr().ValueMightBeTimeVarying())
            {
                continue;
            }

            // Resolve the prim.
            if (_resolvePrim(usdStage, prim, resolvedPrimsSet, checkParentsForCamera))
            {
                SdfPath boundaryPath = rootPrim.GetPath();
                // special case for the "Original Prim" merge boundary mode where every mesh is its own merge boundary
                if (mergePoint == MergePointOption::eOriginalPrim)
                {
                    boundaryPath = prim.GetPath();
                }

                // Found a valid mesh to include.
                // Find or create the output vector in the map now that we have something to add.
                if (resolvedPrims == nullptr)
                {
                    resolvedPrims = &lookup.parentToPrims[boundaryPath];
                }

                resolvedPrims->push_back(prim);
                lookup.primToParent[prim.GetPath()] = boundaryPath;
            }

            // Completed processing the Mesh.
            continue;
        }

        // Perform hierarchy related checks pruning the traversal or starting new traversals as appropriate.

        // Certain prim types we can prune to avoid even recursing into them.
        if (prim.IsA<UsdGeomCamera>() || prim.IsA<UsdShadeMaterial>())
        {
            iter.PruneChildren();
            continue;
        }

        // Do not traverse past UsdSkelRoot prims as the mesh prims below there are skinned.
        // It is possible for some meshes below a UsdSkelRoot to not be skinned but determining that is costly and not
        // merging those meshes is likely acceptable.
        if (prim.IsA<UsdSkelRoot>())
        {
            iter.PruneChildren();
            continue;
        }

        // Generally we want to skip invisible prims and prune the iteration.
        // In some cases (an invisible prim being explicitly specified) we do
        // want to merge, though.
        if (skipInvisible)
        {
            // Do not traverse past invisible prims.
            const auto& imageable = UsdGeomImageable(prim);
            if (imageable)
            {
                TfToken visibility;
                if (imageable.GetVisibilityAttr().Get(&visibility))
                {
                    if (visibility == UsdGeomTokens->invisible)
                    {
                        iter.PruneChildren();
                        continue;
                    }
                }
            }
        }

        // If this prim is an instance then we can't author directly to it. We want to traverse whatever it is
        // instancing and see if we can merge there.
        if (prim.IsInstance())
        {
            SdfReferenceListOp references;
            if (prim.GetMetadata(_tokens->references, &references))
            {
                const auto& added = references.GetAddedItems();

                // Find added reference. Currently only deals with first.
                if (!added.empty())
                {
                    const SdfReference& ref = added.front();
                    const UsdPrim& _prim = usdStage->GetPrimAtPath(ref.GetPrimPath());

                    // Verify the prim exists
                    if (!_prim)
                    {
                        continue;
                    }

                    _findMeshes(usdStage,
                                _prim, // startPrim
                                _prim, // rootPrim
                                instancedPrims,
                                lookup,
                                resolvedPrimsSet,
                                checkParentsForCamera,
                                mergePoint,
                                skipInvisible);
                }

                // Something instanceable that references something else - prune here.
                iter.PruneChildren();
                continue;
            }
        }

        // If this prim is a point instancer we should not traverse past it, but instead start a new traversal for each
        // of it's prototypes with the output parent reset to the prototype path. This way the point instancer will
        // continue to function but the meshes within each prototype will have been merged together.
        const UsdGeomPointInstancer pointInstancer(prim);
        if (pointInstancer)
        {
            // Get the prototypes of this point instancer.
            SdfPathVector prototypePaths;
            pointInstancer.GetPrototypesRel().GetTargets(&prototypePaths);

            // Start new traversals below each prototype with that as the output parent.
            for (const auto& prototypePath : prototypePaths)
            {
                const UsdPrim& prototypePrim = usdStage->GetPrimAtPath(prototypePath);

                // Verify the prototype prim exists
                if (!prototypePrim)
                {
                    continue;
                }

                _findMeshes(usdStage,
                            prototypePrim, // startPrim
                            prototypePrim, // rootPrim
                            instancedPrims,
                            lookup,
                            resolvedPrimsSet,
                            checkParentsForCamera,
                            mergePoint,
                            skipInvisible);
            }

            // Point Instancer that has prototypes - prune here.
            iter.PruneChildren();
            continue;
        }

        // There are some additional checks we only need to do for prims that we are encountering as part of traversal
        // rather than being explicitly passed in as the traversal start point.
        if (prim != rootPrim)
        {
            if (_isMergeBoundary(prim, instancedPrims, mergePoint))
            {
                _findMeshes(usdStage,
                            prim, // startPrim
                            prim, // rootPrim
                            instancedPrims,
                            lookup,
                            resolvedPrimsSet,
                            checkParentsForCamera,
                            mergePoint,
                            skipInvisible);

                // Any descendant meshes will be caught by a new traversal - prune here.
                iter.PruneChildren();
                continue;
            }
        }
    }
}


// Walk back up the path ancestors looking for the first prim that exists in the stage.
// Return its local to world transform on the assumption that this would be inherited by the prim with the given
// path if it existed on the stage.
static GfMatrix4d _getFirstLocalToWorldTransform(const UsdStageWeakPtr& usdStage,
                                                 const SdfPath& path,
                                                 UsdGeomXformCache& xformCache)
{
    for (const auto& _path : path.GetAncestorsRange())
    {
        const UsdPrim& prim = usdStage->GetPrimAtPath(_path);
        if (prim)
        {
            return xformCache.GetLocalToWorldTransform(prim);
        }
    }

    // Return an identity matrix if no ancestor prim was found.
    return GfMatrix4d(1.0);
}


static bool _computeSupersetCentroid(const VirtualMesh& mesh, GfVec3f& centroid)
{
    // Calculate the centroid from the worldspace extents of each input mesh then using the mid point.
    GfRange3f range;
    for (const VirtualMesh& child : mesh.getSupersetChildren())
    {
        const VtVec3fArray& worldExtent = child.getWorldExtent();
        if (worldExtent.size() >= 2)
        {
            range.ExtendBy(child.getWorldExtent()[0]);
            range.ExtendBy(child.getWorldExtent()[1]);
        }
    }
    if (range.IsEmpty())
    {
        return false;
    }

    centroid = range.GetMidpoint();
    return true;
}


SpatialClustering::SpatialClustering()
    : m_defaultPrimName(_tokens->clustered)
{
}


Argument& SpatialClustering::addConsiderMaterialsArg(Operation* operation)
{
    Argument& arg = operation->addArgument(
        "considerMaterials",
        "Keep Materials Separate",
        kDisplayTypeBool,
        "Whether separate mesh prims will be created for each material that can be merged. When off, meshes with "
        "differing materials can be merged under a single mesh prim using GeomSubsets for each material.",
        m_considerMaterials);
    return arg;
}


Argument& SpatialClustering::addMaterialAlbedoAsVertexColorsArg(Operation* operation)
{
    Argument& arg = operation->addArgument("materialAlbedoAsVertexColors",
                                           "Compute Display Colors",
                                           kDisplayTypeBool,
                                           "Set display color and opacity to values computed from the bound material",
                                           m_materialAlbedoAsVertexColors);
    return arg;
}


Argument& SpatialClustering::addOriginalGeomOptionArg(Operation* operation)
{
    Argument& arg = operation->addArgument("originalGeomOption",
                                           "Original Mesh Handling",
                                           kDisplayTypeEnum,
                                           "What to do with any meshes in the original scene that were split or merged",
                                           m_originalGeomOption);
    arg.setEnumValues<RemoveMethod>({
        { RemoveMethod::eIgnore, "Ignore" },
        { RemoveMethod::eDelete, "Delete" },
        { RemoveMethod::eDeactivate, "Deactivate" },
        { RemoveMethod::eHide, "Hide" },
    });
    return arg;
}


Argument& SpatialClustering::addMergePointArg(Operation* operation)
{
    Argument& arg =
        operation->addArgument("mergePoint",
                               "Merge Boundary",
                               kDisplayTypeEnum,
                               "The boundary of where to merge meshes, for example, only merge meshes within a model",
                               m_mergePoint);
    arg.setEnumValues<MergePointOption>({ { MergePointOption::eDefault, "Stage" },
                                          { MergePointOption::eRootPrim, "Root Prim" },
                                          { MergePointOption::eParentPrim, "Parent Prim" },
                                          { MergePointOption::eXform, "Parent Xform" },
                                          { MergePointOption::eOriginalPrim, "Original Prim" },
                                          { MergePointOption::eKindAssembly, "Kind: Assembly" },
                                          { MergePointOption::eKindGroup, "Kind: Group" },
                                          { MergePointOption::eKindComponent, "Kind: Component" },
                                          { MergePointOption::eKindModel, "Kind: Model" },
                                          { MergePointOption::eKindSubcomponent, "Kind: Subcomponent" } });
    return arg;
}


Argument& SpatialClustering::addRootPathArg(Operation* operation)
{
    return operation->addArgument("rootPath",
                                  "Output Name",
                                  kDisplayTypePrimPath,
                                  "The output name to use for newly created merged meshes",
                                  m_rootPath);
}


Argument& SpatialClustering::addConsiderAllAttributesArg(Operation* operation)
{
    Argument& arg =
        operation->addArgument("considerAllAttributes",
                               "Strict Attribute Mode",
                               kDisplayTypeBool,
                               "When enabled all additional attributes on prims must match for them to be merged",
                               m_considerAllAttributes);
    return arg;
}


Argument& SpatialClustering::addAllowSingleMeshesArg(Operation* operation)
{
    Argument& arg = operation->addArgument("allowSingleMeshes",
                                           "Allow Single Meshes",
                                           kDisplayTypeBool,
                                           "When enabled means a single mesh will still be run through the merge process",
                                           m_allowSingleMeshes);
    return arg;
}


Argument& SpatialClustering::addSpatialModeArg(Operation* operation)
{
    Argument& arg = operation->addArgument("spatialMode",
                                           "Spatial Clustering Mode",
                                           kDisplayTypeEnum,
                                           "Enable spatial clustering of meshes by choosing a clustering method",
                                           m_spatialMode);
    arg.setEnumValues<ClusterMode>({ { ClusterMode::eNone, "None" },
                                     { ClusterMode::eBoundingBox, "Bounding Box" },
                                     { ClusterMode::eVertexCount, "Vertex Count" } });
    return arg;
}


Argument& SpatialClustering::addSpatialThresholdArg(Operation* operation, const std::string& enableIf)
{
    Argument& arg = operation->addArgument("spatialThreshold",
                                           "Spatial Threshold",
                                           kDisplayTypeFloat,
                                           "Maximum distance at which to consider meshes neighbors",
                                           m_spatialThreshold);
    arg.setVisibleIf("spatialMode == 1").setEnableIf(enableIf);
    return arg;
}

Argument& SpatialClustering::addSpatialMaxSizeArg(Operation* operation, const std::string& enableIf)
{
    Argument& arg = operation->addArgument("spatialMaxSize",
                                           "Spatial Max Size",
                                           kDisplayTypeFloat,
                                           "Maximum size that clustered meshes can be grouped in",
                                           m_spatialMaxSize);
    arg.setVisibleIf("spatialMode == 1").setEnableIf(enableIf);
    return arg;
}

Argument& SpatialClustering::addSpatialVertexCountArg(Operation* operation, const std::string& enableIf)
{
    Argument& arg = operation->addArgument("spatialVertexCount",
                                           "Spatial Vertex Count",
                                           kDisplayTypeInt,
                                           "Maximum number of vertices that to cluster together",
                                           m_spatialVertexCount);
    arg.setVisibleIf("spatialMode == 2").setEnableIf(enableIf);
    return arg;
}


Argument& SpatialClustering::addTreatAsPrimvarsArg(Operation* operation)
{
    Argument& arg = operation->addArgument("treatAsPrimvars",
                                           "Treat As Primvars",
                                           kDisplayTypeAttributeList,
                                           "List of attribute names that can be merged as if they were constant primvars",
                                           m_treatAsPrimvars);
    arg.setVisible(false);
    return arg;
}


Argument& SpatialClustering::addSpatialDebugArg(Operation* operation)
{
    Argument& arg = operation->addArgument("spatialDebug",
                                           "Spatial Debug Mode",
                                           kDisplayTypeBool,
                                           "Enable debug color mode",
                                           m_spatialDebug);
    arg.setVisible(false);
    return arg;
}


void SpatialClustering::setDefaultPrimName(const TfToken& name)
{
    if (!name.IsEmpty())
    {
        m_defaultPrimName = name;
    }
    else
    {
        m_defaultPrimName = _tokens->clustered;
    }
}


void SpatialClustering::setBucketer(BucketerPtr& bucketer)
{
    m_bucketer = bucketer;
}


void SpatialClustering::clearBucketer()
{
    m_bucketer = nullptr;
}


MergeBoundaryLookup SpatialClustering::discoverMergeBoundaries(const UsdStageWeakPtr& stage,
                                                               const std::vector<std::string>& primPaths)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|SpatialClustering|discoverMergeBoundaries");

    // the lookup table to build data into
    MergeBoundaryLookup lookup;

    std::set<UsdPrim> resolvedPrimsSet;

    // Optimizations we can make in certain cases
    bool checkParentsForCamera = true;
    std::set<UsdPrim>* _resolvedPrimsSet = &resolvedPrimsSet;

    // Cache instanced prims so we don't merge outside their boundaries.
    std::set<UsdPrim> instancedPrims;
    _findInstancedPrims(stage, instancedPrims);

    // Quickly convert explicit paths to prims, or else use the pseudo root (which is a full traversal).
    // Also check if there are any regexes specified. For any explicit paths, record any that are invisible.
    std::vector<UsdPrim> prims;
    std::set<UsdPrim> invisiblePrims;

    if (!primPaths.empty())
    {
        // First, check for explicit prim paths, and separate out any expressions.
        // Note: We do this so abstract prims can be explicitly requested, without us traversing
        // them by default.
        std::vector<std::string> expressions;
        for (const auto& primPath : primPaths)
        {
            if (SdfPath::IsValidPathString(primPath))
            {
                const auto& prim = stage->GetPrimAtPath(SdfPath(primPath));
                if (prim)
                {
                    prims.push_back(prim);
                    continue;
                }
            }

            // If it isn't a valid path it might be an expression
            expressions.push_back(primPath);
        }

        // If anything didn't match an explicit prim, evaluate as an expression
        if (!expressions.empty())
        {
            constexpr bool meshesOnly = false;
            constexpr bool reverse = false;
            std::vector<UsdPrim> expressionPrims = _resolveExpressionsToPrims(stage, expressions, meshesOnly, reverse);
            prims.insert(prims.end(), expressionPrims.begin(), expressionPrims.end());
        }

        // Generally, invisible prims are skipped and not merged. However, if a prim is specified
        // explicitly (rather than merging the entire stage) then we want to override this check,
        // for that prim and its children.
        for (const auto& prim : prims)
        {
            const auto& imageable = UsdGeomImageable(prim);
            if (imageable)
            {
                if (imageable.ComputeVisibility() == UsdGeomTokens->invisible)
                {
                    invisiblePrims.insert(prim);
                }
            }
        }
    }
    else
    {
        // We know we are doing a full traversal and will prune cameras so we can optimize these
        // checks out.
        checkParentsForCamera = false;
        _resolvedPrimsSet = nullptr;

        prims.push_back(stage->GetPseudoRoot());
    }

    // Iterate either the root (full traversal) or any explicit paths, finding any meshes to merge.
    for (const auto& prim : prims)
    {
        // Find the first parent of the prim being traversed that is a merge boundary and use it as the common ancestor.
        // Merge Boundaries are anywhere that mesh prims must not be merged below in order to retain behavior or respect
        // the requested merge point.
        SdfPath commonAncestorPath = SdfPath::AbsoluteRootPath();
        // TODO: Add caching to _isMergeBoundary as this is often called for prims with overlapping ancestors.
        for (const auto& path : prim.GetPath().GetAncestorsRange())
        {
            if (_isMergeBoundary(stage->GetPrimAtPath(path), instancedPrims, m_mergePoint))
            {
                commonAncestorPath = path;
                break;
            }
        }

        // By default, skip invisible prims. However, if a prim was specified by explicit path and
        // happens to be invisible then we will enable traversal for it. This is to allow merging
        // say a geometry library that is hidden and referenced elsewhere.
        bool skipInvisible = true;

        // This check only matters if prims have been specified, generally indicated by them not being
        // the pseudo root.
        if (prim != stage->GetPseudoRoot())
        {
            auto _findIt = invisiblePrims.find(prim);
            if (_findIt != invisiblePrims.end())
            {
                skipInvisible = false;
            }
        }

        // Start a find mesh traversal from this given prim.
        _findMeshes(stage,
                    prim, // startPrim
                    stage->GetPrimAtPath(commonAncestorPath),
                    instancedPrims,
                    lookup,
                    _resolvedPrimsSet,
                    checkParentsForCamera,
                    m_mergePoint,
                    skipInvisible);
    }

    // Reverse paths to start at leaves so traversal for deletion will work
    for (auto& it : lookup.parentToPrims)
    {
        std::reverse(it.second.begin(), it.second.end());
    }

    return lookup;
}


void SpatialClustering::bucket(const Operation* operation,
                               const MergeBoundaryLookup& lookup,
                               const std::vector<VirtualMesh>& virtualMeshes,
                               const UsdStageWeakPtr& stage,
                               std::map<SdfPath, std::vector<VirtualMesh>>& groupedMeshes,
                               std::vector<VirtualMesh>& mergeableMeshes)
{
    // Split the "rootPath" argument into the relative prim path for the parent of output prims and the name to be
    // used for the output prims. The variables will remain empty if no value could be determined.
    SdfPath parentPath;
    TfToken name; // unused
    _getParentAndName(m_rootPath, parentPath, name);

    // setup and run bucketing unless a Bucketer has been explicitly set already
    if (m_bucketer == nullptr)
    {
        m_bucketer = std::make_shared<Bucketer>(operation->getContext());
        m_bucketer->SetReport(operation->getReport());

        m_bucketer->SetConsiderMaterials(m_considerMaterials);
        m_bucketer->SetAllowSingleMeshes(m_allowSingleMeshes);

        // Use the spatial vertex count instead of the max size when clustering by vertex.
        // Note: this is primarily just to allow having separate arguments in the UI that are more descriptive,
        // without having to pass two all the way through since we can just re-use one depending on mode.
        const double maxSize =
            m_spatialMode == ClusterMode::eVertexCount ? (double)m_spatialVertexCount : m_spatialMaxSize;

        // Spatial configuration
        m_bucketer->SetSpatialArgs(m_spatialMode, m_spatialThreshold, maxSize);

        // Configure the Bucketer to handle attributes in the way clustering expects.
        _populateMergeBucketerAttributes(m_bucketer, m_considerAllAttributes);

        // add the attributes to treat as primvars as authored attributes
        if (!m_treatAsPrimvars.empty())
        {
            std::vector<TfToken> treatAsPrimvarTokens;
            treatAsPrimvarTokens.reserve(m_treatAsPrimvars.size());
            for (const auto& attrName : m_treatAsPrimvars)
            {
                treatAsPrimvarTokens.push_back(TfToken(attrName));
            }
            m_bucketer->AddAuthoredAttributes(treatAsPrimvarTokens);
        }

        // If we're using material albedo colors, add display color and opacity to the attributes that authored values
        // should be considered for
        if (m_materialAlbedoAsVertexColors)
        {
            m_bucketer->AddAuthoredAttributes(
                { UsdGeomTokens->primvarsDisplayColor, UsdGeomTokens->primvarsDisplayOpacity });
        }

        // process virtual meshes and lookup their parent path/merge boundary and group them by this
        // NOTE: this currently only supports Virtual meshes that have a derived/original prim that was identified as a
        //       mergeable mesh prim during discoverMergeBoundaries - in future its possible we'll need to support
        //       something more advanced here
        PathToVirtualMeshes meshGroups;
        mergeableMeshes.reserve(virtualMeshes.size());
        for (VirtualMesh mesh : virtualMeshes)
        {
            auto findParentPath = lookup.primToParent.find(mesh.getSourcePath());
            if (findParentPath != lookup.primToParent.end())
            {
                // if material albedo as vertex colors is set, then replace the material binding with its material
                // albedo
                if (m_materialAlbedoAsVertexColors)
                {
                    // get the material of this mesh
                    UsdShadeMaterial material(stage->GetPrimAtPath(mesh.getBoundMaterialPath()));

                    // get the base color of the meshes' material
                    ColorValue baseColor;
                    baseColor.color = GfVec3f(0.2f, 0.2f, 0.2f);
                    _getMaterialAlbedo(material, baseColor);

                    // replace the material of the virtual mesh with the display color
                    mesh.replaceMaterialWithDisplayColor(baseColor);
                }

                // TODO: need to find a way to do a reserve here!
                meshGroups[findParentPath->second].push_back(mesh);
                mergeableMeshes.push_back(mesh);
            }
            else
            {
                SO_LOG_WARN(
                    "Cannot perform clustering on VirtualMesh at \"%s\" because the merge boundary was not discovered "
                    "during discoverMergeBoundaries ",
                    mesh.getSourcePath().GetAsString().c_str());
            }
        }

        // now add grouped virtual meshes to the bucketer
        for (const auto& meshGroup : meshGroups)
        {
            SdfPath outputParentPath = (parentPath.IsEmpty()) ? meshGroup.first : meshGroup.first.AppendPath(parentPath);
            m_bucketer->AddVirtualMeshes(meshGroup.second, outputParentPath);
        }

        m_bucketer->Bucket(stage);
    }

    // we need to group the bucketed virtual meshes by their parent paths / merge boundaries
    // (don't use a VirtualMesh reference as they're reference counted and creating the VirtualMesh requires a non-const
    // call to computeGeometry)
    for (VirtualMesh virtualMesh : m_bucketer->GetOutputData())
    {
        groupedMeshes[virtualMesh.getDestinationParentPath()].push_back(virtualMesh);
    }
}


std::vector<UsdPrim> SpatialClustering::write(std::map<SdfPath, std::vector<VirtualMesh>>& groupedMeshes,
                                              std::vector<VirtualMesh>& mergeableMeshes,
                                              const UsdStageWeakPtr& stage,
                                              SdfLayerHandle& layer)
{
    clearCounters();

    // set up VirtualMesh global config
    VirtualMesh::ConfigLifetime virtualMeshConfig;
    virtualMeshConfig.setMergeAttrsAsPrimvars(m_treatAsPrimvars);

    // compute the target pivots of clustered prims based on their merge boundaries
    std::unordered_map<size_t, GfVec3f> targetPivots;
    UsdGeomXformCache xformCache = UsdGeomXformCache();
    for (auto& meshGroup : groupedMeshes)
    {
        // Get the inverse world to local matrix of the parent group
        GfMatrix4d inverseMatrix = _getFirstLocalToWorldTransform(stage, meshGroup.first, xformCache).GetInverse();

        for (VirtualMesh& virtualMesh : meshGroup.second)
        {
            GfVec3f centroid;
            if (_computeSupersetCentroid(virtualMesh, centroid))
            {
                // Set pivot to the centroid after the inverse matrix has been applied.
                targetPivots[virtualMesh.getId()] = GfVec3f(inverseMatrix.Transform(centroid));
            }
        }
    }

    m_output.clear();

    // Split the "rootPath" argument into the relative prim path for the parent of output prims and the name to be
    // used for the output prims. The variables will remain empty if no value could be determined.
    SdfPath parentPath;
    TfToken name;
    _getParentAndName(m_rootPath, parentPath, name);

    // use default name?
    if (name.IsEmpty())
    {
        name = m_defaultPrimName;
    }


    std::set<UsdPrim> removePrims;
    std::vector<SdfPath> clusteredPrimPaths;
    // enter into a change block to write new prims
    {
        SdfChangeBlock changeBlock;
        std::unordered_set<size_t> processedVirtualMeshIds;

        // now create VirtualMeshes under their parent paths
        for (auto& meshGroup : groupedMeshes)
        {
            // generate names and paths of the new prims
            SdfPath outputParentPath = meshGroup.first;
            // special case for "Original Prim" merge boundary where we need to use the parent path of the merge
            // boundary and prefix with the name of the original prim since resulting prims should be written next to
            // the original prim, not under it
            TfToken clusteredName = name;
            if (m_mergePoint == MergePointOption::eOriginalPrim)
            {
                clusteredName = TfToken(meshGroup.first.GetName() + "_" + name.GetString());
                outputParentPath = outputParentPath.GetParentPath();
            }
            const TfTokenVector clusteredPreferredNames(meshGroup.second.size(), clusteredName);
            SdfPathVector clusteredPaths = _getUniqueChildPaths(stage, outputParentPath, clusteredPreferredNames);

            // create a prim for each mesh
            size_t i = 0;
            for (VirtualMesh& virtualMesh : meshGroup.second)
            {
                // find the superset children of the virtual mesh to determine the virtual mesh ids that don't need to
                // be created and the derived prims that should be removed
                for (const VirtualMesh& child : virtualMesh.getSupersetChildren())
                {
                    processedVirtualMeshIds.insert(child.getId());
                    const UsdPrim& childPrim = child.getPrim();
                    if (childPrim)
                    {
                        removePrims.insert(childPrim);
                    }
                }
                // set debug color if needed
                if (m_spatialDebug)
                {
                    virtualMesh.useSpatialDebug();
                }
                // set path and create!
                virtualMesh.setDestinationPath(clusteredPaths[i++]);
                virtualMesh.createInLayer(stage, layer);
                clusteredPrimPaths.push_back(virtualMesh.getDestinationPath());

                m_output.push_back(virtualMesh);
            }
        }

        // now discover the remaining VirtualMeshes that need to be created and group them by their original path -
        // VirtualMeshes that are not directly derived from an existing prim and haven't been clustered
        std::map<SdfPath, std::vector<VirtualMesh>> unclusteredMeshes;
        for (const VirtualMesh& virtualMesh : mergeableMeshes)
        {
            if (virtualMesh.isDerivedFromPrim())
            {
                continue;
            }

            if (processedVirtualMeshIds.find(virtualMesh.getId()) == processedVirtualMeshIds.end())
            {
                unclusteredMeshes[virtualMesh.getSourcePath()].push_back(virtualMesh);
            }
        }

        // now generate unique paths for the unclustered VirtualMeshes and create them
        for (auto& [path, virtualMeshes] : unclusteredMeshes)
        {
            // TODO: currently this is using the name convention for splitting, but we should decide how we handle this
            const TfTokenVector unclusteredPreferredNames(virtualMeshes.size(), TfToken(path.GetName() + "_part"));
            SdfPathVector unclusteredPaths = _getUniqueChildPaths(stage, path.GetParentPath(), unclusteredPreferredNames);

            size_t i = 0;
            for (VirtualMesh& virtualMesh : virtualMeshes)
            {
                virtualMesh.setDestinationPath(unclusteredPaths[i++]);
                virtualMesh.createInLayer(stage, layer);
                // ensure the original prim is removed
                removePrims.insert(virtualMesh.getPrim());

                m_output.push_back(virtualMesh);
            }
        }
    }

    // update the counter for the number of prims created
    m_numPrimsCreated = m_output.size();

    // enter a changeblock to write pivots on new cluster meshes
    // note: this isn't ideal and we should find a way to do this within Sdf in VirtualMesh::createInLayer so everything
    //       can be done from a single change block
    {
        SdfChangeBlock _changeBlock;

        for (const auto& meshGroup : groupedMeshes)
        {
            for (const VirtualMesh& virtualMesh : meshGroup.second)
            {
                auto findPivot = targetPivots.find(virtualMesh.getId());
                if (findPivot == targetPivots.end())
                {
                    continue;
                }
                UsdGeomXformCommonAPI::Get(stage, virtualMesh.getDestinationPath()).SetPivot(findPivot->second);
            }
        }
    }

    // now that we stripped duplicates re-arrange the prims to remove as a vector
    std::vector<UsdPrim> removePrimsVec;
    removePrimsVec.insert(removePrimsVec.end(), removePrims.begin(), removePrims.end());

    {
        SdfChangeBlock changeBlock;
        _removePrims(m_originalGeomOption, stage, removePrimsVec);
    }

    // update the counter for the number of prims removed
    m_numPrimsRemoved = removePrimsVec.size();

    // clear the bucketer now that we're done
    m_bucketer = nullptr;

    // finally find the Usd prims that were created by clustering so we can return them
    std::vector<UsdPrim> clusteredPrims;
    clusteredPrims.reserve(clusteredPrimPaths.size());
    for (const SdfPath& primPath : clusteredPrimPaths)
    {
        UsdPrim prim = stage->GetPrimAtPath(primPath);
        if (prim)
        {
            clusteredPrims.push_back(prim);
        }
    }

    return clusteredPrims;
}


std::vector<UsdPrim> SpatialClustering::execute(const Operation* operation,
                                                const MergeBoundaryLookup& lookup,
                                                const std::vector<VirtualMesh>& virtualMeshes,
                                                const UsdStageWeakPtr& stage,
                                                SdfLayerHandle& layer)
{
    // Bucket
    std::vector<VirtualMesh> mergeableMeshes;
    std::map<SdfPath, std::vector<VirtualMesh>> groupedMeshes;
    bucket(operation, lookup, virtualMeshes, stage, groupedMeshes, mergeableMeshes);

    // Author
    return write(groupedMeshes, mergeableMeshes, stage, layer);
}


const std::vector<VirtualMesh>& SpatialClustering::getOutput() const
{
    return m_output;
}


void SpatialClustering::clearOutput()
{
    m_output.clear();
}


size_t SpatialClustering::getNumPrimsCreated() const
{
    return m_numPrimsCreated;
}


size_t SpatialClustering::getNumPrimsRemoved() const
{
    return m_numPrimsRemoved;
}


void SpatialClustering::clearCounters()
{
    m_numPrimsCreated = 0;
    m_numPrimsRemoved = 0;
}


} // namespace omni::scene::optimizer
