// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "FindOccludedMeshes.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/CudaUtils.h>
#include <usd_optimize/core/JsonUtils.h>
#include <usd_optimize/core/MeshToolsCommon.h>
#include <usd_optimize/core/ResolveSdfPaths.h>
#include <usd_optimize/core/Utils.h>

// Mesh tools
#include <MeshTools/Stage.h>
#include <MeshTools/VisCheckerCPU.h>
#include <MeshTools/VisCheckerGPU.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primRange.h>

PXR_NAMESPACE_USING_DIRECTIVE

USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::FindOccludedMeshesOperation);

using namespace MeshTools;


namespace usd_optimize
{

/// Constants
constexpr const char* s_categoryFindOccludedMeshes = "FIND_OCCLUDED_MESHES";


FindOccludedMeshesOperation::FindOccludedMeshesOperation()
    : Operation(
          "findOccludedMeshes",
          "Find Occluded Meshes",
          "Finds meshes that are globally occluded meaning they are occluded from any camera that does not cross meshes in the scene.")
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


std::string FindOccludedMeshesOperation::getDocumentation() const
{
    return R"DOC(Analyses a scene to find meshes that are globally occluded: meshes not visible from any
camera that does not have to cross other geometry to see them (for example, geometry sealed inside a
closed enclosure). It is an analysis operation that flags candidates to be deactivated, hidden, or
deleted; the bias is conservative, so a mesh is only reported when it is confidently hidden.

How it works
------------

The scene is rasterized into a voxel grid and visibility is flood-filled from the exterior. A mesh is
considered occluded when no exterior path reaches it. The check runs on GPU when ``useGpu`` is enabled
(the default; it falls back to CPU if CUDA is unavailable or the GPU check fails) and on CPU otherwise. Leave
it on: same meshes, much faster. Out of GPU memory, the CPU rerun is much slower; lower
``maximumGridResolution`` to stay on the GPU.

Tuning
------

- ``maximumGridResolution`` caps the number of cells along the longest axis. Higher values detect smaller
  openings but cost cubically more memory and time (500 suits a powerful GPU; use less for CPU).
- ``minimumGapSize`` is the smallest opening, in **stage units**, treated as a gap. Effective grid
  spacing is ``max(minimumGapSize, maxDim / maximumGridResolution)``. Smaller values produce a finer grid
  that finds smaller openings and flags fewer meshes as occluded. It acts as a tolerance for how sealed
  an enclosure must be (e.g. 3.5 means ignore any opening smaller than 3.5 stage units). Scale it with
  ``metersPerUnit``.
- ``clustered`` splits the stage into clusters of meshes with overlapping bounds and checks each cluster
  separately, improving both accuracy and performance. Clustering runs on the CPU and its cost grows faster
  than the mesh count, so on scenes with thousands of meshes pass one sub-assembly's meshes at a time in
  ``paths`` (mesh paths; a parent prim does not expand). Only meshes in ``paths`` occlude, so keep each mesh's
  enclosure in the same selection. Turning ``clustered`` off is fast but misses most occluded meshes.
- ``checkTransparency`` excludes meshes with opacity < 1.0 from occlusion testing.

Starting configurations
-----------------------

Standard analysis:

.. code-block:: json

    [{"operation": "findOccludedMeshes", "clustered": true, "checkTransparency": true}]

Conservative (finer grid, smaller gaps detected):

.. code-block:: json

    [{"operation": "findOccludedMeshes", "minimumGapSize": 0.01, "maximumGridResolution": 500}]
)DOC";
}


std::string FindOccludedMeshesOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion FindOccludedMeshesOperation::getVersion() const
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
    CARB_PROFILE_ZONE(0, "UsdOptimize|FindOccludedMeshes|Analysis");

    // analysis mode is the same as execute
    m_attributePaths.clear();
    OperationResult evalResult = executeImpl();
    if (!evalResult.success)
    {
        USD_OPTIMIZE_LOG_ERROR("Failed to execute operation.");
        return evalResult;
    }

    // Convert results to JSON payload
    JsObject analysisResult;
    analysisResult["occludedMeshes"] = _toJson(m_attributePaths);

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));
    USD_OPTIMIZE_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult FindOccludedMeshesOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|findOccludedMeshes");

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
        USD_OPTIMIZE_LOG_INFO("No prims to process");
        return { true };
    }

    auto stage = GetStage(getUsdStage(), primsToProcess, m_checkTransparency);

    if (stage->meshes().empty())
    {
        USD_OPTIMIZE_LOG_INFO("No prims to process");
        return { true };
    }

    bool OK = false;

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
                USD_OPTIMIZE_LOG_WARN(
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
        if (!OK)
        {
            USD_OPTIMIZE_LOG_WARN("GPU check failed. Falling back to CPU.");
        }
    }
    if (!OK)
    {
        VisCheckerCPU visChecker;
        OK = visChecker.check(*stage, params);
    }
    // mesh_tools_lib 1.0.2+ returns false (rather than throwing) on degenerate input; any
    // unexpected exception is still caught by Operation::execute()'s framework handler.

    if (!OK)
    {
        USD_OPTIMIZE_LOG_ERROR("Finding hidden meshes failed!");
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
    USD_OPTIMIZE_LOG_INFO("Found %s hidden mesh%s", std::to_string(hiddenCount).c_str(), suffix.c_str());

    return { true };
}

} // namespace usd_optimize
