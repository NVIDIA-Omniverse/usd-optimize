// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "omni/scene.optimizer/core/ComputeExtents.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/ResolveSdfPaths.h"

// USD
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/points.h>

// Carbonite
#include <carb/profiler/Profile.h>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


static void _getTimeSamplesForAttributes(const UsdPrim& prim,
                                         const TfTokenVector& attributes,
                                         std::vector<UsdTimeCode>& timecodes)
{
    // Get any time samples that exist over any of the required attributes
    std::set<double> uniqueSamples;
    for (const auto& attributeName : attributes)
    {
        const UsdAttribute& attr = prim.GetAttribute(attributeName);
        std::vector<double> samples;
        attr.GetTimeSamples(&samples);
        uniqueSamples.insert(samples.begin(), samples.end());
    }

    // Convert to timecodes. This lets us also check whether there were none and use Default() in place.
    for (auto sample : uniqueSamples)
    {
        timecodes.emplace_back(sample);
    }

    // No time samples, just author default
    if (timecodes.empty())
    {
        timecodes.emplace_back(UsdTimeCode::Default());
    }
}


size_t _computeExtents(const std::vector<UsdPrim>& prims)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|_computeExtents");

    // Wrap in a ChangeBlock.
    SdfChangeBlock changeBlock;

    size_t numPrimsComputed = 0;

    for (const auto& prim : prims)
    {
        // Don't attempt to author on InstanceProxy's
        if (prim.IsInstanceProxy())
        {
            continue;
        }

        // Currently only deals with meshes and points, but this check could be changed in the future if we want
        // to support other types.
        if (!prim.IsA<UsdGeomMesh>() && !prim.IsA<UsdGeomPoints>())
        {
            continue;
        }

        const UsdGeomBoundable& boundable = UsdGeomBoundable(prim);

        // Work out the set of unique time samples authored on this prim.
        TfTokenVector attributes{ UsdGeomTokens->points };

        // Get the widths attr for points - note it could be widths or primvars:widths (latter takes precedence)
        if (prim.IsA<UsdGeomPoints>())
        {
            attributes.push_back(UsdGeomPoints(prim).GetWidthsAttr().GetName());
        }

        // Get the unique timecodes to calculate extent for
        std::vector<UsdTimeCode> timecodes;
        _getTimeSamplesForAttributes(prim, attributes, timecodes);

        // Create or get the extent attr
        auto extentAttr = boundable.CreateExtentAttr();

        // Clear any existing/old time samples
        extentAttr.Clear();

        // Finally compute and set the extent for each time sample
        for (const auto& timecode : timecodes)
        {
            VtVec3fArray extent;
            if (UsdGeomBoundable::ComputeExtentFromPlugins(boundable, timecode, &extent))
            {
                extentAttr.Set(extent, timecode);
            }
        }

        // increment counter if we computed at least one extent
        if (!timecodes.empty())
        {
            ++numPrimsComputed;
        }
    }

    return numPrimsComputed;
}

// Compute and set the extent attribute on the specified meshes.
size_t _computeExtents(const UsdStageWeakPtr& usdStage, const std::vector<std::string>& meshPrimPaths)
{
    // Resolve paths to prims
    bool meshesOnly = false;
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(usdStage, meshPrimPaths, meshesOnly);

    // Compute extents for the prims
    return _computeExtents(prims);
}


std::vector<std::string> _findPrimsMissingExtents(const std::vector<UsdPrim>& prims)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|_findPrimsMissingExtents");

    std::vector<std::string> missing;

    for (const auto& prim : prims)
    {
        if (prim.IsInstanceProxy())
        {
            continue;
        }

        // Mirror the type filter used by _computeExtents.
        if (!prim.IsA<UsdGeomMesh>() && !prim.IsA<UsdGeomPoints>())
        {
            continue;
        }

        const UsdGeomBoundable boundable(prim);
        const UsdAttribute extentAttr = boundable.GetExtentAttr();
        if (!extentAttr || !extentAttr.HasAuthoredValue())
        {
            missing.emplace_back(prim.GetPath().GetAsString());
        }
    }

    return missing;
}


std::vector<std::string> _findPrimsMissingExtents(const UsdStageWeakPtr& usdStage,
                                                  const std::vector<std::string>& meshPrimPaths)
{
    bool meshesOnly = false;
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(usdStage, meshPrimPaths, meshesOnly);
    return _findPrimsMissingExtents(prims);
}

} // namespace omni::scene::optimizer
