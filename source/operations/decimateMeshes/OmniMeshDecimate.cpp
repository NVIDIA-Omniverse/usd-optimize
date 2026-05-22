// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "OmniMeshDecimate.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMesh
#include <OmniMeshOps/Decimate.h>
#include <OmniMeshOps/ScopedCudaContext.h>
#include <OmniMeshOps/usd/Mesh.h>

// USD
#include <pxr/usd/usdGeom/mesh.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin with SO
SO_PLUGIN_INIT(omni::scene::optimizer::DecimateOperation);

namespace omni::scene::optimizer
{


/// Constants
constexpr const char* s_categoryDecimate = "DECIMATE";

DecimateOperation::DecimateOperation()
    : OmniOperation("decimateMeshes",
                    "Decimate Meshes",
                    "Reduce decimation amount on an input ``UsdGeom`` mesh primitive type.")
    , m_reductionFactor(50)
    , m_maxMeanError(0)
    , m_minFeatureArea(0)
    , m_featureSensitivity(1)
    , m_cpuVertexcountThreshold(100000)
    , m_gpuVertexcountThreshold(500000)
    , m_guideDecimation(DecimateGuideOption::eByNormals)
    , m_pinBoundaries(false)
    , m_allowCutAndGlue(false)
{


    addArgument("paths",
                "Meshes to Decimate",
                kDisplayTypePrimPaths,
                "Optional list of prim paths/expressions to decimate",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument(
        "reductionFactor",
        "Reduce to Percentage",
        kDisplayTypeFloatSlider,
        "Reduce to end result percentage from original vertex count, 0.0-100.0 values accepted. Set to 0 if using Maximum Mean Error.",
        m_reductionFactor)
        .setMin(0)
        .setMax(100);

    addArgument("maxMeanError",
                "Maximum Mean Error",
                kDisplayTypeFloatSlider,
                "Maximum mean error for the decimation, 0.0-100.0 values accepted. Set 0 to disable this option.",
                m_maxMeanError)
        .setMin(0)
        .setMax(10);

    addArgument("guideDecimation",
                "Guide Decimation",
                kDisplayTypeEnum,
                "Guide Decimation by using Normals or Colors (if available)",
                m_guideDecimation)
        .setEnumValues<DecimateGuideOption>({ { DecimateGuideOption::eByNormals, "By normals" },
                                              { DecimateGuideOption::eByColors, "By colors" },
                                              { DecimateGuideOption::eOff, "Off" } });

    addArgument("pinBoundaries", "Pin mesh boundaries", kDisplayTypeBool, "Preserve the mesh boundaries", m_pinBoundaries);

    addArgument("allowCutAndGlue",
                "Topology Simplification",
                kDisplayTypeBool,
                "Allow changes to mesh topology when decimating. Note that this will take more time",
                m_allowCutAndGlue);

    addJoin(
        "CPU/GPU Vertex Thresholds",
        "When the vertex count of a mesh is higher than the first value, a CPU parallel algorithm is used, and when higher than the second value a GPU algorithm is used",
        addArgument("cpuVertexCountThreshold",
                    "CPU Vertex Threshold",
                    kDisplayTypeIntSlider,
                    "Use CPU Parallel algorithm if vertex count is greater than this value",
                    m_cpuVertexcountThreshold)
            .setMin(0),

        addArgument("gpuVertexCountThreshold",
                    "GPU Vertex Threshold",
                    kDisplayTypeIntSlider,
                    "Use GPU algorithm if vertex count is greater than this value",
                    m_gpuVertexcountThreshold)
            .setMin(0));
}

DecimateOperation::~DecimateOperation() = default;


std::string DecimateOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion DecimateOperation::getVersion() const
{
    return { 1, 0, 0 };
}

std::string DecimateOperation::getCategory() const
{
    return s_categoryDecimate;
}


std::string DecimateOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


OperationResult DecimateOperation::executePre()
{
    // Convert to fraction and clamp from 0-1
    m_reductionFactor = std::clamp(m_reductionFactor / 100.0, 0.0, 1.0);

    // Ensure CPU threshold is less than or equal to GPU threshold
    if (m_cpuVertexcountThreshold > m_gpuVertexcountThreshold)
    {
        m_cpuVertexcountThreshold = m_gpuVertexcountThreshold;
        SO_LOG_INFO("CPU threshold must be lower or equal to the GPU threshold, overridden as %s",
                    std::to_string(m_cpuVertexcountThreshold).c_str());
    }

    return { true };
}


ProcessedData* DecimateOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{

    using namespace omo;

    try
    {
        UsdGeomMesh usdMesh(prim);

        VtVec3fArray points;
        usdMesh.GetPointsAttr().Get(&points);

        VtIntArray face_vertex_indices;
        usdMesh.GetFaceVertexIndicesAttr().Get(&face_vertex_indices);

        VtIntArray face_vertex_counts;
        usdMesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts);

        std::string msg;
        if (!usdMesh.ValidateTopology(face_vertex_indices.AsConst(), face_vertex_counts.AsConst(), points.size(), &msg))
        {
            SO_LOG_WARN("Prim: %s has invalid topology:\n %s", prim.GetPath().GetAsString().c_str(), msg.c_str())
            return nullptr;
        }

        omo::usd::HostMesh inputMesh{ usdMesh,
                                      { omo::Defect::CoincidentVertices | omo::Defect::DegenerateEdges |
                                        omo::Defect::DegenerateFaces } };

        // early out for empty meshes
        if (inputMesh.vertexCount() == 0)
        {
            return new ProcessedHostMesh(inputMesh, prim);
        }

        std::string guide_attribute;
        for (size_t i = 0; i < inputMesh.numAttributes(); i++)
        {
            const auto& attr = inputMesh.getAttribute(i);
            if (m_guideDecimation != DecimateGuideOption::eOff && guide_attribute.empty())
            {
                if (m_guideDecimation == DecimateGuideOption::eByNormals &&
                    ("primvars:normals" == attr.name() || "normals" == attr.name()))
                {
                    guide_attribute = "primvars:normals";
                }
                else if (m_guideDecimation == DecimateGuideOption::eByColors && "primvars:displayColor" == attr.name())
                {
                    guide_attribute = "primvars:displayColor";
                }
            }
        }

        auto use_gpu_decimator = inputMesh.vertexCount() > m_gpuVertexcountThreshold && isCudaAvailable();

        uint32_t stop_at_vertex_count = uint32_t(double(inputMesh.vertexCount()) * m_reductionFactor);

        std::string primMessage = "Prim: " + prim.GetPath().GetAsString() + "\n" + "Use " +
                                  (use_gpu_decimator ? "GPU" : "CPU") +
                                  ", stopVertexCount: " + std::to_string(stop_at_vertex_count) +
                                  (guide_attribute.empty() ? "" : ", guide: " + guide_attribute);


        SO_LOG_VERBOSE(primMessage.c_str());

        if (!use_gpu_decimator)
        {
            HostDecimate decimate{ inputMesh, guide_attribute };
            {
                bool run_parallel = !getContext()->singleThreaded && inputMesh.vertexCount() >= m_cpuVertexcountThreshold;

                auto decimated_mesh =
                    decimate(stop_at_vertex_count, m_maxMeanError, run_parallel, m_pinBoundaries, m_allowCutAndGlue);

                double outMeanError = decimate.meanError();
                auto result = new ProcessedHostMesh(decimated_mesh, prim);

                std::string cpuMessage = (run_parallel ? "[CPU-par] " : "[CPU-seq] ") + prim.GetName().GetString() +
                                         ": " + std::to_string(inputMesh.vertexCount()) + " -> " +
                                         std::to_string(result->vertexCount()) +
                                         " vertices, meanError: " + std::to_string(outMeanError);

                SO_LOG_VERBOSE(cpuMessage.c_str());

                return result;
            }
        }
        else
        {
            ScopedCudaContext cuda_context(omo::Verbose{ getContext()->verbose > 0 });

            DeviceMesh device_input_mesh{ inputMesh };

            // Matching clean-ups as above is applied by DeviceDecimator class.
            DeviceDecimate deviceDecimate{ device_input_mesh, guide_attribute };

            auto device_decimated_mesh = deviceDecimate(stop_at_vertex_count,
                                                        m_maxMeanError,
                                                        true /* parallel */,
                                                        m_pinBoundaries,
                                                        m_allowCutAndGlue);

            double outMeanError = deviceDecimate.meanError();

            HostMesh decimated_mesh(device_decimated_mesh);

            auto result = new ProcessedHostMesh(decimated_mesh, prim);

            std::string gpuMessage =
                "[GPU] " + prim.GetName().GetString() + ": " + std::to_string(inputMesh.vertexCount()) + " -> " +
                std::to_string(result->vertexCount()) + " vertices, meanError: " + std::to_string(outMeanError);

            SO_LOG_VERBOSE(gpuMessage.c_str());

            return result;
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        SO_LOG_ERROR(errorMsg.c_str());
    }

    return nullptr;
}


void DecimateOperation::executePost(const TotalStats& totalStats)
{
    float vDiff = float(totalStats.before.vertexCount) - float(totalStats.after.vertexCount);
    float fDiff = float(totalStats.before.faceCount) - float(totalStats.after.faceCount);
    float vertexReduction =
        vDiff == 0 ?
            0 :
            ((vDiff > 0 ? vDiff : float(totalStats.after.vertexCount)) / float(totalStats.before.vertexCount)) * 100.f;
    float faceReduction =
        fDiff == 0 ?
            0 :
            ((fDiff > 0 ? fDiff : float(totalStats.after.faceCount)) / float(totalStats.before.faceCount)) * 100.f;

    SO_LOG_INFO("VertexCount: %zu -> %zu (%f%%)",
                totalStats.before.vertexCount,
                totalStats.after.vertexCount,
                vertexReduction);

    SO_LOG_INFO("FaceCount: %zu -> %zu (%f%%)", totalStats.before.faceCount, totalStats.after.faceCount, faceReduction);
}


} // namespace omni::scene::optimizer
