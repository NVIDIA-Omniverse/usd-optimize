// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/DeduplicateUtils.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/CudaUtils.h"
#include "omni/scene.optimizer/core/MeshToolsCommon.h"
#include "omni/scene.optimizer/core/TransformUtils.h"
#include "omni/scene.optimizer/core/Utils.h"

// Mesh Tools
#include "MeshTools/DuplicationChecker.h"

// TBB
#include <tbb/parallel_for.h>

// USD
#include "pxr/usd/usdGeom/curves.h"

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


namespace omni::scene::optimizer
{

/// Typedefs
using MeshSpecIndices = std::vector<size_t>;


GfMatrix4d _getOriginToPivotMatrix(const VtArray<GfVec3f>& points)
{
    // Compute helper matrix to identify equal meshes up to deep transform.
    // The following matrix maps the origin on to the first point of the mesh
    // And the unit vectors onto the next three linear independent points.
    // Not that vectors are not normalized or made orthogonal on purpose as this
    // allows us to support any linear transform, s.a. scaling or shearing.
    GfMatrix4d matrix(1.0);
    if (points.empty())
    {
        return matrix; // LCOV_EXCL_LINE
    }

    const auto& pivot = points[0];

    GfVec3f row_0 = _computePseudoMaxVec3f(pivot, points);
    GfVec3f row_1 = _computePseudoMaxVec3f(pivot, row_0, points);
    GfVec3f row_2 = _computePseudoMaxVec3f(pivot, row_0, row_1, points);

    // Matrix going to store the orthonormal basis vectors.
    GfMatrix3d span(0.);
    span.SetRow(0, row_0);
    span.SetRow(1, row_1);
    span.SetRow(2, row_2);

    // In case the mesh is planar, or nearly planar, we use the cross product of the first two vectors as the third.
    // We *consider* a mesh as planar if the projection (determinant) of the third vector on the cross product is
    // significantly shorter than that cross product.
    auto alt_row_2 = GfCross(span.GetRow(0), span.GetRow(1));
    if (std::fabs(span.GetDeterminant()) <= (0.0000001 * alt_row_2.GetLength()))
    {
        span.SetRow(2, alt_row_2);
    }

    // Generating the 4x4 transformation matrix.
    matrix.SetTransform(span, pivot);

    if (matrix.GetDeterminant() == 0)
    {
        return GfMatrix4d(1.0);
    }

    return matrix;
}


// Struct to help with the identification of identical meshes.
struct MeshSpec
{
    MeshSpec()
    {
    }

    // Disable copying
    MeshSpec(const MeshSpec& other) = delete;

    // Provide noexcept move constructor for vector operations
    MeshSpec(const MeshSpec&& other) noexcept
        : hash(std::move(other.hash))
        , points(std::move(other.points))
        , normals(std::move(other.normals))
        , localToWorld(std::move(other.localToWorld))
        , deepTransform(std::move(other.deepTransform))
    {
    }

    // Hash of mesh topology.
    // If this value differs between meshes than it is not possible for them to be equal.
    size_t hash;

    // Default value of points.
    // The values have not been sanitised so it is not guarenteed taht all values are actually used in the mesh.
    VtArray<GfVec3f> points;

    // Default value of normals.
    // The values have not been conformed into a consistent interpolation.
    VtArray<GfVec3f> normals;

    // Local to world transform matrix for the source prim.
    // This is used to transform point positions into world space for tolerance checks.
    GfMatrix4d localToWorld;

    // Origin to pivot matrix computed from the points.
    // This is used to compare meshes that have had deep transforms applied to the points.
    GfMatrix4d deepTransform;
};


// Unary function to access a mesh spec hash by index.
struct MeshSpecHash
{
    MeshSpecHash(const std::vector<MeshSpec>& meshSpecs)
        : m_meshSpecs(meshSpecs){};

    std::size_t operator()(size_t index) const
    {
        return m_meshSpecs[index].hash;
    }

private:
    const std::vector<MeshSpec>& m_meshSpecs;
};


// Binary function to compare two mesh specs from the list supplied during construction.
// First verify that the topology hash of both prims are equal to ensure comparrison is needed.
// If this the case, ensure that the point and normal values are equal within a tolerance.
// The tolerance value is in stage units and is checked in world space.
struct MeshSpecEqual
{
    MeshSpecEqual(const std::vector<MeshSpec>& meshSpecs, float tolerance)
        : m_meshSpecs(meshSpecs)
        , m_toleranceSq(tolerance * tolerance){};

