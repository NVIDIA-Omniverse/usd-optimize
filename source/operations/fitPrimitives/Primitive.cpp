// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "Primitive.h"

// Carbonite
#include <carb/profiler/Profile.h>

// Scene Optimizer Core
#include "primitivesToMeshes/PrimitiveToMeshProcessedData.h"

#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMesh
#include <OmniMeshOps/Primitive.h>
#include <OmniMeshOps/ScopedCudaContext.h>
#include <OmniMeshOps/usd/Mesh.h>
#include <OmniMeshOps/usd/Prim.h>

// USD
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::PrimitiveFitOperation);


namespace omni::scene::optimizer
{

inline static bool primHasNonconstPrimvars(const UsdPrim& prim,
                                           const std::vector<TfToken>& allow = std::vector<TfToken>())
{
    UsdGeomPrimvarsAPI primvarsAPI(prim);
    if (primvarsAPI)
    {
        const auto& primvars = primvarsAPI.GetPrimvarsWithAuthoredValues();
        for (const auto& primvar : primvars)
        {
            // Look for surface-varying primvars that aren't in the allow list
            if (primvar.GetInterpolation() != UsdGeomTokens->constant &&
                std::find(allow.begin(), allow.end(), primvar.GetAttr().GetRoleName()) == allow.end())
            {
                return true;
            }
        }
    }

    return false;
}

/// Override ProcessedHostMesh's writeToUsd function to preserve abstractness / undefinedness of prim
struct PrimitiveFitProcessedData : public ProcessedHostPrim
{
    PrimitiveFitProcessedData(const omo::usd::HostPrim& hostPrim, const PXR_NS::UsdPrim& usdPrim, bool write = true)
        : ProcessedHostPrim(hostPrim, usdPrim, write)
    {
    }

