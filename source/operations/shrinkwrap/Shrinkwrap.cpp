// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "Shrinkwrap.h"

#include <openvdb/tools/ShrinkwrapCore.h>

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>

#include <algorithm>
#include <cfloat>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin with SO
SO_PLUGIN_INIT(omni::scene::optimizer::ShrinkwrapOperation);


namespace omni::scene::optimizer
{

using openvdb::shrinkwrap::ShrinkwrapMesh;
using openvdb::shrinkwrap::shrinkwrapMesh;
using openvdb::shrinkwrap::ShrinkwrapParams;
using openvdb::shrinkwrap::ShrinkwrapResult;

constexpr const char* s_categoryShrinkwrap = "SHRINKWRAP";


ShrinkwrapOperation::ShrinkwrapOperation()
    : Operation("shrinkwrap",
                "Shrinkwrap",
                "Convert meshes to a level set volume and extract a watertight mesh. "
                "Useful for closing holes, simplifying topology, and creating LODs.")
    , m_dim(512)
    , m_voxelSize(0.1)
    , m_erode(8.0)
    , m_threshold(0.0)
    , m_adaptivity(0.0)
    , m_extractLodPyramid(false)
{
    addArgument("paths",
                "Meshes to Shrinkwrap",
                kDisplayTypePrimPaths,
                "Optional list of prim paths/expressions to shrinkwrap",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("dim",
                "Grid Dimension",
                kDisplayTypeInt,
                "Grid dimension for the finest level. Controls resolution by specifying how many voxels along the "
                "largest axis. Higher values produce finer detail. Set to 0 to use Voxel Size instead.",
                m_dim)
        .setMin(0)
        .setVisible(false);

    addArgument("voxelSize",
                "Voxel Size",
                kDisplayTypeFloat,
                "Explicit voxel size. Smaller values produce finer detail but use more memory. "
                "Set to 0 to use Grid Dimension instead.",
                m_voxelSize)
        .setMin(0);

    // Level set controls
    addArgument("erode",
                "Erosion Steps",
                kDisplayTypeFloat,
                "Number of erosion steps. Controls how much the level set can shrink/erode the shape.",
                m_erode)
        .setMin(0);

    addArgument("threshold",
                "Closing Threshold",
                kDisplayTypeFloat,
                "Size of the largest geometric feature or gap beyond which the algorithm should erode "
                "no further. Higher values close more holes and gaps in the mesh.",
                m_threshold)
        .setMin(0);

    // Mesh output controls
    addArgument("adaptivity",
                "Adaptive Meshing Threshold",
                kDisplayTypeFloatSlider,
                "Controls adaptive meshing. 0 = no adaptive meshing (uniform tessellation), "
                "1 = most adaptive (fewest triangles).",
                m_adaptivity)
        .setMin(0)
        .setMax(1);

    // LOD
    addArgument("extractLodPyramid",
                "Extract LOD Pyramid",
                kDisplayTypeBool,
                "When enabled, extracts meshes for all LOD levels in the pyramid. "
                "When disabled, only the finest (tightest) level is output.",
                m_extractLodPyramid)
        .setVisible(false);
}


ShrinkwrapOperation::~ShrinkwrapOperation() = default;


std::string ShrinkwrapOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion ShrinkwrapOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string ShrinkwrapOperation::getCategory() const
{
    return s_categoryShrinkwrap;
}


std::string ShrinkwrapOperation::getDisplayGroup() const
{
    return s_displayGroupUtilities;
}


OperationResult ShrinkwrapOperation::executeImpl()
{
    constexpr double kHalfWidth = 3.0;
    constexpr double kIsovalue = 0.0;

    SO_LOG_VERBOSE("Shrinkwrap: voxelSize=%.4f, maxDim=%u, erode=%.2f, threshold=%.2f, halfWidth=%.2f",
                   m_voxelSize,
                   m_dim,
                   m_erode,
                   m_threshold,
                   kHalfWidth);
    SO_LOG_VERBOSE("Shrinkwrap: adaptivity=%.2f, isovalue=%.2f, extractLodPyramid=%s",
                   m_adaptivity,
                   kIsovalue,
                   m_extractLodPyramid ? "true" : "false");

    // Resolve prims to process
    constexpr bool meshesOnly = true;
    constexpr bool reverse = false;
    std::vector<UsdPrim> primsToProcess = _resolveExpressionsToPrims(getUsdStage(), m_meshPrimPaths, meshesOnly, reverse);

    SO_LOG_INFO("Shrinkwrap: found %zu mesh prim(s) to process", primsToProcess.size());

    if (primsToProcess.empty())
    {
        return { true };
    }

    for (const auto& prim : primsToProcess)
    {
        if (prim.IsInstanceProxy())
        {
            SO_LOG_VERBOSE("Skipped prim %s because it is an instance proxy", prim.GetPath().GetAsString().c_str());
            continue;
        }

        UsdGeomMesh usdMesh(prim);
        if (!usdMesh)
        {
            continue;
        }

        VtVec3fArray usdPoints;
        usdMesh.GetPointsAttr().Get(&usdPoints);

        VtIntArray faceVertexIndices;
        usdMesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

        VtIntArray faceVertexCounts;
        usdMesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);

        if (usdPoints.empty())
        {
            SO_LOG_VERBOSE("Shrinkwrap: %s: skipping empty mesh", prim.GetPath().GetAsString().c_str());
            continue;
        }

        SO_LOG_VERBOSE("Shrinkwrap: %s ('%s'): input mesh has %zu vertices, %zu faces",
                       prim.GetPath().GetAsString().c_str(),
                       prim.GetName().GetString().c_str(),
                       usdPoints.size(),
                       faceVertexCounts.size());

        // Get the local-to-world transform for this prim so that the
        // shrinkwrap operates in world space.
        UsdGeomXformCache xformCache;
        GfMatrix4d localToWorld = xformCache.GetLocalToWorldTransform(prim);

        // Build input mesh for the core function, transforming points
        // into world space.
        ShrinkwrapMesh inputMesh;
        inputMesh.points.resize(usdPoints.size());
        for (size_t i = 0; i < usdPoints.size(); ++i)
        {
            GfVec3f worldPt = GfVec3f(localToWorld.Transform(GfVec3d(usdPoints[i])));
            inputMesh.points[i] = { worldPt[0], worldPt[1], worldPt[2] };
        }

        // Triangulate faces and collect quads
        size_t idx = 0;
        for (size_t f = 0; f < faceVertexCounts.size(); ++f)
        {
            int count = faceVertexCounts[f];
            if (count < 0)
            {
                SO_LOG_WARN("Shrinkwrap: %s: skipping face %zu with negative count=%d",
                            prim.GetPath().GetAsString().c_str(),
                            f,
                            count);
                break;
            }
            if (count < 3)
            {
                SO_LOG_WARN("Shrinkwrap: %s: skipping degenerate face %zu (count=%d)",
                            prim.GetPath().GetAsString().c_str(),
                            f,
                            count);
                idx += count;
                continue;
            }
            if (idx + count > faceVertexIndices.size())
            {
                SO_LOG_WARN("Shrinkwrap: %s: skipping malformed face %zu (count=%d, idx=%zu, total indices=%zu)",
                            prim.GetPath().GetAsString().c_str(),
                            f,
                            count,
                            idx,
                            faceVertexIndices.size());
                break;
            }
            if (count == 3)
            {
                inputMesh.triangles.push_back(
                    { faceVertexIndices[idx], faceVertexIndices[idx + 1], faceVertexIndices[idx + 2] });
            }
            else if (count == 4)
            {
                inputMesh.quads.push_back({ faceVertexIndices[idx],
                                            faceVertexIndices[idx + 1],
                                            faceVertexIndices[idx + 2],
                                            faceVertexIndices[idx + 3] });
            }
            else
            {
                for (int k = 1; k < count - 1; ++k)
                {
                    inputMesh.triangles.push_back(
                        { faceVertexIndices[idx], faceVertexIndices[idx + k], faceVertexIndices[idx + k + 1] });
                }
            }
            idx += count;
        }

        SO_LOG_VERBOSE("Shrinkwrap: %s: %zu triangles after triangulation",
                       prim.GetPath().GetAsString().c_str(),
                       inputMesh.triangles.size());

        // Compute bounding box from input points
        std::array<float, 3> bboxMin = { FLT_MAX, FLT_MAX, FLT_MAX };
        std::array<float, 3> bboxMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
        for (const auto& p : inputMesh.points)
        {
            for (int a = 0; a < 3; ++a)
            {
                bboxMin[a] = std::min(bboxMin[a], p[a]);
                bboxMax[a] = std::max(bboxMax[a], p[a]);
            }
        }
        float maxLength = 0.0f;
        for (int a = 0; a < 3; ++a)
        {
            maxLength = std::max(maxLength, bboxMax[a] - bboxMin[a]);
        }

        // Compute effective voxel size, guarding against a non-positive denominator
        // when dim is too small; fall back to userVoxelSize or error out.
        float halfWidth = static_cast<float>(kHalfWidth);
        float userVoxelSize = static_cast<float>(m_voxelSize);

        float dimDenominator = static_cast<float>(m_dim) - 2.0f * (halfWidth + 1.0f);
        float dimVoxelSize = -1.0f;
        if (dimDenominator > 0.0f && maxLength > 0.0f)
        {
            dimVoxelSize = maxLength / dimDenominator;
        }

        float effectiveVoxelSize;
        if (dimVoxelSize > 0.0f)
        {
            effectiveVoxelSize = (userVoxelSize > 0.0f) ? std::max(userVoxelSize, dimVoxelSize) : dimVoxelSize;
        }
        else if (userVoxelSize > 0.0f)
        {
            effectiveVoxelSize = userVoxelSize;
        }
        else
        {
            SO_LOG_ERROR("Shrinkwrap: %s: Cannot compute a valid voxel size (dim too small and no explicit voxelSize set)",
                         prim.GetPath().GetAsString().c_str());
            continue;
        }

        int effectiveDim = static_cast<int>(maxLength / effectiveVoxelSize + 2.0f * (halfWidth + 1.0f));

        SO_LOG_VERBOSE(
            "Shrinkwrap: %s: effectiveVoxelSize=%.6f, effectiveDim=%d (dimLimit=%u, dimVoxelSize=%.6f, userVoxelSize=%.6f)",
            prim.GetPath().GetAsString().c_str(),
            effectiveVoxelSize,
            effectiveDim,
            m_dim,
            dimVoxelSize,
            userVoxelSize);

        // The narrow band occupies 2*(halfWidth+1) voxels total.  If the
        // mesh doesn't span enough voxels beyond that, OpenVDB can produce
        // a null grid and crash.  Require at least one content voxel beyond
        // the band on each axis.
        int minDim = static_cast<int>(2.0f * (halfWidth + 1.0f)) + 2;
        if (effectiveDim < minDim)
        {
            SO_LOG_WARN(
                "Shrinkwrap: %s: skipping mesh -- effective grid dimension %d is below "
                "minimum %d (voxel size %.4f is too large for bounding box %.4f)",
                prim.GetPath().GetAsString().c_str(),
                effectiveDim,
                minDim,
                effectiveVoxelSize,
                maxLength);
            continue;
        }

        // Run shrinkwrap
        ShrinkwrapParams params;
        params.voxelSize = effectiveVoxelSize;
        params.halfWidth = halfWidth;
        params.erode = static_cast<float>(m_erode);
        params.threshold = static_cast<float>(m_threshold);
        params.isovalue = static_cast<float>(kIsovalue);
        params.adaptivity = static_cast<float>(m_adaptivity);
        params.extractLodPyramid = m_extractLodPyramid;

        ShrinkwrapResult swResult = shrinkwrapMesh(inputMesh, params);

        if (!swResult.error.empty())
        {
            SO_LOG_ERROR("Shrinkwrap: %s: %s", prim.GetPath().GetAsString().c_str(), swResult.error.c_str());
            continue;
        }

        if (swResult.lodMeshes.empty())
        {
            SO_LOG_WARN("Shrinkwrap produced no meshes for %s", prim.GetPath().GetAsString().c_str());
            continue;
        }

        // Log grid info only for grids that produced meshes
        size_t meshCount = swResult.lodMeshes.size();
        SO_LOG_INFO("Shrinkwrap: %s: generated %zu LOD mesh(es)", prim.GetPath().GetAsString().c_str(), meshCount);
        for (size_t g = 0; g < meshCount && g < swResult.gridInfos.size(); ++g)
        {
            const auto& info = swResult.gridInfos[g];
            SO_LOG_VERBOSE("Shrinkwrap: %s:   LOD %zu: voxelSize=%.4f, dim=%d, activeVoxels=%zu",
                           prim.GetPath().GetAsString().c_str(),
                           g,
                           info.voxelSize,
                           info.dim,
                           info.activeVoxels);
        }

        // The output prim is a sibling (same parent), so we need the
        // parent's world-to-local transform to place the world-space
        // shrinkwrap result correctly under that parent.
        GfMatrix4d parentWorldToLocal(1.0);
        UsdPrim parentPrim = prim.GetParent();
        if (parentPrim && parentPrim != getUsdStage()->GetPseudoRoot())
        {
            GfMatrix4d parentLocalToWorld = xformCache.GetLocalToWorldTransform(parentPrim);
            parentWorldToLocal = parentLocalToWorld.GetInverse();
        }

        // Write output meshes as new sibling prims (the original mesh is preserved).
        for (size_t g = 0; g < swResult.lodMeshes.size(); ++g)
        {
            const auto& outMesh = swResult.lodMeshes[g];

            std::string suffix = (g == 0) ? "_shrinkwrap" : "_shrinkwrap_lod" + std::to_string(g);
            std::string outName = prim.GetName().GetString() + suffix;
            SdfPath targetPath = prim.GetPath().GetParentPath().AppendChild(TfToken(outName));
            UsdGeomMesh targetMesh = UsdGeomMesh::Define(getUsdStage(), targetPath);

            SO_LOG_INFO("Shrinkwrap: %s -> '%s': output LOD %zu: %zu vertices, %zu triangles, %zu quads (%zu total faces)",
                        prim.GetPath().GetAsString().c_str(),
                        targetPath.GetAsString().c_str(),
                        g,
                        outMesh.points.size(),
                        outMesh.triangles.size(),
                        outMesh.quads.size(),
                        outMesh.triangles.size() + outMesh.quads.size());

            targetMesh.GetSubdivisionSchemeAttr().Set(UsdGeomTokens->none);

            // Write new mesh data, transforming world-space points back into
            // the parent prim's local space.
            size_t totalFaces = outMesh.triangles.size() + outMesh.quads.size();

            VtVec3fArray newPoints(outMesh.points.size());
            for (size_t i = 0; i < outMesh.points.size(); ++i)
            {
                const auto& p = outMesh.points[i];
                GfVec3f localPt = GfVec3f(parentWorldToLocal.Transform(GfVec3d(p[0], p[1], p[2])));
                newPoints[i] = localPt;
            }

            VtIntArray newFaceVertexCounts(totalFaces);
            VtIntArray newFaceVertexIndices;
            newFaceVertexIndices.reserve(outMesh.triangles.size() * 3 + outMesh.quads.size() * 4);

            size_t faceIdx = 0;
            for (const auto& tri : outMesh.triangles)
            {
                newFaceVertexCounts[faceIdx++] = 3;
                newFaceVertexIndices.push_back(tri[0]);
                newFaceVertexIndices.push_back(tri[1]);
                newFaceVertexIndices.push_back(tri[2]);
            }
            for (const auto& quad : outMesh.quads)
            {
                newFaceVertexCounts[faceIdx++] = 4;
                newFaceVertexIndices.push_back(quad[0]);
                newFaceVertexIndices.push_back(quad[1]);
                newFaceVertexIndices.push_back(quad[2]);
                newFaceVertexIndices.push_back(quad[3]);
            }

            targetMesh.GetPointsAttr().Set(newPoints);
            targetMesh.GetFaceVertexCountsAttr().Set(newFaceVertexCounts);
            targetMesh.GetFaceVertexIndicesAttr().Set(newFaceVertexIndices);
        }
    }

    SO_LOG_INFO("Shrinkwrap: completed processing %zu prim(s)", primsToProcess.size());
    return { true };
}


} // namespace omni::scene::optimizer
