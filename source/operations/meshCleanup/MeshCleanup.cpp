// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "MeshCleanup.h"

// OmniMeshOps
#include <OmniMeshOps/Coorient.h>
#include <OmniMeshOps/Manifold.h>
#include <OmniMeshOps/Reverse.h>
#include <OmniMeshOps/ValidateMeshData.h>
#include <OmniMeshOps/usd/Mesh.h>
#include <OmniMeshOps/usd/MeshData.h>

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::MeshCleanupOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categoryMeshCleanup = "MESHCLEANUP";

MeshCleanupOperation::MeshCleanupOperation()
    : OmniOperation("meshCleanup",
                    "Mesh Cleanup",
                    "This operation applies various cleanups to a mesh, e.g merge vertices that are closer to one "
                    "another than a given tolerance, removing degenerate faces, making the resulting mesh be manifold "
                    "and/or removing any isolated vertices.")
    , m_mergeVertices(true)
    , m_tolerance(0)
    , m_makeManifold(false)
    , m_removeIsolatedVertices(true)
    , m_mergeBoundaries(true)
    , m_mergeNeighbors(true)
    , m_contractDegenerateEdges(true)
    , m_removeDegenerateFaces(true)
    , m_removeDuplicateFaces(true)
    , m_coorientFaces(false)
{

    addArgument("paths", "Meshes To Process", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("mergeVertices", "Merge Vertices", kDisplayTypeBool, "Merge vertices", m_mergeVertices);

    addArgument("tolerance",
                "Tolerance",
                kDisplayTypeFloatSlider,
                "The tolerance (distance) apart for vertices to be considered equal",
                m_tolerance)
        .setMin(0)
        .setVisibleIf("mergeVertices == True");

    addArgument("mergeBoundaries", "Merge Boundaries", kDisplayTypeBool, "Merge coincident boundary vertices", m_mergeBoundaries)
        .setVisibleIf("mergeVertices == True");

    addArgument("mergeNeighbors",
                "Merge Neighbors",
                kDisplayTypeBool,
                "Merge coincident vertices that are neighbors around some face",
                m_mergeNeighbors)
        .setVisibleIf("mergeVertices == True");

    addArgument("contractDegenerateEdges",
                "Contract degenerate edges",
                kDisplayTypeBool,
                "Merge consecutively repeated vertex references around faces",
                m_contractDegenerateEdges);

    addArgument("removeDegenerateFaces",
                "Remove degenerate faces",
                kDisplayTypeBool,
                "Remove faces with fewer than 3 distinct vertex references",
                m_removeDegenerateFaces);

    addArgument("removeIsolatedVertices",
                "Remove isolated vertices",
                kDisplayTypeBool,
                "Remove isolated vertices",
                m_removeIsolatedVertices);

    addArgument("removeDuplicateFaces",
                "Remove duplicate (lamina) faces",
                kDisplayTypeBool,
                "Remove duplicate (lamina) faces",
                m_removeDuplicateFaces);

    addArgument(
        "coorientFaces",
        "Coorient Faces",
        kDisplayTypeBool,
        "Reverses the winding of a minority of faces to enforce consistent (manifold) orientation at all edges shared by two faces",
        m_coorientFaces);

    addArgument("makeManifold",
                "Make Manifold",
                kDisplayTypeBool,
                "Ensure the final result is a manifold mesh",
                m_makeManifold);
}


std::string MeshCleanupOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion MeshCleanupOperation::getVersion() const
{
    return { 2, 0, 0 };
}


std::string MeshCleanupOperation::getCategory() const
{
    return s_categoryMeshCleanup;
}


std::string MeshCleanupOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


bool MeshCleanupOperation::getSupportsAnalysis() const
{
    return true;
}


ProcessedData* MeshCleanupOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{
    using namespace omo::usd;
    UsdGeomMesh usd_mesh(prim);

    try
    {
        if (getContext()->analysisMode)
        {
            HostMeshData mesh_data(usd_mesh);
            if (auto [res, message] = omo::checkManifold(mesh_data); !res)
            {
                SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesThatAreNonManifolds++;
            }
            if (auto [res, message] =
                    omo::checkNoMergeableCoincidentVertices(mesh_data,
                                                            m_tolerance,
                                                            omo::meshdata::MergeNeighbors{ m_mergeNeighbors },
                                                            omo::meshdata::MergeBoundaries{ m_mergeBoundaries });
                !res)
            {
                SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesWithMergeableVertices++;
            }
            if (auto [res, message] = omo::checkNoDegenerateEdges(mesh_data); !res)
            {
                SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesWithDegenerateEdges++;
            }
            if (auto [res, message] = omo::checkNoDegenerateFaces(mesh_data); !res)
            {
                SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesWithDegenerateFaces++;
            }
            if (auto [res, message] = omo::checkNoIsolatedVertices(mesh_data); !res)
            {
                SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesWithIsolatedVertices++;
            }
            if (auto [res, message] = omo::checkNoDuplicateFaces(mesh_data); !res)
            {
                SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesWithDuplicateFaces++;
            }

            auto normals_attr = mesh_data.getAttribute(omo::Role::Normal);
            auto usd_orientation_attr = usd_mesh.GetOrientationAttr();
            PXR_NS::TfToken usd_orientation = PXR_NS::UsdGeomTokens->rightHanded;
            usd_orientation_attr.Get(&usd_orientation);
            if (auto [res, message] = omo::checkNormalsConsistentWithWinding(
                    mesh_data,
                    normals_attr,
                    usd_orientation == PXR_NS::UsdGeomTokens->rightHanded ? omo::Orientation::RightHanded :
                                                                            omo::Orientation::LeftHanded);
                !res)
            {
                SO_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesWithInconsistentWindings.push_back(prim);
            }

            return new ProcessedHostMeshData{ {}, prim, false /* don't write */ };
        }

        omo::MeshConstructionOptions mesh_options{ omo::Defect::None };

        if (m_contractDegenerateEdges)
        {
            mesh_options.fixes |= omo::Defect::DegenerateEdges;
        }

        if (m_removeDegenerateFaces)
        {
            mesh_options.fixes |= omo::Defect::DegenerateFaces;
        }

        if (m_mergeVertices)
        {
            mesh_options.fixes |= omo::Defect::CoincidentVertices;
            mesh_options.mergeVerticesTolerance = m_tolerance;
            mesh_options.mergeBoundaries = { m_mergeBoundaries };
            mesh_options.mergeNeighbors = { m_mergeNeighbors };
        }

        if (m_removeIsolatedVertices)
        {
            mesh_options.fixes |= omo::Defect::IsolatedVertices;
        }

        if (m_removeDuplicateFaces)
        {
            mesh_options.fixes |= omo::Defect::DuplicateFaces;
        }

        HostMeshData mesh_data{ usd_mesh, mesh_options };
        if (m_coorientFaces)
        {
            mesh_data = omo::coorient(mesh_data);

            auto normals_attr = mesh_data.getAttribute(omo::Role::Normal);
            auto usd_orientation_attr = usd_mesh.GetOrientationAttr();
            PXR_NS::TfToken usd_orientation = PXR_NS::UsdGeomTokens->rightHanded;
            usd_orientation_attr.Get(&usd_orientation);
            if (auto [res, message] = omo::checkNormalsConsistentWithWinding(
                    mesh_data,
                    normals_attr,
                    usd_orientation == PXR_NS::UsdGeomTokens->rightHanded ? omo::Orientation::RightHanded :
                                                                            omo::Orientation::LeftHanded);
                !res)
            {
                mesh_data = omo::reverse(mesh_data);
            }
        }

        HostMesh mesh(mesh_data);
        if (m_makeManifold)
        {
            mesh = manifold(mesh);
        }

        return new ProcessedHostMesh{ mesh, prim };
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        SO_LOG_ERROR(errorMsg.c_str());
        return new ProcessedHostMesh{ {}, prim, false /* don't write, leave mesh unchanged */ };
    }
}


OperationResult MeshCleanupOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|MeshCleanup|Analysis");

    m_report.clear();

    OmniOperation::executeImpl(); // Execute to pull in scene stats

    return recordAnalysis();
}

OperationResult MeshCleanupOperation::recordAnalysis()
{
    // Construct analysis result
    JsObject analysis_result;
    analysis_result["meshesThatAreNonManifolds"] = JsValue(m_report.meshesThatAreNonManifolds);
    analysis_result["meshesWithMergeableVertices"] = JsValue(m_report.meshesWithMergeableVertices);
    analysis_result["meshesWithDegenerateEdges"] = JsValue(m_report.meshesWithDegenerateEdges);
    analysis_result["meshesWithDegenerateFaces"] = JsValue(m_report.meshesWithDegenerateFaces);
    analysis_result["meshesWithIsolatedVertices"] = JsValue(m_report.meshesWithIsolatedVertices);
    analysis_result["meshesWithDuplicateFaces"] = JsValue(m_report.meshesWithDuplicateFaces);
    analysis_result["meshesWithInconsistentWindings"] = _toJson(m_report.meshesWithInconsistentWindings);

    JsObject resultJson;
    resultJson["analysis"] = analysis_result;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    return result;
}

} // namespace omni::scene::optimizer