    void writeToUsd(const std::string& primPath, const PXR_NS::UsdStageWeakPtr& stage) override
    {
        UsdPrim prim = stage->GetPrimAtPath(SdfPath(primPath));
        if (!prim)
        {
            return;
        }

        // If the prim is undefined, find the highest undefined ancestor
        UsdPrim undefinedAncestor;
        if (!prim.IsDefined())
        {
            undefinedAncestor = prim;
            UsdPrim parent;
            while (parent = undefinedAncestor.GetParent(), parent && !parent.IsDefined())
            {
                undefinedAncestor = parent;
            }
        }

        // Call base class write
        ProcessedHostPrim::writeToUsd(primPath, stage);

        // Restore non-definition of ancestor
        if (undefinedAncestor)
        {
            undefinedAncestor.SetSpecifier(SdfSpecifierOver);
        }
    }
};

/// Constants
constexpr const char* s_categoryFitPrimitives = "FIT_PRIMITIVES";

PrimitiveFitOperation::PrimitiveFitOperation()
    : OmniOperation("fitPrimitives", "Fit Primitives", "This operation fits primitives to meshes.")
    , m_gpuFaceCountThreshold(0)
    , m_showFittingParameters(true)
    , m_vertexTolerance(0.01f)
    , m_volumeTolerance(0.01f)
    , m_ignoreNonConstPrimvars(true)
    , m_ignoreSubsets(true)
    , m_allowNegativeVolume(true)
    , m_allowMissingEndcaps(true)
    , m_fitSphere(true)
    , m_fitCylinder(true)
    , m_fitCone(true)
    , m_fitCube(true)
    , m_generateMeshes(false)
{
    addArgument("paths", "Meshes to fit", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("gpuFaceCountThreshold",
                "GPU face count threshold",
                kDisplayTypeInt,
                "For meshes with at least this many faces, use GPU algorithm.  A value of zero forces CPU.",
                m_gpuFaceCountThreshold)
        .setMin(0);

    addArgument("showFittingParameters",
                "Show fitting parameters",
                kDisplayTypeBool,
                "Tolerances and filters used to determine quality of fit.",
                m_showFittingParameters);

    addGroup(
        "fittingParameters",
        addArgument("vertexTolerance",
                    "Vertex error tolerance",
                    kDisplayTypeFloat,
                    "Relative tolerance of RMS distance from fit vertices to primitive surface.",
                    m_vertexTolerance)
            .setMin(0.0f),

        addArgument("volumeTolerance",
                    "Volume error tolerance",
                    kDisplayTypeFloat,
                    "Relative tolerance of volume between faces and the fitting primitive.",
                    m_volumeTolerance)
            .setMin(0.0f),

        addArgument(
            "ignoreNonConstPrimvars",
            "Ignore non-const primvars",
            kDisplayTypeBool,
            "If set, a mesh with non-constant primvars is allowed to be fit.  If replaced by a primitive, any non-constant primvars will be lost.",
            m_ignoreNonConstPrimvars),

        addArgument(
            "ignoreSubsets",
            "Ignore subsets",
            kDisplayTypeBool,
            "If set, a mesh with subsets is allowed to be fit.  If replaced by a primitive, any subsets will be lost.",
            m_ignoreSubsets),

        addArgument("allowNegativeVolume",
                    "Allow negative volume",
                    kDisplayTypeBool,
                    "If set, a mesh with negative volume (inward-pointing normals) is allowed to be fit.",
                    m_allowNegativeVolume),

        addArgument("allowMissingEndcaps",
                    "Allow missing endcaps",
                    kDisplayTypeBool,
                    "If set, a cylinder, cone, or box mesh without endcaps is allowed to be fit.",
                    m_allowMissingEndcaps),

        addArgument("fitSphere",
                    "Fit sphere",
                    kDisplayTypeBool,
                    "Attempt to fit a transformed sphere to selected meshes",
                    m_fitSphere),

        addArgument("fitCylinder",
                    "Fit cylinder",
                    kDisplayTypeBool,
                    "Attempt to fit a transformed cylinder to selected meshes",
                    m_fitCylinder),

        addArgument("fitCone", "Fit cone", kDisplayTypeBool, "Attempt to fit a transformed cone to selected meshes", m_fitCone),

        addArgument("fitCube",
                    "Fit cube",
                    kDisplayTypeBool,
                    "Attempt to fit a transformed cube to selected meshes",
                    m_fitCube))
        .setVisibleIf("showFittingParameters"); // group fitting parameters

    addArgument("generateMeshes",
                "Generate meshes",
                kDisplayTypeBool,
                "If set, a mesh will be generated instead of a primitive shape.",
                m_generateMeshes);

    addGroup("meshGenerationParameters",
             addArgument("sphereLongitudeDivisions",
                         "Sphere longitude divisions",
                         kDisplayTypeInt,
                         "The number of longitudinal divisions in which to divide spheres.  Must be at least 3.",
                         m_meshParameters.sphereParameters.n_radial)
                 .setMin(3),

             addArgument("sphereLatitudeDivisions",
                         "Sphere latitude divisions",
                         kDisplayTypeInt,
                         "The number of latitudinal divisions in which to divide spheres.  Must be at least 2.",
                         m_meshParameters.sphereParameters.n_axial)
                 .setMin(2),

             addArgument("cylinderWallDivisions",
                         "Cylinder wall divisions",
                         kDisplayTypeInt,
                         "The number of divisions to make around the cylinder wall.  Must be at least 3.",
                         m_meshParameters.cylinderParameters.n_radial)
                 .setMin(3),

             addArgument("cylinderLatitudeDivisions",
                         "Cylinder length divisions",
                         kDisplayTypeInt,
                         "The number of end-to-end divisions to make along the cylinder.  Must be positive.",
                         m_meshParameters.cylinderParameters.n_axial)
                 .setMin(1),

             addArgument("cylinderEndcaps",
                         "Generate cylinder endcaps",
                         kDisplayTypeBool,
                         "Whether or not to add endcaps to generated cylinder meshes.",
                         m_meshParameters.cylinderParameters.capped),

             addArgument("coneSideDivisions",
                         "Cone side divisions",
                         kDisplayTypeInt,
                         "The number of divisions to make around the side of the cone.  Must be at least 3.",
                         m_meshParameters.coneParameters.n_radial)
                 .setMin(3),

             addArgument("coneLengthDivisions",
                         "Cone length divisions",
                         kDisplayTypeInt,
                         "The number of divisions to make along the length of the cone.  Must be positive.",
                         m_meshParameters.coneParameters.n_axial)
                 .setMin(1),

             addArgument("coneBases",
                         "Generate cone bases",
                         kDisplayTypeBool,
                         "Whether or not to add a base to generated cone meshes.",
                         m_meshParameters.coneParameters.capped))
        .setVisibleIf("generateMeshes"); // group mesh generation parameters
}

std::string PrimitiveFitOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}

SOPluginVersion PrimitiveFitOperation::getVersion() const
{
    return { 1, 2, 0 };
}

std::string PrimitiveFitOperation::getCategory() const
{
    return s_categoryFitPrimitives;
}

std::string PrimitiveFitOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}