    bool verifyTransformFrom1To2(const VtArray<GfVec3f>& points1,
                                 const VtArray<GfVec3f>& points2,
                                 const GfMatrix4d& transformFrom1To2,
                                 const GfMatrix4d& localToWorld2) const
    {
        for (size_t i = 0; i < points1.size(); ++i)
        {
            GfVec3d point1 = points1[i];
            GfVec3d point2 = points2[i];

            // Mapping the points of prim1 into the local space of prim2.
            point1 = transformFrom1To2.Transform(point1);

            // Since the localToWorld transform may contain non uniform scaling we can not compute a valid tolerance in
            // local space. We therefore map the points into world space before comparing. Since we mapped points1 into
            // local space of prim2, we apply to localToWorld matrix for prim2.
            point1 = localToWorld2.Transform(point1);
            point2 = localToWorld2.Transform(point2);

            // Compare the squared length as this avoids computing the square root hidden in the length computation.
            if ((point1 - point2).GetLengthSq() >= m_toleranceSq)
            {
                return false;
            }
        }
        return true;
    }

    bool verifyTransformDirFrom1To2(const VtArray<GfVec3f>& normals1,
                                    const VtArray<GfVec3f>& normals2,
                                    const GfMatrix4d& transformFrom1To2) const
    {
        for (size_t i = 0; i < normals1.size(); ++i)
        {
            // Normalizing the transformed vector to support scaling.
            // In fact both, as attribute entries may not be normalized.
            // Not using worldSpaceTolerance here as normals are normalized.
            if ((transformFrom1To2.TransformDir(normals1[i]).GetNormalized() - normals2[i].GetNormalized()).GetLengthSq() >=
                0.001)
            {
                return false;
            }
        }
        return true;
    }

