// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "GenerateNormals.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMeshOps
#include <OmniMeshOps/Normals.h>
#include <OmniMeshOps/usd/Attribute.h>
#include <OmniMeshOps/usd/Mesh.h>

// Carbonite
#include <carb/profiler/Profile.h>

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((primvarsNormals, "primvars:normals"))
);
// LCOV_EXCL_STOP
// clang-format on


SO_PLUGIN_INIT(omni::scene::optimizer::GenerateNormalsOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categoryGenerateNormals = "NORMALS";

GenerateNormalsOperation::GenerateNormalsOperation()
    : OmniOperation("generateNormals", "Generate Normals", "This operation generates normals for meshes.")
    , m_existingNormals(ExistingNormals::eFix)
    , m_sharpnessAngle(60.0f)
    , m_binding(Binding::eAuto)
    , m_weightMode(WeightMode::eAngle)
    , m_gpuThreshold(500000)
{

    addArgument("paths", "Meshes To Process", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("binding", "Add Normals to", kDisplayTypeEnum, "Type of normals to generate", m_binding)
        .setEnumValues<Binding>({ { Binding::eAuto, "Auto" },
                                  { Binding::ePerCorner, "Corners" },
                                  { Binding::ePerVertex, "Vertices" },
                                  { Binding::ePerFace, "Faces" } });

    addArgument("existingNormals", "Existing Normals", kDisplayTypeEnum, "What to do with existing normals", m_existingNormals)
        .setEnumValues<ExistingNormals>({ { ExistingNormals::eFix, "Fix" }, { ExistingNormals::eReplace, "Replace" } });

    addArgument("sharpnessAngle",
                "Sharpness Angle",
                kDisplayTypeFloat,
                "The absolute value of the dihedral angle at an edge above which the edge is considered sharp. "
                "The dihedral angle is measured in the range ]-180, 180] degrees with 0 being flat.",
                m_sharpnessAngle)
        .setMin(-180.0f)
        .setMax(180.0f)
        .setVisibleIf("binding == 3 or binding == 0")
        .setEnableIf("binding == 3 or binding == 0");

    addArgument("weightmode",
                "Weighting",
                kDisplayTypeEnum,
                "Weight each contribution to the final normal by angle or face area",
                m_weightMode)
        .setVisibleIf("binding != 1")
        .setEnableIf("binding != 1")
        .setEnumValues<WeightMode>({ { WeightMode::eAngle, "Angle" }, { WeightMode::eArea, "Area" } });

    addArgument("gpuThreshold",
                "GPU Threshold",
                kDisplayTypeIntSlider,
                "Use GPU algorithm if number of normals to generate is greater than this value",
                m_gpuThreshold)
        .setMin(0);
}


std::string GenerateNormalsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion GenerateNormalsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string GenerateNormalsOperation::getCategory() const
{
    return s_categoryGenerateNormals;
}

bool GenerateNormalsOperation::getSupportsAnalysis() const
{
    return true;
}


ProcessedData* GenerateNormalsOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{
    using namespace omo::usd;

    ProcessedData* result = nullptr;
    UsdGeomMesh usd_mesh(prim);

    try
    {
        // if usd mesh previously used normals, use that, if not, prefer primvars:normals
        bool had_normals = false;
        std::string_view normals_attr_name = "primvars:normals";
        if (!prim.GetAttribute(_tokens->primvarsNormals).IsValid())
        {
            UsdAttribute normals_attr = usd_mesh.GetNormalsAttr();
            if (normals_attr.IsValid() && normals_attr.HasAuthoredValue())
            {
                normals_attr_name = "normals";
                had_normals = true;
            }
        }
        else
        {
            had_normals = true;
        }

        if (getContext()->analysisMode)
        {
            try
            {
                HostMeshData mesh_data(usd_mesh);
                auto normals_attr = mesh_data.getAttribute(normals_attr_name);
                double tolerance = 1e-4;
                if (auto [res, message] =
                        omo::checkNormalsUnitLength(normals_attr, tolerance, omo::CheckNormalsOptions::Strict);
                    !res)
                {
                    SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                    ++m_report.totalNonUnitLengthStrict;
                }
            }
            catch (const std::exception&)
            {
                // silently ignore any bad normal attribute value counts,
                // possibly unsupported attribute bindings and/or value types.
            }

            return new ProcessedHostMeshData{ {}, prim, false /* don't write */ };
        }

        HostMesh host_mesh(usd_mesh);

        bool useGpu =
            ((m_binding == Binding::eAuto || m_binding == Binding::ePerCorner ?
                  host_mesh.cornerCount() :
                  (m_binding == Binding::ePerFace ? host_mesh.faceCount() : host_mesh.vertexCount())) > m_gpuThreshold) &&
            isCudaAvailable();

        auto fix_existing = false;
        auto effective_binding = m_binding;

        if (m_existingNormals == ExistingNormals::eFix && had_normals)
        {
            fix_existing = true;
            auto old_binding = host_mesh.getAttribute(normals_attr_name).binding();

            if (m_binding != Binding::eAuto &&
                ((old_binding == omo::Binding::PerCorner && m_binding != Binding::ePerCorner) ||
                 (old_binding == omo::Binding::PerVertex && m_binding != Binding::ePerVertex) ||
                 (old_binding == omo::Binding::PerFace && m_binding != Binding::ePerFace)))
            {
                // We are asked to fix an existing normal attribute whose binding explicitly conflicts the choice
                // of output normal. So, we'll replace it!
                fix_existing = false;
            }

            if (effective_binding == Binding::eAuto)
            {
                // We have an existing attribute to be fixed and no explicit choice of target binding. So, we set
                // the effective binding choice to the one implied by the existing attribute.
                switch (old_binding)
                {
                case omo::Binding::PerCorner:
                    effective_binding = Binding::ePerCorner;
                    break;
                case omo::Binding::PerVertex:
                    effective_binding = Binding::ePerVertex;
                    break;
                case omo::Binding::PerFace:
                    effective_binding = Binding::ePerFace;
                    break;
                default:
                    break;
                }
            }
        }

        if (useGpu)
        {
            auto device_mesh = omo::DeviceMesh(host_mesh);
            auto old_normals = device_mesh.getAttribute(normals_attr_name);

            omo::DeviceAttribute device_attr;
            switch (effective_binding)
            {
            case Binding::ePerCorner:
                device_attr =
                    fix_existing ?
                        omo::cornerNormals(device_mesh, old_normals, m_sharpnessAngle, omo::WeightMode(m_weightMode)) :
                        omo::cornerNormals(device_mesh, m_sharpnessAngle, omo::WeightMode(m_weightMode));
                break;
            case Binding::ePerFace:
                device_attr = fix_existing ? omo::faceNormals(device_mesh, old_normals) : omo::faceNormals(device_mesh);
                break;
            case Binding::ePerVertex:
                device_attr = fix_existing ? omo::vertexNormals(device_mesh, old_normals, omo::WeightMode(m_weightMode)) :
                                             omo::vertexNormals(device_mesh, omo::WeightMode(m_weightMode));
                break;
            case Binding::eAuto:
                device_attr = omo::normals(device_mesh, m_sharpnessAngle, omo::WeightMode(m_weightMode));
                break;
            default:
                break;
            }

            if (device_attr.name() != normals_attr_name)
            {
                device_attr.setName(normals_attr_name);
            }

            device_mesh.attachAttribute(device_attr, true /* replace existing*/);

            host_mesh = HostMesh(device_mesh);

            result = new ProcessedHostMesh(host_mesh, prim, true);
        }
        else
        {
            auto old_normals = host_mesh.getAttribute(normals_attr_name);
            HostAttribute host_attr;

            switch (effective_binding)
            {
            case Binding::ePerCorner:
                host_attr =
                    fix_existing ?
                        omo::cornerNormals(host_mesh, old_normals, m_sharpnessAngle, omo::WeightMode(m_weightMode)) :
                        omo::cornerNormals(host_mesh, m_sharpnessAngle, omo::WeightMode(m_weightMode));
                break;
            case Binding::ePerFace:
                host_attr = fix_existing ? omo::faceNormals(host_mesh, old_normals) : omo::faceNormals(host_mesh);
                break;
            case Binding::ePerVertex:
                host_attr = fix_existing ? omo::vertexNormals(host_mesh, old_normals, omo::WeightMode(m_weightMode)) :
                                           omo::vertexNormals(host_mesh, omo::WeightMode(m_weightMode));
                break;
            case Binding::eAuto:
                host_attr = omo::normals(host_mesh, m_sharpnessAngle, omo::WeightMode(m_weightMode));
                break;
            default:
                break;
            }

            if (host_attr.name() != normals_attr_name)
            {
                host_attr.setName(normals_attr_name);
            }

            if (!host_mesh.attachAttribute(host_attr, true /* replace existing*/))
            {
                throw std::runtime_error("Failed to attach new normals, " + prim.GetPath().GetAsString());
            }

            result = new ProcessedHostMesh(host_mesh, prim, true);
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        SO_LOG_ERROR(errorMsg.c_str());
        result = new ProcessedHostMesh{ {}, prim, false /* don't write, leave mesh unchanged */ };
    }

    return result;
}

OperationResult GenerateNormalsOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|GenerateNormals|Analysis");

    m_report.clear();

    OmniOperation::executeImpl(); // Execute to pull in scene stats

    return recordAnalysis();
}

OperationResult GenerateNormalsOperation::recordAnalysis()
{
    // Construct analysis result
    JsObject analysis_result;
    analysis_result["totalNonUnitLengthStrict"] = JsValue(m_report.totalNonUnitLengthStrict);

    JsObject resultJson;
    resultJson["analysis"] = analysis_result;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    return result;
}

void GenerateNormalsOperation::executePost(const TotalStats& totalStats)
{
}


} // namespace omni::scene::optimizer
