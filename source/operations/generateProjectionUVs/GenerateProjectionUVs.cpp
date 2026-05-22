// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "GenerateProjectionUVs.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/base/gf/transform.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>
#include <tbb/parallel_scan.h>
#include <tbb/parallel_sort.h>

// C++
#include <iomanip>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::GenerateProjectionUVsOperation);


namespace omni::scene::optimizer
{

// Constants
constexpr const char* s_category = "GENERATE_PROJECTION_UVS";

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((primvarsUVs, "st"))
);
// LCOV_EXCL_STOP
// clang-format on

/// Hash functor so we can use SdfPaths in a hash set
struct SdfPathHashFunctor
{
    size_t operator()(const SdfPath& path) const
    {
        return path.GetHash();
    }
};


GenerateProjectionUVsOperation::GenerateProjectionUVsOperation()
    : Operation("generateProjectionUVs", "Generate Projection UVs", "Generate Projection UVs")
{

    addArgument("paths",
                "Meshes to generate UVs for",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider",
                m_meshPrimPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("preprojectionXform", "Preprojection Xform", kDisplayTypeFloatArray, "Preprojection Xform", m_xformMatrixEntries)
        .setVisible(false);

    addArgument("projectionType", "Projection Type", kDisplayTypeEnum, "Projection method for generating UVs", m_projectionType)
        .setEnumValues<ProjectionType>({
            { ProjectionType::ePlanar, "Planar" },
            { ProjectionType::eSpherical, "Spherical" },
            { ProjectionType::eCylindrical, "Cylindrical" },
            { ProjectionType::eTriplanar, "Triplanar" },
            { ProjectionType::eCube, "Cube" },
        });

    addArgument("useWorldSpaceScales",
                "Use World Space Scales",
                kDisplayTypeBool,
                "Scale to world space dimensions before projection",
                m_useWorldSpaceScales);

    addArgument("scaleFactor",
                "Scale Factor",
                kDisplayTypeFloat,
                "Uniform scale factor to apply to change UV coordinates texel density",
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


std::string GenerateProjectionUVsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion GenerateProjectionUVsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string GenerateProjectionUVsOperation::getCategory() const
{
    return s_category;
}

// inserts abstract meshes and removes instance meshes with compatible transforms
static void _insertAbstractMeshes(const UsdStageWeakPtr& stage, std::vector<UsdPrim>& prims)
{
    auto layer = stage->GetEditTarget().GetLayer();

    std::map<SdfPath, std::vector<UsdPrim>> protoToInstanceMap;
    const std::vector<UsdPrim>& prototypes = stage->GetPrototypes();

    size_t count = prototypes.size();
    std::mutex insertMutex;

    // this filter is used by both composition queries
    UsdPrimCompositionQuery::Filter filter;
    filter.arcTypeFilter = UsdPrimCompositionQuery::ArcTypeFilter::ReferenceOrPayload;

    // composition queries are expensive, do them in parallel
    // we walk the children of the prototypes looking for meshes, then collect the target node path of the prototype
    // mesh GetPrototypes returns paths resembling "/__Prototype_3/Mesh" <-- cannot have UVs generated, where as the
    // target node path we collect resembles "/World/Assemblies/Chair/Mesh" <-- can have UVs generated
    tbb::parallel_for(tbb::blocked_range<size_t>(0, count),
                      [&](const tbb::blocked_range<size_t>& range)
                      {
                          for (size_t i = range.begin(); i < range.end(); ++i)
                          {
                              const auto& prim = prototypes[i];
                              auto primRange = UsdPrimRange(prim);
                              for (const auto& child : primRange)
                              {
                                  // omit invalid prims
                                  if (!child)
                                  {
                                      continue;
                                  }

                                  // omit non-meshes
                                  if (!child.IsA<UsdGeomMesh>())
                                  {
                                      continue;
                                  }

                                  std::vector<SdfPath> meshPaths;
                                  UsdPrimCompositionQuery compQ(child, filter);
                                  for (const auto& arc : compQ.GetCompositionArcs())
                                  {
                                      auto targetLayer = arc.GetTargetLayer();

                                      // we only want arcs in the current edit target
                                      if (targetLayer != layer)
                                      {
                                          continue;
                                      }
                                      SdfPath meshPath = arc.GetTargetNode().GetPath();
                                      meshPaths.push_back(meshPath);
                                  }

                                  {
                                      std::lock_guard<std::mutex> lock(insertMutex);
                                      for (const auto& path : meshPaths)
                                      {
                                          protoToInstanceMap.insert(std::make_pair(path, std::vector<UsdPrim>()));
                                      }
                                  }
                              }
                          }
                      });

    count = prims.size();

    // composition queries are expensive, do them in parallel
    // we query the non-prototype meshes for matches to the target node paths for prototype meshes
    tbb::parallel_for(tbb::blocked_range<size_t>(0, count),
                      [&](const tbb::blocked_range<size_t>& range)
                      {
                          for (size_t i = range.begin(); i < range.end(); ++i)
                          {
                              const auto& prim = prims[i];
                              SdfPath targetPath;
                              UsdPrimCompositionQuery compQ(prim, filter);
                              for (const auto& arc : compQ.GetCompositionArcs())
                              {
                                  auto targetLayer = arc.GetTargetLayer();
                                  if (targetLayer != layer)
                                  {
                                      continue;
                                  }
                                  SdfPath target = arc.GetTargetNode().GetPath();
                                  if (protoToInstanceMap.count(target))
                                  {
                                      targetPath = target;
                                      std::lock_guard<std::mutex> lock(insertMutex);
                                      protoToInstanceMap[targetPath].push_back(prim);
                                  }
                              }

                              if (!targetPath.IsEmpty())
                              {
                                  std::lock_guard<std::mutex> lock(insertMutex);
                                  protoToInstanceMap[targetPath].push_back(prim);
                              }
                          }
                      });


    // add prototype meshes to list of prims to operate on and construct set of instances to remove
    UsdGeomXformCache xCache;
    std::unordered_set<SdfPath, SdfPathHashFunctor> remove;

    // use a double cast of the float epsilon for matrix comparisons, there is potential for float values in the
    // transform stack
    const static double epsilon = std::numeric_limits<float>::epsilon();
    for (auto const& [protoPath, instances] : protoToInstanceMap)
    {
        UsdPrim protoPrim = stage->GetPrimAtPath(protoPath);
        prims.push_back(protoPrim);
        GfMatrix4d protoMatrix = xCache.GetLocalToWorldTransform(protoPrim);
        for (const auto& prim : instances)
        {
            bool canUseProtoUvs = true;
            GfMatrix4d instanceMatrix = xCache.GetLocalToWorldTransform(prim);
            // compare the first 3 rows of the matrix, ignoring translation (last row)
            for (int i = 0; i < 3; ++i)
            {
                GfVec4d instance = instanceMatrix.GetRow(i);
                GfVec4d proto = protoMatrix.GetRow(i);

                // check that the delta between row values is less than epsilon
                for (int j = 0; j < 4; ++j)
                {
                    if (fabs(instance[j] - proto[j]) > epsilon)
                    {
                        canUseProtoUvs = false;
                        break;
                    }
                }
                // if we have determined the instance mesh cannot use the prototype uvs, no need to keep checking
                if (!canUseProtoUvs)
                {
                    break;
                }
            }
            if (canUseProtoUvs)
            {
                remove.insert(prim.GetPath());
            }
        }
    }

    // remove prims that do not need uvs generated
    prims.erase(std::remove_if(prims.begin(),
                               prims.end(),
                               [&remove](const UsdPrim& prim)
                               { return (std::find(remove.begin(), remove.end(), prim.GetPath()) != remove.end()); }),
                prims.end());
}


static void _generateProjectionUVs(const std::vector<UsdPrim>& prims,
                                   ProjectionType projectionType,
                                   bool useWorldSpaceScales,
                                   const GfMatrix4f& preprojectionXform)
{
    const bool make_indexed = true;
    const auto inv_2pi = 1 / (2 * float(M_PI));

    // Check whether the preprojectionXform is identity. If it is we can skip the matrix multiplications.
    bool xform_is_identity = true;
    for (auto i = 0; i < 4; ++i)
    {
        for (auto j = 0; j < 4; ++j)
        {
            if (preprojectionXform[i][j] != float(i == j))
            {
                xform_is_identity = false;
                break;
            }
        }
    }

    // Experimentally determined to be a reasonable value
    int grain_size = 200;
    auto num_prims = prims.size();

    // We'll run the code in parallel over all the passed prims. Since we cannot add primvars for calculated uvs in
    // parallel, we'll keep them for all the prims and in the end add them sequentially.
    std::vector<VtVec2fArray> all_face_vertex_uvs;
    all_face_vertex_uvs.resize(num_prims);

    std::vector<VtArray<int>> all_face_vertex_uv_indices;
    all_face_vertex_uv_indices.resize(num_prims);

    tbb::parallel_for(
        tbb::blocked_range<size_t>(size_t(0), num_prims),
        [&](const tbb::blocked_range<size_t>& outerRange)
        {
            auto xformCache = UsdGeomXformCache();

            for (size_t index = outerRange.begin(), indexEnd = outerRange.end(); index < indexEnd; ++index)
            {
                const auto& prim = prims[index];

                // Currently only deals with meshes, but this check could be changed in the future if we want to support
                // other types.
                UsdGeomMesh mesh(prim);
                if (!mesh)
                {
                    continue; // LCOV_EXCL_LINE
                }

                VtVec3fArray points;
                VtIntArray face_vertex_counts;
                VtIntArray face_vertex_indices;

                if (!mesh.GetPointsAttr().Get(&points) || !mesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts) ||
                    !mesh.GetFaceVertexIndicesAttr().Get(&face_vertex_indices))
                {
                    continue; // LCOV_EXCL_LINE
                }

                // Compute the scale vector of the local to world transform matrix. We multiply local positions by these
                // values to get the mesh to its world scale while keeping it in it local space orientation which is
                // often best for projection.
                GfVec3f scale(1, 1, 1);
                if (useWorldSpaceScales)
                {
                    auto l2w = xformCache.GetLocalToWorldTransform(prim);
                    auto s = GfTransform(l2w).GetScale();
                    scale[0] = static_cast<float>(std::abs(s[0]));
                    scale[1] = static_cast<float>(std::abs(s[1]));
                    scale[2] = static_cast<float>(std::abs(s[2]));
                }

                // The number of uvs will be *at most* the same as the number of face-vertices. It could be smaller when
                // common values are made unique via indexing.
                all_face_vertex_uvs[index].resize(face_vertex_indices.size());

                // Pick up the *constant* data pointers from the VtArrays to avoid duplicating data in parallel access
                // by tbb.
                const auto face_vertex_counts_data = face_vertex_counts.cdata();
                const auto face_vertex_indices_data = face_vertex_indices.cdata();
                const auto points_data = points.cdata();

                auto face_vertex_uvs_data = all_face_vertex_uvs[index].data();

                auto num_faces = face_vertex_counts.size();
                auto num_face_vertices = face_vertex_indices.size();

                // Determine the starting index of each face in the face-vertices array, by computing an exclusive
                // parallel scan over face-vertex-counts.
                std::vector<int> face_starts;

                // We only need this for some projection types.
                auto compute_face_starts = [&]()
                {
                    face_starts.resize(num_faces + 1);
                    tbb::parallel_scan(
                        tbb::blocked_range<size_t>(0, num_faces, grain_size),
                        0,
                        [&](const tbb::blocked_range<size_t>& range, int sum, bool final)
                        {
                            int tmp = sum;
                            for (size_t i = range.begin(), ie = range.end(); i != ie; ++i)
                            {
                                tmp += int(face_vertex_counts_data[i]);
                                if (final)
                                {
                                    face_starts[i + 1] = tmp;
                                }
                            }
                            return tmp;
                        },
                        [](int left_sum, int right_sum) { return left_sum + right_sum; });
                };

                // Return the point (position) of a given vertex index. The result is the position given by the points
                // attribute scaled by the local-to-world scales and transformed by the preprojection transform matrix.
                auto get_point = [&](int vertex)
                {
                    auto xyz = points_data[vertex];
                    if (!xform_is_identity)
                    {
                        GfVec4f xyzw(xyz[0], xyz[1], xyz[2], 1);
                        xyzw = xyzw * preprojectionXform;
                        return GfVec3f(scale[0] * xyzw[0] / xyzw[3],
                                       scale[1] * xyzw[1] / xyzw[3],
                                       scale[2] * xyzw[2] / xyzw[3]);
                    }

                    return GfVec3f(scale[0] * xyz[0], scale[1] * xyz[1], scale[2] * xyz[2]);
                };

                // Compute the vector area of a given face index. The "vector area" is the integral of the normal vector
                // over a surface patch. When normalized, it's obviously a good "average normal" for the region. But
                // more importantly, using Stoke's Theorem it is possible to compute the vector area of a region using a
                // line integral around its boundary. What it means for an arbitrary closed polygonal curve in 3-space
                // (read the boundary of mesh polygon) is that it computes a normal for the face without requiring the
                // vertices to lie in the same plane (when they do, the result is the precise normal). Furthermore, it
                // turns out that the magnitude of the resulting vector is a great estimate of the surface area of the
                // polygon without requiring all its vertices to lie in the same plane (when they do, the result is the
                // precise area).
                auto vector_area = [&](size_t face)
                {
                    auto index0 = face_starts[face];
                    auto index1 = face_starts[face + 1];
                    GfVec3f va(0, 0, 0);

                    if (index1 < index0 + 3)
                    {
                        return va; // LCOV_EXCL_LINE
                    }

                    auto v0 = face_vertex_indices_data[index0];
                    auto x0 = get_point(v0);

                    auto v1 = face_vertex_indices_data[index0 + 1];
                    auto x_prev = get_point(v1) - x0;

                    for (auto i = index0 + 2, ie = index1; i < ie; ++i)
                    {
                        auto x = get_point(face_vertex_indices_data[i]) - x0;
                        va += GfCross(x_prev, x);
                        x_prev = x;
                    }

                    return 0.5f * va;
                };

                // Return the projection of a point x along a given axis to the coordinate plane orthogonal to that
                // axis. For a point with coordinates (x, y, z) this is:
                //   Axis       Projection
                //   0 (X)      (-z, y)
                //   1 (Y)      (-x, z)
                //   2 (Z)      (x, y)
                auto get_projection = [&](const GfVec3f& x, int axis)
                {
                    switch (axis)
                    {
                    case 0:
                        return GfVec2f(-x[2], x[1]);
                    default:
                    case 1:
                        return GfVec2f(-x[0], x[2]);
                    case 2:
                        return GfVec2f(x[0], x[1]);
                    }
                };

                // We need face traversals to compute normals for triplanar and cube projections in order to pick the
                // best projection plane. We also need them for cylindrical and spherical projections in order to 1)
                // compute the total surface area for picking a good radius, and 2) ensure no faces straddles the
                // parameter discontinuity.

                if (projectionType == ProjectionType::eTriplanar || projectionType == ProjectionType::eCube ||
                    projectionType == ProjectionType::eCylindrical || projectionType == ProjectionType::eSpherical)
                {
                    compute_face_starts();
                }

                if (projectionType == ProjectionType::eTriplanar || projectionType == ProjectionType::eCube)
                {

                    auto handle_face = [&](size_t face)
                    {
                        auto n = vector_area(face);

                        // Which component has the largest magnitude? (No need to normalize for that)
                        int max_coord_index = 0;
                        if (std::abs(n[1]) > std::abs(n[0]))
                        {
                            max_coord_index = 1;
                        }
                        if (std::abs(n[2]) > std::abs(n[max_coord_index]))
                        {
                            max_coord_index = 2;
                        }

                        for (int j = face_starts[face], je = face_starts[face + 1]; j < je; j++)
                        {
                            auto v = face_vertex_indices_data[j];
                            auto x = get_point(v);

                            face_vertex_uvs_data[j] = get_projection(x, max_coord_index);
                            if (n[max_coord_index] < 0 && projectionType == ProjectionType::eCube)
                            {
                                face_vertex_uvs_data[j][0] = -face_vertex_uvs_data[j][0];
                            }
                        }
                    };

                    tbb::parallel_for(tbb::blocked_range<size_t>(size_t(0), num_faces, grain_size),
                                      [&](const tbb::blocked_range<size_t>& range)
                                      {
                                          for (size_t i = range.begin(), ie = range.end(); i < ie; ++i)
                                          {
                                              handle_face(i);
                                          }
                                      });
                }
                else if (projectionType == ProjectionType::ePlanar)
                {
                    // Canonically, we project along the Y axis.
                    const int axis = 1;
                    tbb::parallel_for(tbb::blocked_range<size_t>(size_t(0), num_face_vertices, grain_size),
                                      [&](const tbb::blocked_range<size_t>& range)
                                      {
                                          for (size_t i = range.begin(), ie = range.end(); i < ie; ++i)
                                          {
                                              auto v = face_vertex_indices_data[i];
                                              auto x = get_point(v);
                                              face_vertex_uvs_data[i] = get_projection(x, axis);
                                          }
                                      });
                }
                else if (projectionType == ProjectionType::eCylindrical || projectionType == ProjectionType::eSpherical)
                {
                    // The cylindrical and spherical projection types have a "radius" parameter that can be chosen by
                    // the user. Normally the radius is chosen to be 1/(2pi) so that the perimeter of a great circle of
                    // the sphere or an orthogonal section of the cylinder is 1. We wish to pick the radius to minimize
                    // texture distortion. If the input surface is a sphere or a cylinder then the ideal radius is the
                    // corresponding radius of the input mesh. This is also the distance from every surface point to the
                    // cylinder axis or the center of the sphere. Since the axis of the projection cylinder is always
                    // the Y axis and the center of the projection sphere is always the origin, a compelling choice for
                    // the radius is the average of the surface point distances to the Y axis for the cylindrical case,
                    // or the origin for the spherical one. Integrating these distance functions over a triangular face
                    // seems involved. Furthermore, we have no reason to think that the faces of the mesh are all
                    // triangular, meaning their vertices may not even lie on the same plane. A simple substitute is a
                    // discrete integral of the distance function defined over vertices and interpolated linearly on
                    // faces. The computation is a discrete integral using the distances at vertices weighted by their
                    // shares from their incident face areas (face area divided by the number of face vertices).

                    std::vector<float> face_areas(num_faces);
                    std::vector<float> area_weighted_face_vertex_norm(num_face_vertices);

                    tbb::parallel_for(tbb::blocked_range<size_t>(size_t(0), num_faces, grain_size),
                                      [&](const tbb::blocked_range<size_t>& range)
                                      {
                                          for (size_t i = range.begin(), ie = range.end(); i < ie; ++i)
                                          {
                                              float a = vector_area(i).GetLength();
                                              face_areas[i] = a;

                                              int index0 = face_starts[i];
                                              int index1 = face_starts[i + 1];

                                              a /= float(index1 - index0);
                                              for (auto j = index0; j < index1; j++)
                                              {
                                                  auto p = get_point(face_vertex_indices_data[j]);
                                                  auto norm = p[0] * p[0] + p[2] * p[2];
                                                  if (projectionType == ProjectionType::eSpherical)
                                                  {
                                                      norm += p[1] * p[1];
                                                  }
                                                  area_weighted_face_vertex_norm[j] = a * sqrt(norm);
                                              }
                                          }
                                      });

                    // We do the summations sequentially (to ensure reproducible round-ups) and sum into a double total
                    // for better numerical accuracy.
                    double total_area(0);
                    for (size_t j = 0; j < num_faces; j++)
                    {
                        total_area += face_areas[j];
                    }

                    double total_norm(0);
                    for (size_t j = 0; j < num_face_vertices; j++)
                    {
                        total_norm += area_weighted_face_vertex_norm[j];
                    }

                    auto radius = total_area == 0 ? 1.f : float(total_norm / total_area);

                    if (!std::isfinite(radius))
                    {
                        radius = 1.f; // LCOV_EXCL_LINE
                    }

                    // Calculate the UV coordinates for the given face index.
                    auto handle_face = [&](size_t face)
                    {
                        // We call the U value obtained from a point x "bad" if x lies on the Y axis.
                        // We count the number of good and bad U values in the face.
                        int num_good_us = 0, num_bad_us = 0;

                        // If there are any bad U values, they will be substituted with the average of the good ones
                        // in the same face.
                        float good_us_average = 0;

                        // We run over the face vertices twice: in the first round we compute the U and V values and
                        // see if there are any bad U values. If so, in a second round we set those to the average of
                        // the good ones encountered.
                        for (int pass : { 0, 1 })
                        {
                            float prev_u = 0;
                            for (int j0 = face_starts[face], j = j0, je = face_starts[face + 1]; j < je; j++)
                            {
                                auto vtx = face_vertex_indices_data[j];
                                auto point = get_point(vtx);
                                auto x = point[0], z = point[2];
                                if (x * x + z * z < std::numeric_limits<float>::epsilon())
                                {
                                    // LCOV_EXCL_START
                                    if (!pass)
                                    {
                                        num_bad_us++;
                                        continue;
                                    }
                                    // LCOV_EXCL_STOP
                                }
                                else if (pass)
                                {
                                    continue; // LCOV_EXCL_LINE
                                }

                                float u;

                                if (pass)
                                {
                                    u = good_us_average;
                                }
                                else
                                {
                                    // We compute a variant of unit-perimeter U (-0.5..0.5 range) so that we can easily
                                    // track jumps of greater than 0.5 along the U axis and avoid them by adjusting by a
                                    // unit amount. In the end we multiply the U values by the calculated radius times
                                    // 2pi.
                                    u = std::atan2(point[0], point[2]) * inv_2pi;
                                    if (u >= 0.5)
                                    {
                                        u -= 1; // LCOV_EXCL_LINE
                                    }

                                    u += std::round(prev_u - u);
                                    prev_u = u;
                                    num_good_us++;
                                    good_us_average += u;
                                }

                                // For cylindrical projection, the V value is just the Y coordinate.
                                auto v = point[1];
                                if (projectionType == ProjectionType::eSpherical)
                                {
                                    // Spherical V values are directly computed for the target radius.
                                    auto s = v / point.GetLength();
                                    s = std::min(s, 1.0f);
                                    s = std::max(s, -1.0f);
                                    v = std::asin(s) * radius;
                                    if (!std::isfinite(v))
                                    {
                                        v = 0; // LCOV_EXCL_LINE
                                    }
                                }

                                face_vertex_uvs_data[j][0] = u * radius * float(2 * M_PI);
                                face_vertex_uvs_data[j][1] = v;
                            }

                            // Only need the second round if we had failed u calculations.
                            if (num_bad_us)
                            {
                                good_us_average /= float(std::max(1, num_good_us)); // LCOV_EXCL_LINE
                            }
                            else
                            {
                                break;
                            }
                        }
                    };

                    tbb::parallel_for(tbb::blocked_range<size_t>(size_t(0), num_faces, grain_size),
                                      [&](const tbb::blocked_range<size_t>& range)
                                      {
                                          for (size_t i = range.begin(), ie = range.end(); i < ie; ++i)
                                          {
                                              handle_face(i);
                                          }
                                      });
                }

                if (!make_indexed)
                {
                    continue;
                }

                using std::make_tuple;
                using std::get;

                // Create the UV vertex index.
                // To generate indices, we want to assign a unique index to all face-vertices that are
                //  1) mapped to the same vertex, and
                //  2) have the same UV coordinates.
                // For this, we sort a list of tuples lexicographically and scan over the sorted list to determine
                // the desired indices for each face-vertex.

                using Tuple = std::tuple<int, GfVec2f, int>;
                std::vector<Tuple> tuples(num_face_vertices);
                for (size_t j = 0, je = num_face_vertices; j < je; j++)
                {
                    tuples[j] = make_tuple(face_vertex_indices_data[j], face_vertex_uvs_data[j], int(j));
                }

                tbb::parallel_sort(tuples.begin(),
                                   tuples.end(),
                                   [&](const Tuple& a, const Tuple& b)
                                   {
                                       return get<0>(a) == get<0>(b) ? get<1>(a)[0] == get<1>(b)[0] ?
                                                                       get<1>(a)[1] < get<1>(b)[1] :
                                                                       get<1>(a)[0] < get<1>(b)[0] :
                                                                       get<0>(a) < get<0>(b);
                                   });


                all_face_vertex_uv_indices[index].resize(num_face_vertices);
                auto uv_index_data = all_face_vertex_uv_indices[index].data();

                int idx = -1;
                for (size_t j = 0, je = num_face_vertices; j < je; j++)
                {
                    if (!j || get<0>(tuples[j]) != get<0>(tuples[j - 1]) || get<1>(tuples[j]) != get<1>(tuples[j - 1]))
                    {
                        face_vertex_uvs_data[++idx] = get<1>(tuples[j]);
                    }

                    uv_index_data[get<2>(tuples[j])] = idx;
                }
                all_face_vertex_uvs[index].resize(idx + 1);
            }
        });

    // Run over all prims and create the UV primvars with the conents computed above.

    size_t i = 0;
    for (const auto& prim : prims)
    {
        // Skip this prim if no uvs were generated
        if (all_face_vertex_uvs[i].empty())
        {
            // LCOV_EXCL_START
            ++i;
            continue;
            // LCOV_EXCL_STOP
        }

        UsdGeomPrimvarsAPI primvarsAPI(prim);
        auto uv_coords = primvarsAPI.CreatePrimvar(_tokens->primvarsUVs,
                                                   SdfValueTypeNames->TexCoord2fArray,
                                                   UsdGeomTokens->faceVarying);

        uv_coords.Set(all_face_vertex_uvs[i]);

        if (make_indexed)
        {
            uv_coords.SetIndices(all_face_vertex_uv_indices[i]);
        }
        else
        {
            // Ensure that the primvar is not indexed if we did not compute indices ourselves
            if (uv_coords.IsIndexed())
            {
                uv_coords.BlockIndices();
            }
        }

        ++i;
    }
}


OperationResult GenerateProjectionUVsOperation::executeImpl()
{
    // Resolve paths to prims
    constexpr bool meshesOnly = true;
    constexpr bool reverse = true;
    Usd_PrimFlagsPredicate predicate = UsdPrimAllPrimsPredicate;

    std::vector<UsdPrim> prims =
        _resolveExpressionsToPrims(getUsdStage(), m_meshPrimPaths, meshesOnly, reverse, predicate);

    // only process prototypes and instances if no paths were input
    // if paths are input, we operate only on those prims
    if (m_meshPrimPaths.empty())
    {
        _insertAbstractMeshes(getUsdStage(), prims);
    }

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
        SO_LOG_INFO("No prims to process");
        return { true };
    }

    // Prepare pre-projection matrix. If empty, default to identity
    if (m_xformMatrixEntries.empty())
    {
        m_xformMatrixEntries = { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
    }

    // Verify the correct number of floats
    if (m_xformMatrixEntries.size() != 16)
    {
        SO_LOG_ERROR("Invalid preprojection xform");
        return { false };
    }

    // If there is a scale factor, apply it
    float scaleValue = _calculteUVScaleValue(getUsdStage(), m_scaleFactor, m_scaleUnits);
    if (scaleValue != 1.0)
    {
        for (size_t i = 0; i < 12; ++i)
        {
            m_xformMatrixEntries[i] *= scaleValue;
        }
    }

    // Cast to matrix
    GfMatrix4f preprojectionXform(*reinterpret_cast<const float(*)[4][4]>(m_xformMatrixEntries.data()));

    // Call the actual generate function with the final args
    _generateProjectionUVs(validPrims, m_projectionType, m_useWorldSpaceScales, preprojectionXform);

    return { true };
}


} // namespace omni::scene::optimizer
