// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "MeshCleanup.h"

// OmniMeshOps
#include <OmniMeshOps/Cleanup.h>
#include <OmniMeshOps/Normals.h>
#include <OmniMeshOps/Reverse.h>
#include <OmniMeshOps/UsdIO.h>

// USD
#include <pxr/base/gf/range3f.h>

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/JsonUtils.h>
#include <usd_optimize/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

PXR_NAMESPACE_USING_DIRECTIVE

USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::MeshCleanupOperation);


namespace usd_optimize
{

/// Constants
constexpr const char* s_categoryMeshCleanup = "MESHCLEANUP";

MeshCleanupOperation::MeshCleanupOperation()
    : OmniOperation("meshCleanup", "Mesh Cleanup", "Applies various cleanups to meshes.")
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


std::string MeshCleanupOperation::getDocumentation() const
{
    return R"DOC(Applies various cleanups to a mesh: merge vertices that are closer to one another than a
given tolerance, remove degenerate faces, make the result manifold, and/or remove isolated vertices.

How the flags interact
----------------------

The passes are **not independent**, and this is the most common source of surprise. They run in a
fixed canonical order, each taking its input from the residual state the previous one left behind,
so **fixing one defect can create another**:

1. merge coincident neighbour vertices (``mergeVertices`` + ``mergeNeighbors``)
2. contract degenerate edges (``contractDegenerateEdges``)
3. remove degenerate faces (``removeDegenerateFaces``)
4. remove isolated vertices (``removeIsolatedVertices``)
5. merge coincident boundary vertices (``mergeVertices`` + ``mergeBoundaries``)
6. remove duplicate (lamina) faces (``removeDuplicateFaces``)
7. coorient faces (``coorientFaces``)
8. make manifold (``makeManifold``)

The order is fixed; you choose only which steps run. Merging coincident vertices *creates*
degenerate edges and faces -- a face collapses from three distinct vertex references to two when
two of its vertices merge -- and removing those degenerate faces then *strands* isolated vertices.
Merging boundary vertices can likewise produce duplicate, inconsistently oriented or non-manifold
faces.

**Prefer the defaults — running a subset is what leaves a mesh half-cleaned.** Steps 1-6 are all
enabled by default precisely because they are the interacting set: merging vertices with
``contractDegenerateEdges`` or ``removeDegenerateFaces`` turned off asks the operation to create
degeneracies and then leave them behind. Disable an individual pass only when you know your input
cannot produce the defect that pass handles.

Steps 7 and 8 are off by default because they change winding and topology rather than removing
defects introduced upstream of them; see below.

The merge sub-flags (``mergeBoundaries``, ``mergeNeighbors``) only take effect when
``mergeVertices`` is enabled; they refine *which* coincident vertices are merged.

When to enable coorientFaces and makeManifold
---------------------------------------------

``coorientFaces`` (default ``false``) reverses the winding of a minority of faces to enforce consistent
orientation at shared edges; enable it when a mesh renders with flipped or black faces from inconsistent
winding. ``makeManifold`` (default ``false``) forces a manifold result and is the heaviest pass; enable
it only when a downstream consumer (a renderer, or a boolean/level-set operation) requires manifold
input, since it can alter topology.

Tolerance and units
-------------------

``tolerance`` is the maximum distance, in **stage units**, between two vertices for them to be merged. The
default ``0`` merges only exactly coincident vertices. A non-zero tolerance depends on scene scale, so
scale it with the stage's ``metersPerUnit`` (a value sensible in a centimetre scene is 100x too large in
a metre scene).

Recommended pipelines
---------------------

A common data-quality baseline is ``generateNormals`` -> ``meshCleanup`` -> ``computeExtents``. Run
``meshCleanup`` before ``decimateMeshes`` so decimation operates on clean topology.

Starting configurations
-----------------------

Standard cleanup -- the defaults already enable steps 1-6, so no arguments are needed:

.. code-block:: json

    [{"operation": "meshCleanup"}]

Full repair (manifold, consistent winding):

.. code-block:: json

    [{"operation": "meshCleanup", "mergeVertices": true, "removeDegenerateFaces": true, "coorientFaces": true, "makeManifold": true}]
)DOC";
}


