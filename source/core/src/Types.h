// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/UsdIncludes.h"


namespace omni::scene::optimizer
{


/// Helper struct to store color information
struct ColorValue
{
    PXR_NS::GfVec3f color;
    float opacity;
};


/// Struct to track information about a prim that is part of a bucket.
///
/// Contains the prim, cached points (to avoid querying them repeatedly), and
/// offsets that can be used when combining data.
struct PrimData
{

    PrimData(){};

    explicit PrimData(const PXR_NS::UsdPrim& _prim)
        : prim(_prim)
    {
    }

    // Copy constructor
    PrimData(const PrimData& other) = default;

    // noexcept move constructor for vectors
    PrimData(const PrimData&& other) noexcept
        : prim(std::move(other.prim))
        , points(std::move(other.points))
        , faceVertexIndices(std::move(other.faceVertexIndices))
        , faceVertexCounts(std::move(other.faceVertexCounts))
        , worldExtent(std::move(other.worldExtent))
        , localToWorldTransform(std::move(other.localToWorldTransform))
        , pointsOffset(std::move(other.pointsOffset))
        , faceVertexCountsOffset(std::move(other.faceVertexCountsOffset))
        , faceVertexIndicesOffset(std::move(other.faceVertexIndicesOffset))
        , overrides(std::move(other.overrides))
    {
    }

    // Prim
    PXR_NS::UsdPrim prim;

    // Cached data
    PXR_NS::VtArray<PXR_NS::GfVec3f> points;
    PXR_NS::VtArray<int> faceVertexIndices;
    PXR_NS::VtArray<int> faceVertexCounts;

    PXR_NS::VtVec3fArray worldExtent;
    PXR_NS::GfMatrix4d localToWorldTransform;

    // Offsets
    int pointsOffset = 0;
    int faceVertexCountsOffset = 0;
    int faceVertexIndicesOffset = 0;

    // Overrides
    PXR_NS::VtDictionary overrides;
};


/// Struct to hold the merged mesh values
struct MergedValues
{
    // Basic geometry
    PXR_NS::VtArray<int> faceVertexCounts;
    PXR_NS::VtArray<int> faceVertexIndices;
    PXR_NS::VtArray<PXR_NS::GfVec3f> points;
    PXR_NS::VtArray<int> holeIndices;

    // Subdivision Surface
    PXR_NS::VtArray<int> cornerIndices;
    PXR_NS::VtArray<float> cornerSharpnesses;
    PXR_NS::VtArray<int> creaseLengths;
    PXR_NS::VtArray<int> creaseIndices;
    PXR_NS::VtArray<float> creaseSharpnesses;

    // UsdSkel
    PXR_NS::VtArray<PXR_NS::TfToken> jointNames;
    PXR_NS::VtArray<int> jointIndices;
    PXR_NS::VtArray<float> singleKeyJointWeights;

    // Materials
    std::map<PXR_NS::SdfPath, PXR_NS::VtArray<int>> faceMaterials;
};


} // namespace omni::scene::optimizer
