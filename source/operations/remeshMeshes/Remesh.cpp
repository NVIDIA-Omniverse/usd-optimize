// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "Remesh.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMesh
#include <OmniMeshOps/Remesh.h>
#include <OmniMeshOps/ScopedCudaContext.h>
#include <OmniMeshOps/usd/Mesh.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin with SO
SO_PLUGIN_INIT(omni::scene::optimizer::RemeshOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categoryRemesh = "REMESH";

RemeshOperation::RemeshOperation()
    : OmniOperation("remeshMeshes", "Remesh Meshes", "Remesh an input ``UsdGeom`` mesh primitive type.")
    , m_gradation(0)
    , m_maxError(0.1)
    , m_gpu_vertexcount_threshold(500000)
{
    addArgument("paths",
                "Meshes to Remesh",
                kDisplayTypePrimPaths,
                "Optional list of prim paths/expressions to remesh",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument(
        "gradation",
        "Gradation",
        kDisplayTypeFloatSlider,
        "The gradation for the remesh, affecting how many triangles are generated. [Note: this parameter will likely be replaced by something else]",
        m_gradation)
        .setMin(0)
        .setMax(0.5);

    addArgument("maxError", "Maximum Error", kDisplayTypeFloatSlider, "Maximum error for the remesh.", m_maxError).setMin(0);

    addArgument("gpuVertexCountThreshold",
                "GPU Vertex Threshold",
                kDisplayTypeIntSlider,
                "Use GPU algorithm if vertex count is greater than this value",
                m_gpu_vertexcount_threshold)
        .setMin(0)
        .setVisible(false);
}


RemeshOperation::~RemeshOperation(){};


std::string RemeshOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion RemeshOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string RemeshOperation::getCategory() const
{
    return s_categoryRemesh;
}


std::string RemeshOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


ProcessedData* RemeshOperation::processMesh(const UsdPrim& prim, tbb::task_group_context& taskGroupContext)
{
    using namespace omo;

    ProcessedData* result = nullptr;

    try
    {
        UsdGeomMesh usdMesh(prim);
        usd::HostMesh inputMesh(
            usdMesh,
            { omo::Defect::CoincidentVertices | omo::Defect::DegenerateEdges | omo::Defect::DegenerateFaces });

        auto use_gpu = inputMesh.vertexCount() > m_gpu_vertexcount_threshold && isCudaAvailable();
        if (!use_gpu)
        {
            HostRemesh remesh(inputMesh);
            {
                if (!taskGroupContext.is_group_execution_cancelled())
                {
                    auto remeshed_mesh = remesh(m_gradation, m_maxError);
                    if (!taskGroupContext.is_group_execution_cancelled())
                    {
                        result = new ProcessedHostMesh(remeshed_mesh, prim);
                    }
                }
            }
        }
        else
        {
            ScopedCudaContext cuda_context(omo::Verbose{ getContext()->verbose > 0 });
            DeviceRemesh device_remesh{ DeviceMesh{ inputMesh } };

            if (!taskGroupContext.is_group_execution_cancelled())
            {
                HostMesh host_remeshed_mesh(device_remesh(m_gradation, m_maxError));
                if (!taskGroupContext.is_group_execution_cancelled())
                {
                    result = new ProcessedHostMesh(host_remeshed_mesh, prim);
                }
            }
        }

        if (result != nullptr)
        {
            std::ostringstream oss;
            oss << prim.GetName().GetString() << ": "
                << "\nBefore:  Faces: " << inputMesh.faceCount() << "  Vertices: " << inputMesh.vertexCount()
                << "\nAfter:  Faces: " << result->faceCount() << "  Vertices: " << result->vertexCount();
            SO_LOG_VERBOSE(oss.str().c_str());
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = std::string(e.what()) + " (Prim: " + prim.GetPath().GetText() + ")";
        SO_LOG_ERROR(errorMsg.c_str());
        if (result)
        {
            delete result;
            result = nullptr;
        }

        // Cancel further task execution
        if (taskGroupContext.cancel_group_execution())
        {
            SO_LOG_ERROR("Cancelling execution due to exception...");
        }
    }

    if (result != nullptr && taskGroupContext.is_group_execution_cancelled())
    {
        delete result;
        result = nullptr;
    }

    return result;
}


} // namespace omni::scene::optimizer
