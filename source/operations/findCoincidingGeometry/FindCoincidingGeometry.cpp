// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "FindCoincidingGeometry.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/bboxCache.h>

// C++
#include <iostream>
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE

struct BBoxData
{
    GfRange3d range;
    double volume = 0.0;
};
using BBoxMap = std::unordered_map<SdfPath, BBoxData, SdfPath::Hash>;

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::FindCoincidingGeometryOperation);


namespace omni::scene::optimizer
{


/// Constants
constexpr const char* s_categoryCoinciding = "COINCIDING";

FindCoincidingGeometryOperation::FindCoincidingGeometryOperation()
    : Operation("findCoincidingGeometry",
                "Find Coincident Geometry",
                "Find geometry that occupies the same positional space in a scene.")
{

    addArgument("primPaths", "Prims To Consider", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_paths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("tolerance",
                "Tolerance",
                kDisplayTypeFloat,
                "Tolerance value when comparing points values in world space",
                m_tolerance);

    addArgument("offset",
                "Offset %",
                kDisplayTypeFloatSlider,
                "An offset to allow prims to be considered coincident. Describes a percentage relative to the prim bounds",
                m_offset)
        .setMin(0.0)
        .setMax(150.0)
        .setStep(1.0);

    addArgument("fuzzy",
                "Fuzzy",
                kDisplayTypeBool,
                "Find geometry that is the same shape but may have different vertex positions/connectivity",
                m_fuzzy);
}


std::string FindCoincidingGeometryOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion FindCoincidingGeometryOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string FindCoincidingGeometryOperation::getCategory() const
{
    return s_categoryCoinciding;
}


bool FindCoincidingGeometryOperation::getSupportsAnalysis() const
{
    return true;
}


// Note that using the subscript operator on a non-const VtArray,
// or accessing the data using a non-const iterator, will
// cause the underlying data to copy / localize to the VtArray instance,
// whereas by default VtArray is just a lightweight pointer to the original
// data in memory. This is not intuitive behavior and often unexpected.
// https://graphics.pixar.com/usd/release/api/class_vt_array.html#details
inline const VtVec3fArray _getConstPointArray(const UsdPrim& prim)
{
    UsdGeomPointBased pointBased(prim);
    VtVec3fArray points;
    pointBased.GetPointsAttr().Get(&points);
    return points.AsConst();
}


static bool _doCoincideFuzzy(const UsdPrim& prim1,
                             const UsdPrim& prim2,
                             const float tolerance,
                             const float toleranceCb,
                             const float offset,
                             const BBoxMap& bboxMap)
{
    auto it1 = bboxMap.find(prim1.GetPath());
    auto it2 = bboxMap.find(prim2.GetPath());
    if (it1 == bboxMap.end() || it2 == bboxMap.end())
    {
        return false;
    }

    const BBoxData& bd1 = it1->second;
    const BBoxData& bd2 = it2->second;

    // First, check volume. Given we want to make sure the meshes are the same
    // size, volume should be a quick filter.
    double volDiff = std::abs(bd1.volume - bd2.volume);
    if (volDiff > toleranceCb)
    {
        return false;
    }

    // If there is an offset, then expand the bbox range to look for coinciding objects
    // that aren't directly on top of each other.
    //
    // Note: this trumps fuzzy mode, since we don't need to do a tolerance-check on
    // the bounds if we allow it being offset anyway. Fuzzy mode will still have found
    // fuzzy duplicates, so it does have an effect. We just allow also using the offset.
    if (offset > 0.0)
    {
        // Next, work out if the second object is "close enough"
        GfRange3d range1 = bd1.range;

        const GfVec3d& min = range1.GetMin();
        const GfVec3d& max = range1.GetMax();

        GfVec3d rangeDiff = max - min;
        const double _offset = offset / 100.0;

        // Adjust the range. Essentially, adding a percentage of the width of each axis to "expand"
        // the range so that the offset is relative to its size.
        rangeDiff[0] *= _offset;
        rangeDiff[1] *= _offset;
        rangeDiff[2] *= _offset;

        range1.SetMin(min - GfVec3d(abs(rangeDiff[0]), abs(rangeDiff[1]), abs(rangeDiff[2])));
        range1.SetMax(max + GfVec3d(abs(rangeDiff[0]), abs(rangeDiff[1]), abs(rangeDiff[2])));

        // Now just see if the centroid of the other object is inside the expanded range.
        GfVec3d centroid2 = (bd2.range.GetMin() + bd2.range.GetMax()) * 0.5;
        if (!range1.Contains(centroid2))
        {
            return false;
        }

        return true;
    }

    // Otherwise for fuzzy mode, check that the world bounds are fuzzily the
    // same.
    const GfVec3d& min1 = bd1.range.GetMin();
    const GfVec3d& max1 = bd1.range.GetMax();

    const GfVec3d& min2 = bd2.range.GetMin();
    const GfVec3d& max2 = bd2.range.GetMax();

    if (!GfIsClose(min1, min2, tolerance) || !GfIsClose(max1, max2, tolerance))
    {
        return false;
    }

    return true;
}


// Returns true if the two provided prims coincide.
static bool _doCoincide(const UsdPrim& prim1,
                        const UsdPrim& prim2,
                        const float toleranceSq,
                        UsdGeomXformCache& xformCache,
                        const BBoxMap& bboxMap)
{
    // Fast bbox rejection: if the world-space bounding boxes don't overlap at all,
    // the meshes can't coincide. This is O(1) vs the O(n) vertex loop below.
    // No tolerance expansion here because the vertex loop compares in local space,
    // and the mapping between local and world tolerance depends on the prim's scale.
    auto it1 = bboxMap.find(prim1.GetPath());
    auto it2 = bboxMap.find(prim2.GetPath());
    if (it1 != bboxMap.end() && it2 != bboxMap.end())
    {
        const BBoxData& bd1 = it1->second;
        const BBoxData& bd2 = it2->second;

        if (!bd1.range.IsEmpty() && !bd2.range.IsEmpty())
        {
            if (GfRange3d::GetIntersection(bd1.range, bd2.range).IsEmpty())
            {
                return false;
            }

            // Volume check: coinciding meshes must occupy the same space, so their
            // world-space volumes must match. This is scale-invariant and catches
            // overlapping but differently-sized meshes before loading any vertex data.
            const double maxVol = std::max(bd1.volume, bd2.volume);
            if (maxVol > 0.0 && std::abs(bd1.volume - bd2.volume) > maxVol * 0.01)
            {
                return false;
            }
        }
    }

    // Unfortunately, we can't just look at the local to world transforms as points may have been subject to deep
    // transforms.
    const VtVec3fArray points1 = _getConstPointArray(prim1);
    if (points1.empty())
    {
        return false;
    }

    const VtVec3fArray points2 = _getConstPointArray(prim2);
    if (points2.empty())
    {
        return false;
    }

    if (points1.size() != points2.size())
    {
        return false;
    }

    GfMatrix4d transform1 = xformCache.GetLocalToWorldTransform(prim1);
    GfMatrix4d transform2 = xformCache.GetLocalToWorldTransform(prim2);

    // Transform mapping the second prim into world coordinates and then back into local space of first prim.
    // This is expected to be numerically more stable for prims that are mapped far away from the origin as
    // we force the large numbers in the matrices cancel out before applying them to point coordinates.
    const GfMatrix4d transformFrom2to1 = transform2 * transform1.GetInverse();

    for (size_t i = 0; i < points1.size(); ++i)
    {
        const auto& p1 = points1[i];
        auto p2 = transformFrom2to1.Transform(points2[i]);

        if ((p1 - p2).GetLengthSq() > toleranceSq)
        {
            return false;
        }
    }

    // Not checking the normals here on purpose, as those are already verified during deduplicate.
    return true;
}


PrimVectors FindCoincidingGeometryOperation::computeCoincidingGeometry(const PrimVectors& equalMeshPrimSets)
{
    UsdGeomXformCache xformCache;

    const float toleranceSq = m_tolerance * m_tolerance;
    const float toleranceCb = toleranceSq * m_tolerance;

    // Pre-compute world-space bounding boxes once per unique prim.
    BBoxMap bboxMap;
    {
        UsdGeomBBoxCache bboxCache(UsdTimeCode::Default(), { UsdGeomTokens->default_ });
        for (const auto& set : equalMeshPrimSets)
        {
            for (const auto& prim : set)
            {
                if (bboxMap.count(prim.GetPath()) == 0)
                {
                    const GfBBox3d bbox = bboxCache.ComputeWorldBound(prim);
                    bboxMap[prim.GetPath()] = { bbox.ComputeAlignedRange(), bbox.GetVolume() };
                }
            }
        }
    }

    PrimVectors result;

    for (const auto& set : equalMeshPrimSets)
    {
        PrimVectors coincidingPrimSets;
        for (const auto& prim : set)
        {
            bool isDefined = prim.IsDefined();
            bool isAbstract = prim.IsAbstract();

            bool foundCoincidingMesh = false;
            for (size_t j = 0; j < coincidingPrimSets.size(); ++j)
            {
                const auto& otherPrim = coincidingPrimSets[j][0];

                // Consider defined vs non-defined or abstract vs non-abstract to not be coinciding, to avoid comparing
                // instances/inherits to their definition (assuming for example a geometry library).
                if (otherPrim.IsDefined() != isDefined || otherPrim.IsAbstract() != isAbstract)
                {
                    continue;
                }

                bool didCoincide = false;
                if (m_fuzzy || m_offset > 0.0)
                {
                    didCoincide = _doCoincideFuzzy(otherPrim, prim, m_tolerance, toleranceCb, m_offset, bboxMap);
                }
                else
                {
                    didCoincide = _doCoincide(otherPrim, prim, toleranceSq, xformCache, bboxMap);
                }

                if (didCoincide)
                {
                    coincidingPrimSets[j].push_back(prim);
                    foundCoincidingMesh = true;
                    break;
                }
            }

            if (!foundCoincidingMesh)
            {
                coincidingPrimSets.emplace_back(1, prim);
            }
        }

        // Copy any sets with multiple (coinciding) prims to the result, filtering
        // out the ones that are not coinciding.
        for (const auto& coincidingSet : coincidingPrimSets)
        {
            if (coincidingSet.size() > 1)
            {
                result.push_back(coincidingSet);
            }
        }
    }

    return result;
}


/// Returns true if the prim is supported by the coinciding mesh checks
inline bool _isSupportedPrim(const UsdPrim& prim, const bool fuzzy)
{
    // We cannot read from invalid prims.
    if (!prim.IsValid())
    {
        return false;
    }

    // For fuzzy, only meshes are supported.
    if (fuzzy)
    {
        return prim.IsA<UsdGeomMesh>();
    }

    // Otherwise anything point based is supported.
    return prim.IsA<UsdGeomPointBased>();
}


OperationResult FindCoincidingGeometryOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|FindCoincidingGeometry|Execute");

    constexpr bool meshesOnly = false;
    constexpr bool reverse = false;
    Usd_PrimFlagsPredicate predicate = UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate);

