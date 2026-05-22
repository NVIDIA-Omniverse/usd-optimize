// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "FindOccludedMeshes.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/MeshToolsCommon.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// Mesh tools
#include <MeshTools/Stage.h>
#include <MeshTools/VisCheckerCPU.h>
#include <MeshTools/VisCheckerGPU.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primRange.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::FindOccludedMeshesOperation);

using namespace MeshTools;


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categoryFindOccludedMeshes = "FIND_OCCLUDED_MESHES";


FindOccludedMeshesOperation::FindOccludedMeshesOperation()
    : Operation("findOccludedMeshes", "Find Occluded Meshes", "This operation finds meshes that are occluded by others.")
{

    addArgument("paths",
                "Meshes used for occlusion testing",
                kDisplayTypePrimPaths,
                "Meshes that are tested for occlusion as well as considered as occluders",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument(
        "clustered",
        "Clustered",
        kDisplayTypeBool,
        "Split the stage into clusters of meshes with overlapping bounding boxes and check visibility per cluster, improving both accuracy and performance by reducing the number of meshes compared at the same time",
        m_clustered);

    addArgument(
        "minimumGapSize",
        "Minimum gap size",
        kDisplayTypeFloat,
        "The minimum gap size corresponding to the spacing of the background grid. Gaps smaller than this value are considered closed for occlusion culling. "
        "The actual grid spacing is max(minimumGapSize, maxDim/maximumGridResolution). "
        "Very small values defer to maximumGridResolution for spacing, producing a finer grid that detects smaller gaps and results in fewer meshes being flagged as occluded. "
        "It is essentially a tolerance for how sealed an enclosure needs to be: "
        "e.g. a value of 3.5 means ignore any opening smaller than 3.5 scene units when deciding if something is hidden",
        m_minimumGapSize)
        .setMin(0.0);

    addArgument(
        "maximumGridResolution",
        "Maximum grid resolution",
        kDisplayTypeFloat,
        "The maximum number of cells along the longest axis of the grid used for visibility checking. "
        "This caps the grid resolution to prevent excessive memory and compute costs (the grid is 3D, so memory scales with the cube of resolution). "
        "A value of 500 is suitable for powerful GPUs, use smaller values for less powerful GPUs or CPUs",
        m_maximumGridResolution)
        .setMin(1.0);

    addArgument("checkTransparency",
                "Check Transparency",
                kDisplayTypeBool,
                "Exclude meshes with opacity < 1.0 from occlusion testing",
                m_checkTransparency);

    addArgument("action", "Action", kDisplayTypeEnum, "What to do with occluded meshes", m_action)
        .setEnumValues<RemoveMethod>({ { RemoveMethod::eDelete, "Delete" },
                                       { RemoveMethod::eDeactivate, "Deactivate" },
                                       { RemoveMethod::eHide, "Hide" } });

    // Do not expose GPU argument - we generally want to use GPU since it is much faster.
    // Keep the argument hidden in case we need to override.
    addArgument("useGpu", "Use GPU", kDisplayTypeBool, "Choose whether to use GPU or CPU algorithm", m_useGpu)
        .setVisible(false);
}


std::string FindOccludedMeshesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion FindOccludedMeshesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string FindOccludedMeshesOperation::getCategory() const
{
    return s_categoryFindOccludedMeshes;
}


bool FindOccludedMeshesOperation::getSupportsAnalysis() const
{
    return true;
}


OperationResult FindOccludedMeshesOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|FindOccludedMeshes|Analysis");

    // analysis mode is the same as execute
    m_attributePaths.clear();
    OperationResult evalResult = executeImpl();
    if (!evalResult.success)
    {
        SO_LOG_ERROR("Failed to execute operation.");
        return evalResult;
    }

    // Convert results to JSON payload
    JsObject analysisResult;
    analysisResult["occludedMeshes"] = _toJson(m_attributePaths);

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));
    SO_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult FindOccludedMeshesOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|findOccludedMeshes");

    // Config
    constexpr bool meshesOnly = true;
    constexpr bool reverse = true;
    const Usd_PrimFlagsPredicate& predicate = UsdPrimAllPrimsPredicate;

    // Custom resolve callback to filter out anything with time samples
    auto callback = [](const UsdPrim& prim, UsdPrimRange::iterator&) -> bool { return !_hasAuthoredTimeSamples(prim); };

    // Resolve Expressions.
    const std::vector<UsdPrim>& primsToProcess =
        _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(), m_meshPrimPaths, meshesOnly, reverse, predicate, callback);

    if (primsToProcess.empty())
    {
        SO_LOG_INFO("No prims to process");
        return { true };
    }

    auto stage = GetStage(getUsdStage(), primsToProcess, m_checkTransparency);

    if (stage->meshes().empty())
    {
        SO_LOG_INFO("No prims to process");
        return { true };
    }

    bool OK = true;

    VisCheckerParams params;
    params.clustered = m_clustered;
    params.minimumGapSize = m_minimumGapSize;
    params.granularity = MeshTools::Granularity::MESH;

    params.maximumGridResolution = m_maximumGridResolution;

    // Warn if maximumGridResolution is capping the grid resolution, resulting in a coarser grid than minimumGapSize
    // requests
    {
        Vec3 dimensions = stage->getAABB().getDimensions();
        float maxDim = std::max({ dimensions.x, dimensions.y, dimensions.z });
        if (maxDim > 0.0f && params.minimumGapSize > 0.0f)
        {
            float desiredResolution = maxDim / params.minimumGapSize;
            float maxRes = m_maximumGridResolution;
            if (desiredResolution > maxRes)
            {
                float effectiveGapSize = maxDim / maxRes;
                SO_LOG_WARN(
                    "maximumGridResolution (%.0f) is capping the grid resolution. "
                    "Effective minimum gap size is %.2f instead of the requested %.2f",
                    m_maximumGridResolution,
                    effectiveGapSize,
                    params.minimumGapSize);
            }
        }
    }

    if (m_useGpu && isCudaAvailable())
    {
        VisCheckerGPU visChecker;
        OK = visChecker.check(*stage, params);
    }
    else
    {
        VisCheckerCPU visChecker;
        OK = visChecker.check(*stage, params);
    }

    if (!OK)
    {
        SO_LOG_ERROR("Finding hidden meshes failed!");
        return { false };
    }

    // modify meshes in the scene according to the desired action

    auto meshes = stage->meshes();

    std::vector<UsdPrim> hiddenPrims;
    std::vector<UsdPrim> visiblePrims;

    for (auto& mesh : meshes)
    {
        auto prim = getUsdStage()->GetPrimAtPath(SdfPath(mesh->path()));

        // Analysis mode - just record if mesh is occluded
        if (getContext()->analysisMode)
        {
            if (!mesh->isVisible())
            {
                m_attributePaths.push_back(prim.GetPath().GetAsString());
            }
            continue;
        }

        if (!mesh->isVisible())
        {
            hiddenPrims.push_back(prim);
        }
        else
        {
            visiblePrims.push_back(prim);
        }
    }

    // only remove the prims if we're not in analysis mode
    if (!getContext()->analysisMode)
    {
        _removePrims(m_action, getUsdStage(), hiddenPrims, visiblePrims);
    }

    // Log the appropriate count based on mode
    size_t hiddenCount = getContext()->analysisMode ? m_attributePaths.size() : hiddenPrims.size();
    std::string suffix = hiddenCount == 1 ? "" : "es";
    SO_LOG_INFO("Found %s hidden mesh%s", std::to_string(hiddenCount).c_str(), suffix.c_str());

    return { true };
}

} // namespace omni::scene::optimizer