std::string MeshCleanupOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion MeshCleanupOperation::getVersion() const
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


namespace
{

// Whether the input mesh's normals attribute (if any) and face winding disagree on the manifold's orientation. Run
// independently of the cleanup defect set; cleanup's coorient step only enforces *consistent* winding among
// neighbouring faces and is free to pick either global sign.
bool windingsDisagreeWithNormals(const omo::HostMeshData& mesh_data, const UsdGeomMesh& usd_mesh, std::string* message)
{
    auto normals_attr = mesh_data.getAttribute(omo::Role::Normal);
    auto usd_orientation_attr = usd_mesh.GetOrientationAttr();
    PXR_NS::TfToken usd_orientation = PXR_NS::UsdGeomTokens->rightHanded;
    usd_orientation_attr.Get(&usd_orientation);
    auto vr = omo::checkNormalsConsistentWithWinding(mesh_data,
                                                     normals_attr,
                                                     usd_orientation == PXR_NS::UsdGeomTokens->rightHanded ?
                                                         omo::Orientation::RightHanded :
                                                         omo::Orientation::LeftHanded);
    if (!vr && message)
    {
        *message = vr.message;
    }
    return !vr;
}


// Whether every point of the mesh is coincident (the mesh collapses to a single point, i.e. a
// zero-size bounding box). omo::checkClean() performs an out-of-bounds heap write on such fully
// degenerate meshes; see processMesh().
bool isZeroExtentMesh(const UsdGeomMesh& usd_mesh)
{
    PXR_NS::VtVec3fArray points;
    if (!usd_mesh.GetPointsAttr().Get(&points) || points.empty())
    {
        return true;
    }

    // Zero-extent iff every point equals the first; bail on the first that differs. Exact equality
    // is intentional -- only fully point-collapsed meshes trigger the OOB, so don't widen to epsilon.
    const PXR_NS::GfVec3f& first = points[0];
    for (const PXR_NS::GfVec3f& point : points)
    {
        if (point != first)
        {
            return false;
        }
    }
    return true;
}

} // namespace


