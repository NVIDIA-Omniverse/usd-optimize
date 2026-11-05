// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "PrimPhysicsInfo.h"

#include <pxr/usd/usdPhysics/tokens.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{

/// Retain the prim for the first effective-schema query.
PrimPhysicsInfo::PrimPhysicsInfo(const UsdPrim& prim)
    : m_prim(prim)
{
}

/// Return the cached rigid body API flag.
bool PrimPhysicsInfo::isRigidBody() const
{
    return _getSchemas().rigidBody;
}

/// Return the cached articulation root API flag.
bool PrimPhysicsInfo::isArticulationRoot() const
{
    return _getSchemas().articulationRoot;
}

/// Return the cached collision API flag.
bool PrimPhysicsInfo::isCollider() const
{
    return _getSchemas().collider;
}

/// Read the prim's effective schemas once and reuse all physics API flags on subsequent queries.
const PrimPhysicsInfo::AppliedSchemas& PrimPhysicsInfo::_getSchemas() const
{
    if (!m_schemas)
    {
        AppliedSchemas schemas;
        for (const auto& schema : m_prim.GetAppliedSchemas())
        {
            schemas.rigidBody |= schema == UsdPhysicsTokens->PhysicsRigidBodyAPI;
            schemas.articulationRoot |= schema == UsdPhysicsTokens->PhysicsArticulationRootAPI;
            schemas.collider |= schema == UsdPhysicsTokens->PhysicsCollisionAPI;
        }
        m_schemas = schemas;
    }
    return *m_schemas;
}

} // namespace usd_optimize::physics_body_meshes
