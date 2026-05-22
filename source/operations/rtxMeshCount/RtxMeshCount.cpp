// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "RtxMeshCount.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/geometry/GeometryProcessor.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::RtxMeshCountOperation);


namespace omni::scene::optimizer
{


// Constants
constexpr const char* s_category = "RTX_MESH_COUNT";


RtxMeshCountOperation::RtxMeshCountOperation()
    : Operation("rtxMeshCount",
                "RTX Mesh Count",
                "Analysis operation for counting the number of RTX Meshes in the stage and how many are unique.")
{
    addArgument("paths", "Paths", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_paths)
        .setPlaceholder("Add paths or entire scene will be processed");
}


RtxMeshCountOperation::~RtxMeshCountOperation() = default;


std::string RtxMeshCountOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion RtxMeshCountOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string RtxMeshCountOperation::getCategory() const
{
    return s_category;
}


std::string RtxMeshCountOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


bool RtxMeshCountOperation::getVisible() const
{
    return false;
}


bool RtxMeshCountOperation::getSupportsAnalysis() const
{
    return true;
}


OperationResult RtxMeshCountOperation::executeImpl()
{
    // analysis-only operation
    return { true };
}


OperationResult RtxMeshCountOperation::executeAnalysisImpl()
{
    // set up and run the geometry processor
    GeometryProcessor processor(getUsdStage(), m_paths);
    processor.setComputeRtxMeshCount(true);
    processor.execute();

    // Create result
    OperationResult result{ true, nullptr, nullptr };
    JsObject analysisResult;
    analysisResult["rtxAccelStructCount"] = _toJson(processor.getRtxAccelStructCount());
    analysisResult["rtxMeshCount"] = _toJson(processor.getRtxMeshCount());
    analysisResult["rtxUniqueMeshCount"] = _toJson(processor.getRtxUniqueMeshCount());
    analysisResult["rtxMeshPrims"] = _toJson(processor.getRtxMeshPrims());
    JsObject resultJson;
    resultJson["analysis"] = analysisResult;
    result.output = getCStr(JsWriteToString(resultJson));

    processor.clear();

    return result;
}


} // namespace omni::scene::optimizer
