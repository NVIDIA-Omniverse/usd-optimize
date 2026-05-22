// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "omni/scene.optimizer/core/TransformUtils.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{

// The following block of functions each computes a vector that essentially maximizes a certain objective function.
// However, due to symmetries in objects there may be several of such vectors that establish the maximum.
// Thus, in the presence of rounding errors, the selected vector would be essentially random.
// In order to prevent that, we give a slight preference to the vector that we found first.
// Hence the name of the function.

// This is a epsilon used for relative error.
static constexpr float reps = float(1 + 1E-7);

// Compute the pseudo largest diff vector from ref_point.
GfVec3f _computePseudoMaxVec3f(const GfVec3f& ref_point, const VtVec3fArray& points)
{
    float max_sq_length(0);
    float cur_sq_length(0);
    GfVec3f max_vec(0);
    GfVec3f cur_vec(0);
    for (const auto& cur_point : points)
    {
        cur_vec = cur_point - ref_point;
        cur_sq_length = cur_vec.GetLengthSq();
        if (cur_sq_length > max_sq_length * reps)
        {
            max_vec = cur_vec;
            max_sq_length = cur_sq_length;
        }
    }
    return max_vec;
}

// Compute the diff vector from ref_point that generates the pseudo largest cross product to ref_vec.
GfVec3f _computePseudoMaxVec3f(const GfVec3f& ref_point, const GfVec3f& ref_vec, const VtVec3fArray& points)
{
    float max_sq_length(0);
    float cur_sq_length(0);
    GfVec3f max_vec(0);
    GfVec3f cur_vec(0);
    for (const auto& cur_point : points)
    {
        cur_vec = cur_point - ref_point;
        cur_sq_length = GfCross(cur_vec, ref_vec).GetLengthSq();
        if (cur_sq_length > max_sq_length * reps)
        {
            max_vec = cur_vec;
            max_sq_length = cur_sq_length;
        }
    }
    return max_vec;
}

// Compute the diff vector from ref_point that generates the largest determinant.
GfVec3f _computePseudoMaxVec3f(const GfVec3f& ref_point,
                               const GfVec3f& row_0,
                               const GfVec3f& row_1,
                               const VtVec3fArray& points)
{
    float max_abs_det(0);
    float cur_abs_det(0);
    GfVec3f max_vec(0);
    GfVec3f cur_vec(0);
    // gcov reports the following is not hit but it is because the lines above and below are.
    // LCOV_EXCL_START
    GfMatrix3d span(0.0);
    span.SetRow(0, row_0);
    span.SetRow(1, row_1);
    // LCOV_EXCL_STOP

    for (const auto& cur_point : points)
    {
        cur_vec = cur_point - ref_point;
        span.SetRow(2, cur_vec.GetNormalized());
        cur_abs_det = float(fabs(span.GetDeterminant()));
        if (cur_abs_det > max_abs_det * reps)
        {
            max_vec = cur_vec;
            max_abs_det = cur_abs_det;
        }
    }
    return max_vec;
}


// Compute the centroid, that is, the average of the given point.
GfVec3f _computeCentroid(const VtVec3fArray& points)
{
    const GfVec3f& p0 = points[0];
    GfVec3f centroid(0.f);
    for (const auto& p : points)
    {
        centroid += (p - p0);
    }
    centroid /= double(points.size());
    centroid += p0;

    // The following returns false iff a coordinate is nan.
    return centroid;
}


} // namespace omni::scene::optimizer
