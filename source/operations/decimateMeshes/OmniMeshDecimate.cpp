// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "OmniMeshDecimate.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/CudaUtils.h>
#include <usd_optimize/core/Utils.h>

// OmniMesh
#include <OmniMeshOps/Decimate.h>
#include <OmniMeshOps/ScopedCudaContext.h>
#include <OmniMeshOps/UsdIO.h>

// USD
#include <pxr/usd/usdGeom/mesh.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin with Usd Optimize
USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::DecimateOperation);

namespace usd_optimize
{


/// Constants
constexpr const char* s_categoryDecimate = "DECIMATE";

DecimateOperation::DecimateOperation()
    : OmniOperation("decimateMeshes", "Decimate Meshes", "Reduce tessellation density for mesh prims.")
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
        .setMax(100)
        // A percentage outside [0, 100] is meaningless; reject it rather than clamping (clamping 150
        // to 100 is a silent "reduce to 100%" no-op that still reports success).
        .setRejectOutOfRange(true);

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


std::string DecimateOperation::getDocumentation() const
{
    return R"DOC(Reduce the polygon count of ``UsdGeom.Mesh`` prims while preserving shape as much
as possible. Decimation uses QEM (Quadric Error Metrics) edge-collapse simplification: edges are collapsed
in order of least geometric error and the mesh is locally re-triangulated after each collapse. CPU
(parallel and sequential) and GPU paths are selected automatically based on vertex count thresholds.

Output mesh structure
---------------------

Each targeted mesh goes through three steps; the first two always run:

1. Colocated vertices are merged and degenerate faces are removed.
2. The mesh is triangulated.
3. If the ``reductionFactor`` or ``maxMeanError`` requires decimation, edges are collapsed until a stop
   condition is reached. ``reductionFactor`` applies to the vertex count after step 1.

As a result:

- Output is always triangles.
- Step 1 means that the vertex count can drop even if no decimation is performed.
- Step 2 doubles a quad mesh's face count, so face count can stay level or even rise as vertices fall.
- Flat or symmetric regions have many edges of equal cost, so small differences in input locations or
  numbering can change the result.

Choosing a stop condition
--------------------------

``reductionFactor`` and ``maxMeanError`` are the two stop conditions; either can be used alone or together
(whichever is reached first stops decimation of a given mesh). Set one to ``0.0`` to disable it.

- **Prefer an error bound for silhouette-preserving decimation.** ``maxMeanError`` bounds the geometric
  error, so the decimator stops before visible features are lost. This is the recommended default: set a
  non-zero ``maxMeanError`` and ``reductionFactor`` to ``0.0``.
- **Use a reduction rate only to hit a fixed target** (e.g. a memory budget or a fixed LOD level).
  ``reductionFactor`` is a percentage in the range 0-100, **not** a fraction: ``50`` keeps 50% of the
  vertices, while ``0.5`` keeps 0.5% and destroys the mesh. Values below 10 typically ruin the silhouette.

Use float literals for these float arguments; some bindings reject an integer ``0``.

Scale and units
---------------

``maxMeanError`` is the maximum mean geometric distance (in **stage units**) the decimated surface may
drift from the original. To target a physical tolerance, convert from millimetres using the stage's
``metersPerUnit``::

    maxMeanError = (tolerance_mm / 1000) / metersPerUnit

``UsdGeom.GetStageMetersPerUnit`` returns USD's default of ``0.01`` (centimetres) when the metadata is
unset, so a tolerance-sensitive config depends on the stage being authored at a known scale.

Important defaults and footguns
-------------------------------

- ``pinBoundaries`` defaults to ``false``. Set it to ``true`` explicitly whenever mesh outlines matter
  (architectural walls, tiles that must align along edges); otherwise boundary edges can collapse.
- ``guideDecimation`` lets a vertex-colour or corner-normal attribute steer which regions are simplified
  more aggressively. ``allowCutAndGlue`` permits topology changes for better quality at aggressive
  reduction.
- The GPU path requires CUDA and engages above ``gpuVertexCountThreshold`` (default 500K vertices); a CPU
  parallel path engages above ``cpuVertexCountThreshold`` (default 100K). Keep both defaults: the GPU pays
  off only on large meshes, and forcing small meshes onto it has caused intermittent aborts.

Decimation rewrites the vertex data of the targeted meshes. On stages with references, payloads, or
scenegraph instances this writes overrides on the composed stage while the source asset stays high-poly;
source-level optimization or proxy variants are often the better publishing path. Skinned (``UsdSkel``)
meshes bind joint weights to vertex order, so decimation invalidates those bindings unless skin weights
are regenerated.

Recommended pipelines
---------------------

Commonly paired as ``meshCleanup`` -> ``decimateMeshes`` (clean topology decimates more predictably), and
used for LOD generation or thinning meshes imported at excessive resolution.

Starting configurations
-----------------------

Silhouette-preserving (recommended default), error-budget driven:

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": true}]

Conservative (tighter error budget):

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.001, "pinBoundaries": true}]