bool PrimitiveFitOperation::getSupportsAnalysis() const
{
    return true;
}

ProcessedData* PrimitiveFitOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{
    using namespace omo;

    ProcessedData* result = nullptr;

    ++m_meshReport.totalMeshCount;

    // skip if prim has references
    UsdPrimCompositionQuery::Filter filter;
    filter.arcTypeFilter = UsdPrimCompositionQuery::ArcTypeFilter::Reference;
    if (UsdPrimCompositionQuery(prim, filter).GetCompositionArcs().size())
    {
        ++m_meshReport.composedCount;
        return new ProcessedData(prim, false);
    }

    UsdGeomMesh usdMesh(prim);
    const std::string primPath = prim.GetPath().GetAsString();

    // skip if the prim has subsets
    std::vector<UsdGeomSubset> subsets = UsdGeomSubset::GetGeomSubsets(usdMesh);
    if (!subsets.empty())
    {
        if (!m_ignoreSubsets)
        {
            return new ProcessedData(prim, false);
        }

        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE(
                "Prim: %s has subsets, but will be considered for GPrim replacement anyway "
                "because \"Ignore subsets\" is set.",
                primPath.c_str());
        }
    }

    // Not worrying about Normal primvars for now, assuming these are used for smoothing that a prim will provide
    static const std::vector<TfToken> allow = { SdfValueRoleNames->Normal };

    // skip if the prim has non-const primvars, unless it's allowed)
    const bool hasNonconstPrimvars = primHasNonconstPrimvars(prim, allow);
    if (hasNonconstPrimvars && !getContext()->analysisMode)
    {
        if (!m_ignoreNonConstPrimvars)
        {
            return new ProcessedData(prim, false);
        }

        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE(
                "Prim: %s has non-const primvars, but will be considered for GPrim replacement anyway "
                "because \"Ignore non-const primvars\" is set.",
                primPath.c_str());
        }
    }

    omo::usd::HostMesh inputMesh{ usdMesh };

    // skip empty meshes
    if (inputMesh.vertexCount() == 0)
    {
        return new ProcessedData(prim, false);
    }

    auto use_gpu_fitting =
        m_gpuFaceCountThreshold > 0 && inputMesh.faceCount() >= m_gpuFaceCountThreshold && isCudaAvailable();

    const PrimitiveUpAxis upAxis = omo::usd::HostPrim::convertUpAxis(UsdGeomGetStageUpAxis(getUsdStage()));

    if (!getContext()->analysisMode)
    {
        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE("Prim: %s\nUse %s, face count: %zu",
                           primPath.c_str(),
                           (use_gpu_fitting ? "GPU" : "CPU"),
                           inputMesh.faceCount());
        }
    }

    PrimitiveType::Enum primType = PrimitiveType::None;

    const bool write = !getContext()->analysisMode;

    if (!use_gpu_fitting)
    {
        HostPrimitiveFit fit{ inputMesh };
        {
            auto fitPrim =
                fit(upAxis, m_shapeMask, m_volumeTolerance, m_vertexTolerance, m_allowNegativeVolume, m_allowMissingEndcaps);
            primType = fitPrim.type;
            if (primType != omo::PrimitiveType::None)
            {
                if (!m_generateMeshes)
                {
                    result = new PrimitiveFitProcessedData(fitPrim, prim, write);
                }
                else
                {
                    result = new PrimitiveToMeshProcessedData(prim,
                                                              fitPrim,
                                                              upAxis,
                                                              m_meshParameters.forType(fitPrim.type),
                                                              &m_hashCache,
                                                              write,
                                                              inputMesh);
                }
            }
        }
    }
    else
    {
        ScopedCudaContext cuda_context(omo::Verbose{ getContext()->verbose > 0 });
        DevicePrimitiveFit fit{ DeviceMesh{ inputMesh } };
        omo::usd::HostPrim fitPrim(
            fit(upAxis, m_shapeMask, m_volumeTolerance, m_vertexTolerance, m_allowNegativeVolume, m_allowMissingEndcaps));
        primType = fitPrim.type;
        if (primType != omo::PrimitiveType::None)
        {
            if (!m_generateMeshes)
            {
                result = new PrimitiveFitProcessedData(fitPrim, prim, write);
            }
            else
            {
                result = new PrimitiveToMeshProcessedData(prim,
                                                          fitPrim,
                                                          upAxis,
                                                          m_meshParameters.forType(fitPrim.type),
                                                          &m_hashCache,
                                                          write,
                                                          inputMesh);
            }
        }
    }

    if (!result)
    {
        result = new ProcessedData(prim, false); // If the mesh wasn't fit, don't write
    }

    if (!getContext()->analysisMode)
    {
        if (primType != PrimitiveType::None)
        {
            static const char* prim_names[] = { "sphere", "cylinder", "cone", "cube" };
            static_assert(std::size(prim_names) == omo::PrimitiveType::Count,
                          "prim_names array must cover every PrimitiveType");
            if (getContext()->verbose)
            {
                SO_LOG_VERBOSE("Prim: %s\n[%s] %zu faces -> %s",
                               primPath.c_str(),
                               (use_gpu_fitting ? "GPU" : "CPU"),
                               inputMesh.faceCount(),
                               prim_names[primType]);
            }

            ++m_meshReport.fitMeshCount[primType];
            m_meshReport.fitFaceCount[primType] += inputMesh.faceCount();
            m_meshReport.fitVertexCount[primType] += inputMesh.vertexCount();
        }
        else
        {
            if (getContext()->verbose)
            {
                SO_LOG_VERBOSE("Prim: %s\n[%s] %zu faces, not replaced",
                               primPath.c_str(),
                               (use_gpu_fitting ? "GPU" : "CPU"),
                               inputMesh.faceCount());
            }
        }
    }
    else
    {
        if (primType != PrimitiveType::None)
        {
            if (!hasNonconstPrimvars || m_ignoreNonConstPrimvars)
            {
                ++m_meshReport.fitMeshCount[primType];
                m_meshReport.fitFaceCount[primType] += inputMesh.faceCount();
                m_meshReport.fitVertexCount[primType] += inputMesh.vertexCount();
            }
            else
            {
                ++m_meshReport.fitNonConstPrimvarMeshCount[primType];
                m_meshReport.fitNonConstPrimvarFaceCount[primType] += inputMesh.faceCount();
                m_meshReport.fitNonConstPrimvarVertexCount[primType] += inputMesh.vertexCount();
            }
        }
    }

    return result;
}