ProcessedData* MeshCleanupOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{
    using namespace omo;
    UsdGeomMesh usd_mesh(prim);

    try
    {
        omo::CleanupOptions options{ omo::noDefects, m_tolerance };
        if (m_mergeVertices)
        {
            if (m_mergeNeighbors)
            {
                options.fixes += omo::Defect::CoincidentNeighborVertices;
            }
            if (m_mergeBoundaries)
            {
                options.fixes += omo::Defect::CoincidentBoundaryVertices;
            }
        }
        if (m_contractDegenerateEdges)
        {
            options.fixes += omo::Defect::DegenerateEdges;
        }
        if (m_removeDegenerateFaces)
        {
            options.fixes += omo::Defect::DegenerateFaces;
        }
        if (m_removeIsolatedVertices)
        {
            options.fixes += omo::Defect::IsolatedVertices;
        }
        if (m_removeDuplicateFaces)
        {
            options.fixes += omo::Defect::DuplicateFaces;
        }
        if (m_coorientFaces)
        {
            options.fixes += omo::Defect::InconsistentlyOrientedFaces;
        }
        if (m_makeManifold)
        {
            options.fixes += omo::Defect::Nonmanifold;
        }

        // omo::checkClean() (analysis mode) does an out-of-bounds heap write on zero-extent meshes;
        // two or more surface it as an intermittent SIGSEGV. Cleanup handles them fine, so skip them
        // only in analysis -- before importMeshData, to avoid importing a mesh we won't check.
        if (getContext()->analysisMode && isZeroExtentMesh(usd_mesh))
        {
            USD_OPTIMIZE_LOG_VERBOSE("%s: skipping analysis of zero-extent (degenerate) mesh",
                                     prim.GetPath().GetAsString().c_str());
            return new ProcessedHostMeshData{ {}, prim, false /* don't write */ };
        }

        auto mesh_data = omo::importMeshData(usd_mesh, { omo::noDefects });

        if (getContext()->analysisMode)
        {
            auto vr = omo::checkClean(mesh_data, options);
            if (!vr)
            {
                USD_OPTIMIZE_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), vr.message.c_str());
            }

            if (vr.presentDefects.contains(omo::Defect::Nonmanifold))
            {
                m_report.meshesThatAreNonManifolds++;
                m_report.meshesThatAreNonManifoldsPaths.push_back(prim);
            }
            if (vr.presentDefects.intersects(omo::coincidentVertexDefects))
            {
                m_report.meshesWithMergeableVertices++;
                m_report.meshesWithMergeableVerticesPaths.push_back(prim);
            }
            if (vr.presentDefects.contains(omo::Defect::DegenerateEdges))
            {
                m_report.meshesWithDegenerateEdges++;
                m_report.meshesWithDegenerateEdgesPaths.push_back(prim);
            }
            if (vr.presentDefects.contains(omo::Defect::DegenerateFaces))
            {
                m_report.meshesWithDegenerateFaces++;
                m_report.meshesWithDegenerateFacesPaths.push_back(prim);
            }
            if (vr.presentDefects.contains(omo::Defect::IsolatedVertices))
            {
                m_report.meshesWithIsolatedVertices++;
                m_report.meshesWithIsolatedVerticesPaths.push_back(prim);
            }
            if (vr.presentDefects.contains(omo::Defect::DuplicateFaces))
            {
                m_report.meshesWithDuplicateFaces++;
                m_report.meshesWithDuplicateFacesPaths.push_back(prim);
            }

            std::string message;
            if (windingsDisagreeWithNormals(mesh_data, usd_mesh, &message))
            {
                USD_OPTIMIZE_LOG_VERBOSE("%s: %s", prim.GetPath().GetAsString().c_str(), message.c_str());
                m_report.meshesWithInconsistentWindings.push_back(prim);
            }

            return new ProcessedHostMeshData{ {}, prim, false /* don't write */ };
        }

        mesh_data = omo::cleanup(mesh_data, options);

        // Cleanup's coorient step only enforces winding consistency among neighbouring faces; the global sign is
        // arbitrary. Match it to the input normals here so downstream consumers don't see the mesh flip.
        if (m_coorientFaces && windingsDisagreeWithNormals(mesh_data, usd_mesh, nullptr))
        {
            mesh_data = omo::reverse(mesh_data);
        }

        return new ProcessedHostMeshData{ mesh_data, prim };
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        USD_OPTIMIZE_LOG_ERROR(errorMsg.c_str());
        return new ProcessedHostMeshData{ {}, prim, false /* don't write, leave mesh unchanged */ };
    }
}


void MeshCleanupOperation::executePost(const TotalStats& totalStats)
{
    // Keep the base "Total vertex/face count" lines.
    OmniOperation::executePost(totalStats);

    const size_t vertsRemoved = totalStats.before.vertexCount > totalStats.after.vertexCount ?
                                    totalStats.before.vertexCount - totalStats.after.vertexCount :
                                    0;
    const size_t facesRemoved = totalStats.before.faceCount > totalStats.after.faceCount ?
                                    totalStats.before.faceCount - totalStats.after.faceCount :
                                    0;

    USD_OPTIMIZE_LOG_INFO("Mesh cleanup removed %zu vertices and %zu faces", vertsRemoved, facesRemoved);
}


OperationResult MeshCleanupOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|MeshCleanup|Analysis");

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

    // Per-prim path lists backing the counts above (verbose reporting). Kept as
    // additive keys alongside the integer counts so existing consumers are
    // unaffected.
    analysis_result["meshesThatAreNonManifoldsPaths"] = _toJson(m_report.meshesThatAreNonManifoldsPaths);
    analysis_result["meshesWithMergeableVerticesPaths"] = _toJson(m_report.meshesWithMergeableVerticesPaths);
    analysis_result["meshesWithDegenerateEdgesPaths"] = _toJson(m_report.meshesWithDegenerateEdgesPaths);
    analysis_result["meshesWithDegenerateFacesPaths"] = _toJson(m_report.meshesWithDegenerateFacesPaths);
    analysis_result["meshesWithIsolatedVerticesPaths"] = _toJson(m_report.meshesWithIsolatedVerticesPaths);
    analysis_result["meshesWithDuplicateFacesPaths"] = _toJson(m_report.meshesWithDuplicateFacesPaths);

    JsObject resultJson;
    resultJson["analysis"] = analysis_result;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    return result;
}

} // namespace usd_optimize
