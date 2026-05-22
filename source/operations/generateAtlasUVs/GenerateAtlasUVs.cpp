// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "GenerateAtlasUVs.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// AutoUV (minimal API — Eigen-free headers)
#include <array>
#include <autouv/minimal_api.h>
#include <limits>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/base/gf/transform.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>


PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::AtlasUVsOperation);


namespace omni::scene::optimizer
{

using namespace autouv;

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((primvarsUVs, "st"))
);
// LCOV_EXCL_STOP
// clang-format on

// Constants
constexpr const char* s_category = "AUTOUV_UNWRAP";

namespace
{

// Same logic as autouv::triangulate_polygon_fan (triangulate_polygon.cpp).
void triangulate_polygon_fan(int32_t polygon_degree, std::vector<std::array<int32_t, 3>>& tri_inds_out)
{
    tri_inds_out.resize(static_cast<size_t>(polygon_degree - 2));
    for (int32_t i = 0; i + 2 < polygon_degree; i++)
    {
        tri_inds_out[static_cast<size_t>(i)] = std::array<int32_t, 3>{ 0, i + 1, i + 2 };
    }
}

constexpr int32_t kInvalidCornerUvInd = std::numeric_limits<int32_t>::min();

} // namespace

static bool computeUVs(const UsdGeomMesh& mesh,
                       const GfVec3f& scale,
                       VtArray<GfVec2f>& uvsArray,
                       VtArray<int>& faceUvIndices,
                       double distortionThreshold,
                       bool enableAtlasPacking,
                       bool useWorldSpaceScales,
                       std::string& error)
{
    // extract vertex positions and face indices
    VtVec3fArray points;
    VtIntArray faceVertexCounts;
    VtIntArray faceVertexIndices;
    mesh.GetPointsAttr().Get(&points);
    mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
    mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

    if (points.empty() || faceVertexCounts.empty() || faceVertexIndices.empty())
    {
        // USD mesh is empty, nothing to do
        error = "Empty mesh";
        return false;
    }

    // == Copy over all vertex positions [nV*3] row-major
    int32_t nV = static_cast<int32_t>(points.size());
    std::vector<double> V(static_cast<size_t>(nV) * 3);
    for (int32_t iV = 0; iV < nV; iV++)
    {
        for (int32_t j = 0; j < 3; j++)
        {
            V[static_cast<size_t>(iV) * 3 + static_cast<size_t>(j)] = scale[j] * points[iV][j];
        }
    }

    // === Copy over face indices (accumulate, then flatten to minimal API layout)
    std::vector<std::array<int32_t, 3>> F_tmp;
    std::vector<std::array<bool, 3>> edge_is_real_tmp;
    std::vector<std::array<int32_t, 3>> tri_inds_out;

    size_t face_ind_start = 0;
    size_t n_faces = faceVertexCounts.size();

    for (size_t fi = 0; fi < n_faces; fi++)
    {
        int32_t degree = faceVertexCounts[fi];
        triangulate_polygon_fan(degree, tri_inds_out);

        for (size_t iTri = 0; iTri < tri_inds_out.size(); iTri++)
        {
            const auto& tri_poly_inds = tri_inds_out[iTri];
            std::array<int32_t, 3> cIndTupArr{ faceVertexIndices[face_ind_start + static_cast<size_t>(tri_poly_inds[0])],
                                               faceVertexIndices[face_ind_start + static_cast<size_t>(tri_poly_inds[1])],
                                               faceVertexIndices[face_ind_start + static_cast<size_t>(tri_poly_inds[2])] };
            F_tmp.emplace_back(cIndTupArr);

            std::array<bool, 3> edge_is_real{ (tri_poly_inds[0] + 1) % degree == tri_poly_inds[1],
                                              (tri_poly_inds[1] + 1) % degree == tri_poly_inds[2],
                                              (tri_poly_inds[2] + 1) % degree == tri_poly_inds[0] };
            edge_is_real_tmp.push_back(edge_is_real);
        }

        face_ind_start += static_cast<size_t>(degree);
    }

    int32_t nTri = static_cast<int32_t>(F_tmp.size());
    std::vector<int32_t> F(static_cast<size_t>(nTri) * 3);
    std::vector<bool> edge_is_real(static_cast<size_t>(nTri) * 3);
    for (int32_t iT = 0; iT < nTri; iT++)
    {
        for (int32_t j = 0; j < 3; j++)
        {
            F[static_cast<size_t>(iT) * 3 + static_cast<size_t>(j)] =
                F_tmp[static_cast<size_t>(iT)][static_cast<size_t>(j)];
            edge_is_real[static_cast<size_t>(iT) * 3 + static_cast<size_t>(j)] =
                edge_is_real_tmp[static_cast<size_t>(iT)][static_cast<size_t>(j)];
        }
    }

    // Prep options
    ParameterizeOptions options;
    options.enable_threading = true;
    options.distortion_thresh = std::max(1.05, distortionThreshold);
    if (enableAtlasPacking)
    {
        options.atlas_pack_method = AtlasPackMethod::Hybrid;
        options.orient_islands_by_curvature = false;
    }
    else
    {
        options.atlas_pack_method = AtlasPackMethod::None;
        options.orient_islands_by_curvature = true;
    }
    options.atlas_scaling = useWorldSpaceScales ? AtlasScaling::WorldSpace : AtlasScaling::UnitBox;

    ParameterizeResultMinimal result = patch_parameterize_pipeline_minimal(V, F, edge_is_real, options);

    // create USD UVs array
    uvsArray.clear();
    const int32_t nCoords = static_cast<int32_t>(result.coords.size() / 2);
    for (int32_t i = 0; i < nCoords; i++)
    {
        uvsArray.emplace_back((float)result.coords[static_cast<size_t>(i) * 2],
                              (float)result.coords[static_cast<size_t>(i) * 2 + 1]);
    }

    // create USD face uv indices
    size_t tri_ind_start = 0;
    std::vector<int32_t> corner_UVind;
    tri_inds_out.clear();
    faceUvIndices.clear();

    for (size_t fi = 0; fi < n_faces; fi++)
    {
        int32_t degree = faceVertexCounts[fi];
        triangulate_polygon_fan(degree, tri_inds_out);
        int32_t n_poly_tris = static_cast<int32_t>(tri_inds_out.size());

        corner_UVind.resize(static_cast<size_t>(degree));
        std::fill(corner_UVind.begin(), corner_UVind.end(), kInvalidCornerUvInd);

        for (size_t iTri = 0; iTri < tri_inds_out.size(); iTri++)
        {
            const auto& tri_poly_inds = tri_inds_out[iTri];
            size_t globalTriIdx = tri_ind_start + iTri;
            for (int32_t s = 0; s < 3; s++)
            {
                int32_t orig_corner_ind = tri_poly_inds[static_cast<size_t>(s)];
                int32_t UV_ind = result.coord_inds[globalTriIdx * 3 + static_cast<size_t>(s)];

                int32_t& orig_corner_UVind = corner_UVind[static_cast<size_t>(orig_corner_ind)];
                if (orig_corner_UVind != kInvalidCornerUvInd && orig_corner_UVind != UV_ind)
                {
                    error = "parameterization created a cut along the induced edge of a triangulated polygon";
                    return false;
                }

                orig_corner_UVind = UV_ind;
            }
        }

        for (int l = 0; l < degree; l++)
        {
            int32_t tInd = corner_UVind[static_cast<size_t>(l)];
            faceUvIndices.emplace_back(tInd);
        }

        tri_ind_start += static_cast<size_t>(n_poly_tris);
    }

    return true;
}

