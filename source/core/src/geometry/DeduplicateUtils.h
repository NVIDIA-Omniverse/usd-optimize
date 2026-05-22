// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"

// C++
#include <vector>


namespace omni::scene::optimizer
{

// Typedefs
using PrimVector = std::vector<PXR_NS::UsdPrim>;
using PrimVectors = std::vector<PrimVector>;

/// Find sets of equal mesh prims within prims.
/// In case considerDeepTransforms is set to true, equality is up to a linear transform.
/// In case ignoreNormals is set to true, the normal values are ignored.
OMNI_SO_EXPORT
PrimVectors _computeEqualMeshPrims(const PrimVector& prims,
                                   bool considerDeepTransforms,
                                   float worldSpaceTolerance,
                                   bool ignoreNormals = false);

/// Function to compute a transform that is intrinsic to the mesh.
/// The transform maps the coordinate frame to a sufficiently linear independent set of points of the mesh.
/// The selected set of points depends on the order in which the points are provided and is therefore independent of
/// the current position of the mesh in space. Therefore, meshes that are identical up to a deep transform will select
/// the same set of points. Thus the resulting matrices can be combined to map two such meshes onto each other.
OMNI_SO_EXPORT
PXR_NS::GfMatrix4d _getOriginToPivotMatrix(const PXR_NS::VtArray<PXR_NS::GfVec3f>& points);

/// Find almost-equal sets of meshes.
///
/// Similar to \ref _computeEqualMeshPrims, but the meshes do not have to be exactly the same - different
/// vertices/connectivity are allowed.
///
/// \param prims A set of prims to test for duplicates
/// \param tolerance The allowable tolerance between vertices
/// \param allowScaling Allow scaling
/// \param useGpu Use GPU instead of CPU
OMNI_SO_EXPORT
PrimVectors _computeEqualMeshPrimsFuzzy(const PrimVector& prims, float tolerance, bool allowScaling, bool useGpu);


} // namespace omni::scene::optimizer