Target reduction rate (keep 50% of vertices); disable the error cap:

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 50.0, "maxMeanError": 0.0, "pinBoundaries": true}]

Aggressive LOD (expect visible silhouette change; use only for small-screen LODs):

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 10.0, "maxMeanError": 0.0, "pinBoundaries": true, "allowCutAndGlue": true}]
)DOC";
}


std::string DecimateOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion DecimateOperation::getVersion() const
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
        USD_OPTIMIZE_LOG_INFO("CPU threshold must be lower or equal to the GPU threshold, overridden as %s",
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
            USD_OPTIMIZE_LOG_WARN("Prim: %s has invalid topology:\n %s", prim.GetPath().GetAsString().c_str(), msg.c_str())
            return nullptr;
        }

        // Non-finite (NaN/Inf) points, normals, or a non-finite world transform all resolve to
        // non-finite geometry, which makes the native decimator flood assertion failures and produce
        // garbage (or bake NaN into the output transform) while still reporting success. Skip such
        // meshes instead.
        if (!arePointsFinite(points))
        {
            USD_OPTIMIZE_LOG_WARN("Prim: %s has non-finite point coordinates; skipping.",
                                  prim.GetPath().GetAsString().c_str());
            return nullptr;
        }

        VtVec3fArray normals;
        if (usdMesh.GetNormalsAttr().Get(&normals) && !arePointsFinite(normals))
        {
            USD_OPTIMIZE_LOG_WARN("Prim: %s has non-finite normals; skipping.", prim.GetPath().GetAsString().c_str());
            return nullptr;
        }

        if (!isTransformFinite(usdMesh.ComputeLocalToWorldTransform(UsdTimeCode::Default())))
        {
            USD_OPTIMIZE_LOG_WARN("Prim: %s has a non-finite transform; skipping.", prim.GetPath().GetAsString().c_str());
            return nullptr;
        }

        auto inputMesh = omo::importMesh(usdMesh, { omo::manifestDefects + omo::Defect::CoincidentBoundaryVertices });

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

        std::string primMessage = "Prim: " + prim.GetPath().GetAsString() + " Use " +
                                  (use_gpu_decimator ? "GPU" : "CPU") +
                                  ", stopVertexCount: " + std::to_string(stop_at_vertex_count) +
                                  (guide_attribute.empty() ? "" : ", guide: " + guide_attribute);


        USD_OPTIMIZE_LOG_VERBOSE(primMessage.c_str());

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

                USD_OPTIMIZE_LOG_VERBOSE(cpuMessage.c_str());

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

            USD_OPTIMIZE_LOG_VERBOSE(gpuMessage.c_str());

            return result;
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        USD_OPTIMIZE_LOG_ERROR(errorMsg.c_str());
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

    USD_OPTIMIZE_LOG_INFO("VertexCount: %zu -> %zu (%f%%)",
                          totalStats.before.vertexCount,
                          totalStats.after.vertexCount,
                          vertexReduction);

    USD_OPTIMIZE_LOG_INFO("FaceCount: %zu -> %zu (%f%%)",
                          totalStats.before.faceCount,
                          totalStats.after.faceCount,
                          faceReduction);
}


} // namespace usd_optimize