    bool operator()(size_t index, size_t other) const
    {
        // Early out is the mesh spec hashes are different
        if (m_meshSpecs[index].hash != m_meshSpecs[other].hash)
        {
            return false;
        }

        // The meshes cannot be equal if they have different numbers of points or normals.
        if (m_meshSpecs[index].points.size() != m_meshSpecs[other].points.size())
        {
            return false;
        }

        if (m_meshSpecs[index].normals.size() != m_meshSpecs[other].normals.size())
        {
            return false;
        }

        // Determine the transform matrix between the origin to pivot matrices of the meshes.
        // This will be identity if we are not looking at deep transforms in point/normal values.
        GfMatrix4d transformFrom1To2 = m_meshSpecs[index].deepTransform.GetInverse() * m_meshSpecs[other].deepTransform;
        GfMatrix4d transformFrom2To1 = m_meshSpecs[other].deepTransform.GetInverse() * m_meshSpecs[index].deepTransform;

        // Get point values and compare.
        const VtArray<GfVec3f> points1 = m_meshSpecs[index].points.AsConst();
        const VtArray<GfVec3f> points2 = m_meshSpecs[other].points.AsConst();

        if (!this->verifyTransformFrom1To2(points1, points2, transformFrom1To2, m_meshSpecs[other].localToWorld))
        {
            return false;
        }

        if (!this->verifyTransformFrom1To2(points2, points1, transformFrom2To1, m_meshSpecs[index].localToWorld))
        {
            return false;
        }

        // Get normal values and compare.
        const VtArray<GfVec3f> normals1 = m_meshSpecs[index].normals.AsConst();
        const VtArray<GfVec3f> normals2 = m_meshSpecs[other].normals.AsConst();

        if (!this->verifyTransformDirFrom1To2(normals1, normals2, transformFrom1To2))
        {
            return false;
        }

        if (!this->verifyTransformDirFrom1To2(normals2, normals1, transformFrom2To1))
        {
            return false;
        }

        return true;
    }

private:
    const std::vector<MeshSpec>& m_meshSpecs;
    bool m_considerDeepTransforms;
    float m_toleranceSq;
};

/// Given an array of prims produce an array of populated mesh specs
///
/// \param prims The prims to compute Mesh Specs for.
/// \param normals If true, normals values will be included in the Mesh Spec.
/// \param deepTransform Whether to consider deep transforms
/// \param threads Number of threads to use during population. If 1 the process is linear, if 0 the process is parallel.
/// \return Array of Mesh Specs with the same order as the supplied prims.
static std::vector<MeshSpec> _getMeshSpecsForPrims(const PrimVector& prims, bool normals, bool deepTransform, size_t threads)
{
    size_t count = prims.size();

    // Build a vector of mesh spec structs to hold data about the prims being considered.
    std::vector<MeshSpec> meshSpecs(count);

    // Define a parallel function to populate the mesh spec for each prim in a range.
    auto initMeshSpecFn = [&prims, &meshSpecs, &normals, &deepTransform](tbb::blocked_range<size_t> r)
    {
        // Construct an Xform Cache for use within this chunk.
        UsdGeomXformCache xformCache = UsdGeomXformCache();

        for (size_t i = r.begin(); i < r.end(); ++i)
        {
            const UsdPrim& prim = prims[i];

            // Do not populate the Mesh Spec if the prim is not a valid UsdGeomMesh
            UsdGeomPointBased pointBased(prim);
            if (!pointBased)
            {
                continue; // LCOV_EXCL_LINE
            }

            // Populate the points on the Mesh Spec
            pointBased.GetPointsAttr().Get(&meshSpecs[i].points);

            // Populate normals on the Mesh Spec if requested.
            // By conditionally populating the normals we avoid the cost of getting the value if it is not used in
            // comparisons, but we can always compare normals attribute on the Mesh Spec knowing that if they
            // should not be considered then the array will simply be empty.
            if (normals)
            {
                // If primvar normals are authored get the flattened value of those ...
                if (auto primvar = UsdGeomPrimvar(prim.GetAttribute(_tokens->primvarsNormals)))
                {
                    // Compute the flattened value to account for potential indexing of the primvar.
                    primvar.ComputeFlattened(&meshSpecs[i].normals);
                }
                // ... otherwise get the normals which cannot be indexed so are effectivly flattened.
                // If there is no authored value for normals we will still get an empty array as the fallback value.
                else
                {
                    pointBased.GetNormalsAttr().Get(&meshSpecs[i].normals);
                }
            }

            // Compute the local to world matrix.
            meshSpecs[i].localToWorld = xformCache.GetLocalToWorldTransform(prim);

            // Compute the origin to pivot matrix if requested.
            meshSpecs[i].deepTransform = GfMatrix4d(1.f);
            if (deepTransform)
            {
                // By conditionally computing the origin to pivot matrix we avoid the cost of compute if the value is
                // not used in in compare. The fallback value is an identity matrix so that the transform can be applied
                // even when deep transforms are not considered.
                meshSpecs[i].deepTransform = _getOriginToPivotMatrix(meshSpecs[i].points.AsConst());
            }

            // Determine the topology hash for this prim
            size_t hash = 0;

            // Include type name, to differentiate meshes vs curves etc.
            hash = TfHash::Combine(hash, VtHashValue(prim.GetTypeName()));

            // For certain types we can include extra info in the hash to make sure
            // they are unique
            if (auto mesh = UsdGeomMesh(prim))
            {
                // Get the topology attribute values from the Mesh.
                VtIntArray faceVertexCounts;
                VtIntArray faceVertexIndices;
                mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
                mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

                hash = TfHash::Combine(hash, VtHashValue(faceVertexCounts));
                hash = TfHash::Combine(hash, VtHashValue(faceVertexIndices));
            }
            else if (auto basisCurve = UsdGeomCurves(prim))
            {
                VtIntArray curveVertexCounts;
                basisCurve.GetCurveVertexCountsAttr().Get(&curveVertexCounts);
                hash = TfHash::Combine(hash, VtHashValue(curveVertexCounts));
            }

            // Add the count of points and normals to the hash in case they do not conform to the vertex counts.
            hash = TfHash::Combine(hash, VtHashValue(meshSpecs[i].points.size()));
            hash = TfHash::Combine(hash, VtHashValue(meshSpecs[i].normals.size()));

            // Store the hash.
            meshSpecs[i].hash = hash;
        }
    };

    // Populate mesh specs
    if (threads == 1)
    {
        // Run on a single thread if the requested thread count is one.
        initMeshSpecFn(tbb::blocked_range<size_t>(0, count)); // LCOV_EXCL_LINE
    }
    else
    {
        // Otherwise run in parallel on all available threads.
        // TODO: Test grain sizes for optimal performance.
        size_t grainSize = 20;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, count, grainSize), initMeshSpecFn);
    }


    return meshSpecs;
}


