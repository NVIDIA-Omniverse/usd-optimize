// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <optional>
#include <usd_optimize/core/UsdIncludes.h>

namespace usd_optimize::physics_body_meshes
{

/// Physics roles of one prim, determined from its effective APIs, including built-in and auto-applied schemas.
/// The first query records all three API flags. Later queries reuse that snapshot and do not reflect prim edits.
/// Keep the prim valid until the first query and create a new object to observe later changes.
class PrimPhysicsInfo
{
public:
    /// Select a valid prim without reading its effective schemas yet.
    explicit PrimPhysicsInfo(const PXR_NS::UsdPrim& prim);

    /// Return whether the cached effective schemas include RigidBodyAPI.
    bool isRigidBody() const;

    /// Return whether the cached effective schemas include ArticulationRootAPI.
    bool isArticulationRoot() const;

    /// Return whether the cached effective schemas include CollisionAPI.
    bool isCollider() const;

private:
    /// Physics APIs present when the prim is first queried.
    struct AppliedSchemas
    {
        bool rigidBody = false;
        bool articulationRoot = false;
        bool collider = false;
    };

    /// Read the prim's effective schemas once and return the cached physics API flags.
    const AppliedSchemas& _getSchemas() const;

    PXR_NS::UsdPrim m_prim;
    mutable std::optional<AppliedSchemas> m_schemas;
};

} // namespace usd_optimize::physics_body_meshes
