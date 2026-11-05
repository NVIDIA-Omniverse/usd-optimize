// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <optional>
#include <usd_optimize/core/UsdIncludes.h>

namespace usd_optimize::physics_body_meshes
{

/// Explicit mass properties in the output collider's local physics frame.
struct MassProperties
{
    float mass = 0.0f;
    PXR_NS::GfVec3f centerOfMass = PXR_NS::GfVec3f(0.0f);
    PXR_NS::GfVec3f diagonalInertia = PXR_NS::GfVec3f(0.0f);
    PXR_NS::GfQuatf principalAxes = PXR_NS::GfQuatf(1.0f);
};

/// An empty property value with no skip reason means a complete body override already preserves mass behavior.
struct MassMergeResult
{
    std::optional<MassProperties> properties;
    std::string skipReason;
};

/// Combine explicit collider mass properties with USD's rotation and parallel-axis utilities.
/// Preserve complete body overrides. Report unresolved or invalid inputs instead of inventing implicit mass data.
/// Inertias within USD 26.08's tolerance of zero are implicit for both colliders and body overrides.
MassMergeResult combineMassProperties(const PXR_NS::UsdPrim& body,
                                      const PXR_NS::UsdPrim& outputParent,
                                      const std::vector<PXR_NS::UsdPrim>& colliders);

/// Author the combined mass, centre of mass, diagonal inertia, and principal axes on a new collider.
void authorMassProperties(const PXR_NS::UsdPrim& collider, const MassProperties& properties);

} // namespace usd_optimize::physics_body_meshes
