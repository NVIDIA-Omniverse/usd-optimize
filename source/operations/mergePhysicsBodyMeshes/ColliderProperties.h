// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <map>
#include <usd_optimize/core/UsdIncludes.h>

namespace usd_optimize::physics_body_meshes
{

/// Whether a property belongs to the USD physics or PhysX namespaces.
bool isPhysicsProperty(const std::string& name);

/// Whether a schema name uses the USD physics or PhysX prefix.
bool isPhysicsSchema(const std::string& name);

/// Composed physics APIs, including authored PhysX APIs when their plugins are not loaded.
PXR_NS::TfTokenVector physicsSchemas(const PXR_NS::UsdPrim& prim);

/// Resolved collision behavior and the source properties needed to author it on a merged collider.
struct ColliderProperties
{
    PXR_NS::VtDictionary compatibility;
    PXR_NS::VtDictionary approximationSettings;
    std::map<PXR_NS::TfToken, PXR_NS::UsdAttribute> attributes;
    PXR_NS::TfTokenVector schemas;
    PXR_NS::SdfPathVector simulationOwners;
    PXR_NS::TfToken approximation;
    std::string skipReason;
};

/// Resolve materials and simulation ownership and compare local schema values, including schema defaults.
/// Approximation-specific settings can be left out of compatibility when a convex-hull fallback is permitted.
ColliderProperties resolveColliderProperties(const PXR_NS::UsdPrim& collider,
                                             const PXR_NS::UsdPrim& body,
                                             bool convexHullFallback);

/// Author resolved behavior on new geometry and discard stale cooked collision caches.
void authorColliderProperties(const PXR_NS::UsdPrim& output, const ColliderProperties& properties, bool useConvexHull);

} // namespace usd_optimize::physics_body_meshes