OperationResult PrimitiveFitOperation::executePre()
{
    using namespace omo;

    // Create shape mask once
    m_shapeMask = 0;
    if (m_fitSphere)
    {
        m_shapeMask |= int(PrimitiveTypeMask::Sphere);
    }
    if (m_fitCylinder)
    {
        m_shapeMask |= int(PrimitiveTypeMask::Cylinder);
    }
    if (m_fitCone)
    {
        m_shapeMask |= int(PrimitiveTypeMask::Cone);
    }
    if (m_fitCube)
    {
        m_shapeMask |= int(PrimitiveTypeMask::Cube);
    }
    return { true };
}

OperationResult PrimitiveFitOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|PrimitiveFitOperation|PerformAnalysis");

    m_meshReport.clear();

    OmniOperation::executeImpl(); // Execute to pull in scene stats

    return recordAnalysis();
}

void PrimitiveFitOperation::executePost(const TotalStats& totalStats)
{
    if (!getContext()->analysisMode)
    {
        removeUnusedAttributes();

        OmniOperation::executePost(totalStats);

        const size_t replacedMeshCount =
            std::accumulate(m_meshReport.fitMeshCount, m_meshReport.fitMeshCount + omo::PrimitiveType::Count, 0);

        const char* replacementKind = m_generateMeshes ? "regularized meshes" : "primitives";

        if (replacedMeshCount == 0)
        {
            SO_LOG_INFO("Replaced 0 meshes with %s.", replacementKind);
        }
        else
        {
            std::string breakdown;
            for (int type = 0; type < omo::PrimitiveType::Count; ++type)
            {
                const size_t count = m_meshReport.fitMeshCount[type];
                if (count > 0)
                {
                    if (!breakdown.empty())
                    {
                        breakdown += ", ";
                    }
                    breakdown += std::to_string(count) + " " + omo::PrimitiveType::name(type).data();
                    if (count != 1)
                    {
                        breakdown += "s";
                    }
                }
            }
            SO_LOG_INFO("Replaced %zu meshes with %s (%s).", replacedMeshCount, replacementKind, breakdown.c_str());
        }

        return;
    }

    // Analysis mode, just collect the stats
    m_meshReport.totalFaceCount = totalStats.before.faceCount;
    m_meshReport.totalVertexCount = totalStats.before.vertexCount;
}