// Find sets of equal mesh prims within filteredPrims.
// In case considerDeepTransforms is set to true, equality is up to a linear transform.
PrimVectors _computeEqualMeshPrims(const PrimVector& prims,
                                   bool considerDeepTransforms,
                                   float worldSpaceTolerance,
                                   bool ignoreNormals)
{
    ScopedTimer timer("computeEqualMeshPrims", "", LogLevel::eDebug);
    constexpr size_t threads = 0;

    // Build a vector of mesh specs to hold data about the prims being considered for equality.
    bool normals(!ignoreNormals);
    std::vector<MeshSpec> meshSpecs = _getMeshSpecsForPrims(prims, normals, considerDeepTransforms, threads);

    // Group the Mesh Specs that have the same topology layout so that we can do the more detailed equality check in
    // parallel. The hash of faceVertexCounts and faceVertexIndices was computed when the Mesh Spec was populated.
    std::unordered_map<size_t, MeshSpecIndices> meshSpecsByHashMap;
    for (size_t index = 0; index < meshSpecs.size(); ++index)
    {
        size_t key = meshSpecs[index].hash;
        auto iter = meshSpecsByHashMap.insert(std::make_pair(key, MeshSpecIndices()));
        iter.first->second.push_back(index);
    }

    // Convert the unordered map into a vector (order is not important) so we can iterate it in parallel.
    // Only add mesh specs where more than one had the given topology and we have something to equality check.
    std::vector<MeshSpecIndices> similarMeshSpecs;
    for (const auto& iter : meshSpecsByHashMap)
    {
        if (iter.second.size() < 2)
        {
            continue;
        }
        similarMeshSpecs.push_back(iter.second);
    }

    // We can compute
    size_t count = similarMeshSpecs.size();
    std::vector<std::vector<MeshSpecIndices>> equalMeshSpecs(count);

    // Construct functions to work with MeshSpec structs in an unordered map
    MeshSpecHash hash(meshSpecs);
    MeshSpecEqual key_equal(meshSpecs, worldSpaceTolerance);

    // Define function to compute the hash value for each mesh spec
    auto compareMeshSpecFn =
        [&similarMeshSpecs, &equalMeshSpecs, &hash, &key_equal](const tbb::blocked_range<size_t>& range)
    {
        for (size_t i = range.begin(); i < range.end(); ++i)
        {
            std::unordered_map<size_t, MeshSpecIndices, MeshSpecHash, MeshSpecEqual> equalPrims(42, hash, key_equal);
            for (size_t meshSpecIndex : similarMeshSpecs[i])
            {
                equalPrims[meshSpecIndex].push_back(meshSpecIndex);
            }

            for (const auto& iter : equalPrims)
            {
                if (iter.second.size() > 1)
                {
                    equalMeshSpecs[i].push_back(iter.second);
                }
            }
        }
    };

    // Populate mesh specs
    if (threads == 1)
    {
        // Run on a single thread if the requested thread count is one.
        compareMeshSpecFn(tbb::blocked_range<size_t>(0, count));
    }
    else
    {
        // Otherwise run in parallel on all available threads.
        // TODO: Test grain sizes for optimal performance.
        size_t grainSize = 20;
        tbb::parallel_for(tbb::blocked_range<size_t>(0, count, grainSize), compareMeshSpecFn);
    }

    // Populate an array of arrays of equal prims by converting the mesh spec indices to their corresponding prims.
    PrimVectors result;

    // Iterate over the parallel results and then the lists of equal mesh spec indices within that.
    for (const std::vector<MeshSpecIndices>& equalIndicesArray : equalMeshSpecs)
    {
        for (const MeshSpecIndices& equalIndices : equalIndicesArray)
        {
            // Construct a vector of equal prims using the equal mesh spec indices as the lookup.
            PrimVector equalPrims;
            for (size_t index : equalIndices)
            {
                equalPrims.push_back(prims[index]);
            }

            // Add the equal prims to the result.
            result.push_back(equalPrims);
        }
    }

    return result;
}


// Find sets of equal mesh prims within filteredPrims using the MeshTools fuzzy checker
PrimVectors _computeEqualMeshPrimsFuzzy(const PrimVector& prims, float tolerance, bool allowScaling, bool useGpu)
{
    ScopedTimer timer("computeEqualMeshPrimsFuzzy", "", LogLevel::eDebug);

    // Create a stage from the provided prims for the duplication checker.
    auto stage = GetStage(nullptr, prims);

    if (!stage)
    {
        return PrimVectors{};
    }

    MeshTools::DuplicationChecker checker;

    // find sets of similar prims given a tolerance
    // if scaling is allowed, a primitive pair is also considered similar if one is a scaled version of the other

    // If true, optimizes towards the volume minimizing OBB using multiple 2d projections.
    // This increases the computation time.
    constexpr bool optimizeOBBs = true;

    // If true, meshes with different vertex counts are not considered similar.
    // This is useful to compare fuzzy (matchVertexCount == false) versus non-fuzzy (matchVertexCount == true) results.
    constexpr bool matchVertexCount = false;

    checker.findDuplicationSets(*stage, tolerance, allowScaling, matchVertexCount, optimizeOBBs, useGpu && isCudaAvailable());
    auto sets = checker.duplicationSets();

    // convert the result into PrimVectors
    PrimVectors result;

    for (size_t setNr = 0; setNr < sets.size(); ++setNr)
    {
        auto& set = sets[setNr];
        PrimVector primsInSet;

        for (size_t primNr : set)
        {
            auto prim = prims[primNr];
            primsInSet.push_back(prim);
        }
        result.push_back(primsInSet);
    }

    return result;
}


} // namespace omni::scene::optimizer