    // Custom resolve callback to filter out anything that is not point-based.
    auto callback = [&](const UsdPrim& prim, UsdPrimRange::iterator&) -> bool { return _isSupportedPrim(prim, m_fuzzy); };

    // Resolve Expressions.
    const std::vector<UsdPrim>& prims =
        _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(), m_paths, meshesOnly, reverse, predicate, callback);

    // Compute the mesh prims that are equal to one another so that we reduce the number of meshes that we need to
    // check for coincidental meshes.
    PrimVectors equalMeshPrims;

    if (m_fuzzy)
    {
        constexpr bool allowScaling = true;
        constexpr bool useGpu = false;
        equalMeshPrims = _computeEqualMeshPrimsFuzzy(prims, m_tolerance, allowScaling, useGpu);
    }
    else
    {
        constexpr bool considerDeepTransforms = true;
        constexpr bool ignoreNormals = true;
        equalMeshPrims = _computeEqualMeshPrims(prims, considerDeepTransforms, m_tolerance, ignoreNormals);
    }

    // Compute the meshes that coincide in world space
    m_coincidingPrims = computeCoincidingGeometry(equalMeshPrims);

    // Add the lists of coinciding meshes to the report
    if (getContext()->generateReport || getContext()->verbose)
    {
        // Report the number of coinciding mesh sets found.
        size_t count = m_coincidingPrims.size();
        std::string plural = count == 1 ? "" : "s";
        SO_LOG_INFO("Found %zu set%s of coinciding meshes", count, plural.c_str());

        // For each set report the number of coinciding meshes and their paths.
        for (const auto& coincidingPrims : m_coincidingPrims)
        {
            SO_LOG_INFO("Found %zu coinciding meshes", coincidingPrims.size());
            for (const auto& coincidingPrim : coincidingPrims)
            {
                SO_LOG_INFO(coincidingPrim.GetPath().GetAsString().c_str());
            }
        }
    }

    // Create result
    OperationResult result{ true, nullptr, nullptr };

    // how we return the results depends on whether we're in analysis mode or not - this is because the analysis API
    // expects the results to be under the "analysis" key but otherwise the operation runs exactly the same
    if (getContext()->analysisMode != 0)
    {
        JsObject analysisResult;
        analysisResult["coincidingGeometry"] = _toJson(m_coincidingPrims);
        JsObject resultJson;
        resultJson["analysis"] = analysisResult;
        result.output = getCStr(JsWriteToString(resultJson));
    }
    else if (!m_coincidingPrims.empty())
    {
        result.output = _toJsonStr(m_coincidingPrims);
    }

    return result;
}


OperationResult FindCoincidingGeometryOperation::executeAnalysisImpl()
{
    // analysis mode is the same as execute
    return executeImpl();
}


} // namespace omni::scene::optimizer
