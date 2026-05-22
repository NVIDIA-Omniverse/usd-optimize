// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "Merge.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>

// Carbonite
#include <carb/profiler/Profile.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::MergeOperation);


namespace omni::scene::optimizer
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((merged, "merged"))
);
// LCOV_EXCL_STOP
// clang-format on

/// Constants
constexpr const char* s_categoryMerge = "MERGE";


MergeOperation::MergeOperation()
    : Operation("merge",
                "Merge Static Meshes",
                "The merge static meshes operation replaces multiple meshes that share common properties with a "
                "single merged mesh. This reduces scene prim count and can improve overall stage performance.")
{
    addArgument("meshPrimPaths",
                "Static Meshes to Merge",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider for merging",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    m_clustering.addConsiderMaterialsArg(this);
    m_clustering.addMaterialAlbedoAsVertexColorsArg(this);
    m_clustering.addOriginalGeomOptionArg(this);
    m_clustering.addMergePointArg(this);
    m_clustering.addRootPathArg(this).setPlaceholder("Merged_0");
    m_clustering.addConsiderAllAttributesArg(this);
    m_clustering.addAllowSingleMeshesArg(this);
    m_clustering.addSpatialModeArg(this);
    m_clustering.addSpatialThresholdArg(this, "spatialMode == 1");
    m_clustering.addSpatialMaxSizeArg(this, "spatialMode == 1");
    m_clustering.addSpatialVertexCountArg(this, "spatialMode == 2");
    m_clustering.addTreatAsPrimvarsArg(this);
    m_clustering.addSpatialDebugArg(this);
}


MergeOperation::~MergeOperation(){};


std::string MergeOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion MergeOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string MergeOperation::getCategory() const
{
    return s_categoryMerge;
}


std::string MergeOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


void MergeOperation::setUserData(void* userData)
{
    // Configure operation based on merge data
    auto mergeData = reinterpret_cast<MergeUserData*>(userData);
    m_clustering.setBucketer(mergeData->bucketer);
    m_clustering.m_considerSkeleton = mergeData->considerSkeleton;

    // Record for later - required for output
    m_userData = mergeData;
}


OperationResult MergeOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|MergeOperation|Execute");

    // build the lookup table for mesh merge boundaries which also contains the mesh prims to be merged
    MergeBoundaryLookup meshLookup = m_clustering.discoverMergeBoundaries(getUsdStage(), m_meshPrimPaths);

    m_clustering.setDefaultPrimName(_tokens->merged);

    // create VirtualMeshes for each mesh prim
    std::vector<VirtualMesh> virtualMeshes;
    UsdGeomXformCache xformCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
    for (const auto& it : meshLookup.parentToPrims)
    {
        for (const auto& prim : it.second)
        {
            virtualMeshes.emplace_back(prim, xformCache, bindingsCache, collQueryCache);
        }
    }

    // call common clustering module
    SdfLayerHandle editLayer = getUsdStage()->GetEditTarget().GetLayer();
    std::vector<UsdPrim> mergedPrims = m_clustering.execute(this, meshLookup, virtualMeshes, getUsdStage(), editLayer);

    // If there is userData, copy the merged prims as an output.
    if (m_userData != nullptr)
    {
        m_userData->mergedPrims = mergedPrims;
    }

    if (getContext()->generateReport)
    {
        for (const VirtualMesh& mesh : m_clustering.getOutput())
        {
            const std::vector<VirtualMesh>& children = mesh.getSupersetChildren();

            SO_LOG_INFO("Output Mesh: %s contains %s",
                        mesh.getDestinationPath().GetAsString().c_str(),
                        std::to_string(children.size()).c_str());

            for (const VirtualMesh& child : children)
            {
                SO_LOG_INFO(child.getSourcePath().GetAsString().c_str());
            }
        }
    }

    const std::string suffix = mergedPrims.size() == 1 ? "" : "es";
    SO_LOG_INFO("Created %s Merged Mesh%s", +std::to_string(mergedPrims.size()).c_str(), suffix.c_str());

    // Create result
    OperationResult result{ true, nullptr, nullptr };

    // build the result to return
    if (!mergedPrims.empty())
    {
        result.output = _toJsonStr(mergedPrims);
    }

    return result;
}


} // namespace omni::scene::optimizer
