// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"


namespace omni::scene::optimizer
{

// The following block of functions each computes a vector that essentially maximizes a certain objective function.
// However, due to symmetries in objects there may be several of such vectors that establish the maximum.
// Thus, in the presence of rounding errors, the selected vector would be essentially random.
// In order to prevent that, we give a slight preference to the vector that we found first.
// Hence the name of the function.

// Compute the pseudo largest diff vector from ref_point.
OMNI_SO_EXPORT
PXR_NS::GfVec3f _computePseudoMaxVec3f(const PXR_NS::GfVec3f& ref_point, const PXR_NS::VtVec3fArray& points);

// Compute the diff vector from ref_point that generates the pseudo largest cross product to ref_vec.
OMNI_SO_EXPORT
PXR_NS::GfVec3f _computePseudoMaxVec3f(const PXR_NS::GfVec3f& ref_point,
                                       const PXR_NS::GfVec3f& ref_vec,
                                       const PXR_NS::VtVec3fArray& points);

// Compute the diff vector from ref_point that generates the largest determinant.
OMNI_SO_EXPORT
PXR_NS::GfVec3f _computePseudoMaxVec3f(const PXR_NS::GfVec3f& ref_point,
                                       const PXR_NS::GfVec3f& row_0,
                                       const PXR_NS::GfVec3f& row_1,
                                       const PXR_NS::VtVec3fArray& points);

// Compute the centroid, that is, the average of the given point.
OMNI_SO_EXPORT
PXR_NS::GfVec3f _computeCentroid(const PXR_NS::VtVec3fArray& points);

} // namespace omni::scene::optimizer