void PrimitiveFitOperation::removeUnusedAttributes()
{
    std::vector<UsdPrim> primsToProcess;
    for (const auto& prim : getUsdStage()->TraverseAll())
    {
        if (prim.IsA<UsdGeomGprim>() && !prim.IsA<UsdGeomMesh>())
        {
            primsToProcess.push_back(prim);
        }
    }

    // Get root layer and use SdfPrimSpecs to remove unused attributes
    for (auto& prim : primsToProcess)
    {
        omo::usd::HostPrim::removeMeshSpecificAttributesAndPrimvarsFromGprim(prim.GetPath().GetString(), getUsdStage());
    }
}

OperationResult PrimitiveFitOperation::recordAnalysis()
{
    // Construct analysis result
    JsObject analysisResult;
    analysisResult["totalMeshCount"] = JsValue(m_meshReport.totalMeshCount);
    analysisResult["totalFaceCount"] = JsValue(m_meshReport.totalFaceCount);
    analysisResult["totalVertexCount"] = JsValue(m_meshReport.totalVertexCount);
    analysisResult["composedCount"] = JsValue(m_meshReport.composedCount);

    JsObject primitiveFits;
    JsObject fitStats[omo::PrimitiveType::Count];
    for (int type = 0; type < omo::PrimitiveType::Count; ++type)
    {
        fitStats[type]["meshCount"] = JsValue(m_meshReport.fitMeshCount[type]);
        fitStats[type]["faceCount"] = JsValue(m_meshReport.fitFaceCount[type]);
        fitStats[type]["vertexCount"] = JsValue(m_meshReport.fitVertexCount[type]);
        fitStats[type]["nonconstPrimvarMeshCount"] = JsValue(m_meshReport.fitNonConstPrimvarMeshCount[type]);
        fitStats[type]["nonconstPrimvarFaceCount"] = JsValue(m_meshReport.fitNonConstPrimvarFaceCount[type]);
        fitStats[type]["nonconstPrimvarVertexCount"] = JsValue(m_meshReport.fitNonConstPrimvarVertexCount[type]);
        primitiveFits[omo::PrimitiveType::name(type).data()] = fitStats[type];
    }
    analysisResult["primitives"] = primitiveFits;

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    return result;
}

} // namespace omni::scene::optimizer
