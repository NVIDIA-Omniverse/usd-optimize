// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "Subdivide.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMesh
#include <OmniMeshOps/ScopedCudaContext.h>
#include <OmniMeshOps/Subdivide.h>
#include <OmniMeshOps/usd/Mesh.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::SubdivideOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categorySubdivide = "SUBDIVIDE";

SubdivideOperation::SubdivideOperation()
    : OmniOperation("subdivideMeshes", "Subdivide Meshes", "This operation subdivides meshes.")
    , m_gpu_face_count_threshold(4000)
    , m_face_count_limit(2000000)
    , m_method(Method::eCatmullClark)
    , m_iteration_count(1)
{

    addArgument("paths",
                "Meshes to subdivide",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("gpuFaceCountThreshold",
                "GPU face count threshold",
                kDisplayTypeInt,
                "When a mesh will more than this number of faces after subdivision, use GPU algorithm",
                m_gpu_face_count_threshold)
        .setMin(0);

    addArgument("faceCountLimit",
                "Maximum face count",
                kDisplayTypeInt,
                "If the subdivided mesh would have more than this number of faces, it will not be generated",
                m_face_count_limit)
        .setMin(4);

    addArgument("method", "Subdivision Method", kDisplayTypeEnum, "Which subdivision method to use", m_method)
        .setEnumValues<Method>({ { Method::eCatmullClark, "Catmull-Clark" }, { Method::eLoop, "Loop" } });

    addArgument("iterationCount",
                "Subdivision Iteration Count",
                kDisplayTypeInt,
                "The number of times to subdivide",
                m_iteration_count)
        .setMin(1)
        .setMax(10);
}


std::string SubdivideOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion SubdivideOperation::getVersion() const
{
    return { 1, 1, 1 };
}


std::string SubdivideOperation::getCategory() const
{
    return s_categorySubdivide;
}


std::string SubdivideOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


static size_t _subdividedFaceCount(const omo::usd::HostMesh& mesh, omo::SubdivisionMethod method, uint32_t iteration_count)
{
    switch (method)
    {
    default:
        return 0;
    case omo::SubdivisionMethod::CatmullClark:
        return iteration_count ? size_t(std::pow(4, iteration_count - 1)) * mesh.cornerCount() : mesh.faceCount();
    case omo::SubdivisionMethod::Loop:
        return size_t(std::pow(4, iteration_count)) * (mesh.cornerCount() - size_t(2) * mesh.faceCount());
    }
}


static const char* _subdivisionMethodName(omo::SubdivisionMethod method)
{
    switch (method)
    {
    default:
        return "Unknown";
    case omo::SubdivisionMethod::CatmullClark:
        return "Catmull-Clark";
    case omo::SubdivisionMethod::Loop:
        return "Loop";
    }
}


ProcessedData* SubdivideOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{

    using namespace omo;

    ProcessedData* result = nullptr;

    UsdGeomMesh usdMesh(prim);
    omo::usd::HostMesh inputMesh{ usdMesh };

    // early out for empty meshes
    if (inputMesh.vertexCount() == 0)
    {
        return new ProcessedHostMesh(inputMesh, prim);
    }

    const std::string primPath = prim.GetPath().GetAsString();

    const size_t subdividedFaceCount = _subdividedFaceCount(inputMesh, m_subdivisionMethod, m_iteration_count);

    if (subdividedFaceCount > m_face_count_limit)
    {
        SO_LOG_WARN(
            "Prim: %s\nwould have %d faces after %d iterations of\n"
            "%s subdivision, exceeding the set limit. Skipping.",
            primPath.c_str(),
            subdividedFaceCount,
            m_iteration_count,
            _subdivisionMethodName(m_subdivisionMethod));
        return new ProcessedHostMesh(inputMesh, prim);
    }

    auto use_gpu_subdivider = subdividedFaceCount >= m_gpu_face_count_threshold && isCudaAvailable();

    if (!use_gpu_subdivider)
    {
        HostSubdivide subdivide{ inputMesh, m_subdivisionMethod };
        auto subd_mesh = subdivide(m_iteration_count);
        result = new ProcessedHostMesh(subd_mesh, prim);
    }
    else
    {
        ScopedCudaContext cuda_context(omo::Verbose{ getContext()->verbose > 0 });
        DeviceSubdivide subdivide{ DeviceMesh{ inputMesh }, m_subdivisionMethod };
        HostMesh hostSubdMesh(subdivide(m_iteration_count));
        result = new ProcessedHostMesh(hostSubdMesh, prim);
    }

    SO_LOG_VERBOSE("Prim: %s\n[%s, %s] %zu -> %zu faces",
                   primPath.c_str(),
                   (use_gpu_subdivider ? "GPU" : "CPU"),
                   _subdivisionMethodName(m_subdivisionMethod),
                   inputMesh.faceCount(),
                   result->faceCount());

    return result;
}


OperationResult SubdivideOperation::executePre()
{
    using namespace omo;

    // Convert and cache method once
    switch (m_method)
    {
    default:
    case Method::eCatmullClark:
        m_subdivisionMethod = SubdivisionMethod::CatmullClark;
        break;
    case Method::eLoop:
        m_subdivisionMethod = SubdivisionMethod::Loop;
        break;
    }

    return { true };
}


} // namespace omni::scene::optimizer
