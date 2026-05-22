// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "OptimizeSkelRoots.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>

// Merge Operation
#include <merge/Merge.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primFlags.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/cache.h>
#include <pxr/usd/usdSkel/root.h>
#include <pxr/usd/usdUtils/stageCache.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::OptimizeSkelRootsOperation);


namespace omni::scene::optimizer
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((primvarsSkelGeomBindTransform, "primvars:skel:geomBindTransform"))
    ((skelSkinningMethod, "skel:skinningMethod"))
    ((xformOpTransform, "xformOp:transform"))
    (Looks)
    (Material)
    (Scope)
);
// LCOV_EXCL_STOP
// clang-format on


// Merge meshes with an awareness of the specifics of UsdSkel Skinning properties
static void _mergeSkinnedMeshes(ExecutionContext* context, const UsdStageWeakPtr& usdStage, const SdfPath& skelRootPath)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|_mergeSkinnedMeshes");

    // Bucket arguments unique to skinned mesh prims
    // Differing materials will be translated to subset material bindings
    bool considerMaterials = false;

    // Attributes specific to UsdSkel that cannot be merged and who's presence and value define bucket uniqueness
    const TfTokenVector& commonAttributeNames = { _tokens->primvarsSkelGeomBindTransform, _tokens->skelSkinningMethod };

    // Construct a SkelRoot and SkelCache for the given prim so we can make queries through the UsdSkel API
    const UsdSkelRoot& skelRoot = UsdSkelRoot::Get(usdStage, skelRootPath);
    UsdSkelCache skelCache;
    skelCache.Populate(skelRoot, UsdPrimDefaultPredicate);

    // Iterate over each of the Skeletons below the SkelRoot and merge the skinned Meshes
    std::vector<UsdSkelBinding> skelBindings;
    skelCache.ComputeSkelBindings(skelRoot, &skelBindings, UsdPrimDefaultPredicate);

    for (const UsdSkelBinding& skelBinding : skelBindings)
    {
        // Build a list of all the Mesh prims that are bound to the UsdSkelSkeleton and could be merged
        std::vector<UsdPrim> skinnedMeshPrims;

        for (const UsdSkelSkinningQuery& skinningTarget : skelBinding.GetSkinningTargets())
        {
            // Skip skinning targets with blend shapes as we do not support merging them.
            if (skinningTarget.HasBlendShapes())
            {
                continue; // LCOV_EXCL_LINE
            }
            skinnedMeshPrims.push_back(skinningTarget.GetPrim());
        }

        // Early out if there are no skinned meshes to merge
        if (skinnedMeshPrims.empty())
        {
            continue; // LCOV_EXCL_LINE
        }

        // ensure we can get the merge operation
        auto& core = SceneOptimizerCore::getInstance();
        auto mergeOp = core.getOperation("merge");
        if (mergeOp == nullptr)
        {
            return; // LCOV_EXCL_LINE
        }


        // Prepare execution context
        ExecutionContext childContext;
        so_execution_context_copy(&childContext, context);

        // create VirtualMeshes for of the skinned mesh prims so we can add it to the bucketer
        std::vector<VirtualMesh> virtualMeshes;
        virtualMeshes.reserve(skinnedMeshPrims.size());
        UsdGeomXformCache xformCache;
        UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
        UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
        for (const UsdPrim& prim : skinnedMeshPrims)
        {
            virtualMeshes.emplace_back(prim, xformCache, bindingsCache, collQueryCache);
        }

        // Bucket the meshes based on the ability for them to be merged
        BucketerPtr bucketer = std::make_shared<Bucketer>(&childContext);
        bucketer->SetConsiderMaterials(considerMaterials);
        _populateMergeBucketerAttributes(bucketer, false);
        bucketer->AddValueAttributes(commonAttributeNames);
        bucketer->AddVirtualMeshes(virtualMeshes, skelRootPath);
        bucketer->Bucket(usdStage);

        // Merge the skinned Meshes
        // Meshes skinned to a skeleton must live below the SkelRoot so the rootPath needs to express that
        MergeUserData userData{};
        userData.bucketer = bucketer;
        userData.considerSkeleton = true;

        // Set user data
        mergeOp->setUserData(&userData);

        // Merge. No args required, using the defaults other than the user data.
        mergeOp->execute(&childContext, JsObject());
        so_execution_context_free(&childContext);

        auto& newPrims = userData.mergedPrims;

        // Ensure that the new meshes are skinned to the original skeleton
        const UsdSkelSkeleton& skeleton = skelBinding.GetSkeleton();
        const SdfPathVector skeletonPaths = { skeleton.GetPrim().GetPath() };
        for (const UsdPrim& newPrim : newPrims)
        {
            UsdSkelBindingAPI skelBindingApi = UsdSkelBindingAPI::Apply(newPrim);
            skelBindingApi.CreateSkeletonRel().SetTargets(skeletonPaths);
        }
    }
}

constexpr const char* s_category = "OPTIMIZE_SKELROOTS";

OptimizeSkelRootsOperation::OptimizeSkelRootsOperation()
    : Operation("optimizeSkelRoots",
                "Optimize Skeleton Roots",
                "This operation will merge all meshes for meshes attached to a skeleton. This can greatly improve "
                "character playback speed by optimizing scenes for GPU skinning computation.")
{
}


std::string OptimizeSkelRootsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion OptimizeSkelRootsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string OptimizeSkelRootsOperation::getCategory() const
{
    return s_category;
}


// Find and optimize all the UsdSkel setups in a given a Usd Stage
OperationResult OptimizeSkelRootsOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|_optimizeSkelRoots");

    constexpr bool meshesOnly = false;
    constexpr bool reverse = false;
    const Usd_PrimFlagsPredicate& predicate = UsdPrimDefaultPredicate;

    // Custom resolve callback to filter on SkelRoot
    auto callback = [](const UsdPrim& prim, UsdPrimRange::iterator& iter) -> bool
    {
        // If this is a SkelRoot we can also prune traversal, as nested roots are
        // not supported.
        if (prim.IsA<UsdSkelRoot>())
        {
            iter.PruneChildren();
            return true;
        }

        return false;
    };

    // Resolve Expressions.
    const std::vector<UsdPrim>& skelRootPrims =
        _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(), {}, meshesOnly, reverse, predicate, callback);

    // Optimize each of the UsdSkel setups
    for (const auto& skelRootPrim : skelRootPrims)
    {
        // Merge the skinned Meshes
        _mergeSkinnedMeshes(getContext(), getUsdStage(), skelRootPrim.GetPath());
    }

    return { true };
}


} // namespace omni::scene::optimizer