AtlasUVsOperation::AtlasUVsOperation()
    : Operation("generateAtlasUVs", "Auto UV Unwrap", "Generates texture(UV) coordinates using AutoUV unwrap")
    , m_distortionThreshold(3.0)
    , m_enableAtlasPacking(true)
    , m_useWorldSpaceScales(true)
    , m_scaleFactor(0.01)
    , m_scaleUnits(0.0)
    , m_overwriteExisting(true)
{
    addArgument("paths",
                "Meshes to generate UVs for",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider",
                m_meshPrimPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("distortionThreshold",
                "Distortion Threshold",
                kDisplayTypeFloat,
                "Lower values reduce distortion but increase number of UV islands. Should be > 1.",
                m_distortionThreshold);

    addArgument("enableAtlasPacking",
                "Enable Atlas Packing",
                kDisplayTypeBool,
                "Enable atlas packing for AutoUV unwrap",
                m_enableAtlasPacking);

    addArgument("useWorldSpaceScales",
                "Use World Space Scales",
                kDisplayTypeBool,
                "Scales UV islands to world space dimensions of the source mesh",
                m_useWorldSpaceScales);

    addArgument("scaleFactor",
                "Scale Factor",
                kDisplayTypeFloat,
                "Uniform scale factor to apply to UV islands to change texel density",
                m_scaleFactor);

    addArgument("scaleUnits",
                "Scale Units",
                kDisplayTypeFloatPresets,
                "Real world unit in which the scale factor is described",
                m_scaleUnits)
        .setPrecision(9)
        .setFloatPresets(_getDefaultLinearUnitsPresets());

    addArgument("overwriteExisting",
                "Overwrite Existing",
                kDisplayTypeBool,
                "Overwrite existing UVs on the meshes selected for processing",
                m_overwriteExisting);
}


std::string AtlasUVsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion AtlasUVsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string AtlasUVsOperation::getCategory() const
{
    return s_category;
}


OperationResult AtlasUVsOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|generateAtlasUVs");

    // Resolve paths to prims
    bool meshesOnly = true;
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(getUsdStage(), m_meshPrimPaths, meshesOnly);

    // Refine the list of prims to just those that we can operate on
    std::vector<UsdPrim> validPrims;
    for (const auto& prim : prims)
    {
        // Omit Invalid Prims and Instance Proxies
        if (!prim || prim.IsInstanceProxy())
        {
            continue;
        }

        // Omit prims that are not valid Meshes with authored topology attributes
        const UsdGeomMesh mesh(prim);
        if (!mesh)
        {
            continue;
        }
        if (!mesh.GetPointsAttr().IsAuthored() || !mesh.GetFaceVertexCountsAttr().IsAuthored() ||
            !mesh.GetFaceVertexIndicesAttr().IsAuthored())
        {
            continue;
        }

        // exclude time varying meshes as they cause a crash, adjust when a fix is implemented
        if (_hasAuthoredTimeSamples(prim))
        {
            continue;
        }

        // if this prim has authored UVs with values, and we shouldn't overwrite, just skip it
        if (!m_overwriteExisting && _primHasAuthoredUVsWithValues(prim))
        {
            continue;
        }

        validPrims.push_back(prim);
    }

    // Early out if we have no valid prims to process
    if (validPrims.empty())
    {
        return { true };
    }

    auto numPrims = validPrims.size();

    // We'll run the code in parallel over all the passed prims. Since we cannot add primvars for calculated uvs in
    // parallel, we'll keep them for all the prims and in the end add them sequentially.
    std::vector<VtVec2fArray> allFaceVertexUVs;
    allFaceVertexUVs.resize(numPrims);

    std::vector<VtArray<int>> allFaceVertexUvIndices;
    allFaceVertexUvIndices.resize(numPrims);

    tbb::parallel_for(tbb::blocked_range<size_t>(size_t(0), numPrims),
                      [&](const tbb::blocked_range<size_t>& r)
                      {
                          auto xformCache = UsdGeomXformCache();

                          // caculate scaling amount to apply
                          float scaleValue = _calculteUVScaleValue(getUsdStage(),
                                                                   static_cast<float>(m_scaleFactor),
                                                                   static_cast<float>(m_scaleUnits));

                          for (size_t i = r.begin(), ie = r.end(); i < ie; i++)
                          {
                              const auto& prim = validPrims[i];

                              // Currently only deals with meshes, but this check could be changed in the future if we
                              // want to support other types.
                              UsdGeomMesh mesh(prim);
                              if (!mesh)
                              {
                                  // Excluded, already filtered by resolvePathsToPrims to be only meshes
                                  continue; // LCOV_EXCL_LINE
                              }

                              // Compute the scale vector of the local to world transform matrix. We multiply local
                              // positions by these values to get the mesh to its world scale.
                              GfVec3f scale(scaleValue, scaleValue, scaleValue);
                              if (m_useWorldSpaceScales)
                              {
                                  auto l2w = xformCache.GetLocalToWorldTransform(prim);
                                  auto s = GfTransform(l2w).GetScale();
                                  scale[0] *= float(std::abs(s[0]));
                                  scale[1] *= float(std::abs(s[1]));
                                  scale[2] *= float(std::abs(s[2]));
                              }

                              std::string errorStr;
                              bool success = computeUVs(mesh,
                                                        scale,
                                                        allFaceVertexUVs[i],
                                                        allFaceVertexUvIndices[i],
                                                        m_distortionThreshold,
                                                        m_enableAtlasPacking,
                                                        m_useWorldSpaceScales,
                                                        errorStr);

                              if (!success)
                              {
                                  // LCOV_EXCL_START
                                  // failed to generate UVs for this mesh, log error and continue
                                  errorStr += " Mesh name: " + mesh.GetPath().GetName();
                                  std::lock_guard<std::mutex> lock(m_logMutex);
                                  SO_LOG_ERROR(errorStr.c_str());
                                  // LCOV_EXCL_STOP
                              }
                          }
                      });

    // Run over all prims and create the UV primvars with the contents computed above.
    SdfChangeBlock _changeBlock;
    for (size_t i = 0; i < numPrims; i++)
    {
        // Skip this prim if no uvs were generated
        if (allFaceVertexUVs[i].empty())
        {
            continue; // LCOV_EXCL_LINE
        }

        UsdGeomPrimvarsAPI primvarsAPI(validPrims[i]);
        auto uvCoords = primvarsAPI.CreatePrimvar(_tokens->primvarsUVs,
                                                  SdfValueTypeNames->TexCoord2fArray,
                                                  UsdGeomTokens->faceVarying);

        uvCoords.Set(allFaceVertexUVs[i]);
        uvCoords.SetIndices(allFaceVertexUvIndices[i]);
    }

    return { true };
}

} // namespace omni::scene::optimizer
