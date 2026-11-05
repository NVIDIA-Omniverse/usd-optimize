// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "ColliderProperties.h"

#include "MaterialBindings.h"

#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdPhysics/collisionAPI.h>
#include <pxr/usd/usdPhysics/massAPI.h>
#include <pxr/usd/usdPhysics/meshCollisionAPI.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>
#include <pxr/usd/usdPhysics/tokens.h>

#include <algorithm>
#include <set>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{
namespace
{

/// Identify settings owned by collision approximation methods.
bool _approximationSetting(const std::string& name)
{
    return TfStringStartsWith(name, "physxConvexHullCollision:") ||
           TfStringStartsWith(name, "physxConvexDecompositionCollision:") ||
           TfStringStartsWith(name, "physxSDFMeshCollision:") || TfStringStartsWith(name, "physxTriangleMeshCollision:");
}

/// Identify APIs that configure collision approximation methods.
bool _approximationSchema(const std::string& name)
{
    return name == "PhysxConvexHullCollisionAPI" || name == "PhysxConvexDecompositionCollisionAPI" ||
           name == "PhysxSDFMeshCollisionAPI" || name == "PhysxTriangleMeshCollisionAPI";
}

/// Identify APIs that must stay on the retained body or mass frame.
bool _bodyOrMassSchema(const std::string& name)
{
    return name.find("RigidBody") != std::string::npos || name.find("Articulation") != std::string::npos ||
           name.find("MassAPI") != std::string::npos;
}

/// Resolve, sort, and deduplicate forwarded simulation-owner targets.
SdfPathVector _simulationOwners(const UsdPrim& prim)
{
    SdfPathVector owners;
    const auto relationship = prim.GetRelationship(UsdPhysicsTokens->physicsSimulationOwner);
    if (relationship)
    {
        relationship.GetForwardedTargets(&owners);
    }
    std::sort(owners.begin(), owners.end());
    owners.erase(std::unique(owners.begin(), owners.end()), owners.end());
    return owners;
}

} // namespace

/// Identify USD physics and PhysX properties by namespace.
bool isPhysicsProperty(const std::string& name)
{
    return TfStringStartsWith(name, "physics:") || TfStringStartsWith(name, "physx");
}

/// Identify USD physics and PhysX schemas by name prefix.
bool isPhysicsSchema(const std::string& name)
{
    return TfStringStartsWith(name, "Physics") || TfStringStartsWith(name, "Physx");
}

/// Collect effective and authored physics APIs, including unavailable plugin schemas.
TfTokenVector physicsSchemas(const UsdPrim& prim)
{
    std::set<TfToken> schemas;
    /// Keep physics API names while discarding unrelated applied schemas.
    const auto add = [&](const TfToken& schema)
    {
        if (isPhysicsSchema(schema.GetString()))
        {
            schemas.insert(schema);
        }
    };
    for (const auto& schema : prim.GetAppliedSchemas())
    {
        add(schema);
    }
    SdfTokenListOp authored;
    if (prim.GetMetadata(UsdTokens->apiSchemas, &authored))
    {
        for (const auto& schema : authored.GetAppliedItems())
        {
            add(schema);
        }
    }
    return { schemas.begin(), schemas.end() };
}

/// Resolve collider behavior, materials, and approximation values for grouping and output.
ColliderProperties resolveColliderProperties(const UsdPrim& collider, const UsdPrim& body, bool convexHullFallback)
{
    ColliderProperties result;
    result.approximation = UsdPhysicsTokens->none;
    const auto approximation = collider.GetAttribute(UsdPhysicsTokens->physicsApproximation);
    if (approximation)
    {
        approximation.Get(&result.approximation);
    }
    result.approximationSettings["method"] = VtValue(result.approximation);
    // The rigid body's simulation owner, including its default owner, overrides collider-local owners.
    result.simulationOwners = _simulationOwners(body);
    result.compatibility["simulationOwners"] = VtValue(result.simulationOwners);
    result.compatibility["materials"] = VtValue(physicsMaterials(collider));

    const auto massAttrs = UsdPhysicsMassAPI::GetSchemaAttributeNames(false);
    const auto bodyAttrs = UsdPhysicsRigidBodyAPI::GetSchemaAttributeNames(false);
    std::set<TfToken> excluded(massAttrs.begin(), massAttrs.end());
    excluded.insert(bodyAttrs.begin(), bodyAttrs.end());
    for (const auto& attr : collider.GetAttributes())
    {
        const auto& name = attr.GetName().GetString();
        if (!isPhysicsProperty(name) || excluded.count(attr.GetName()) || TfStringStartsWith(name, "physxRigidBody:") ||
            TfStringStartsWith(name, "physxArticulation:") || TfStringStartsWith(name, "physxCookedData:"))
        {
            continue;
        }
        VtValue value;
        if (!attr.Get(&value))
        {
            if (attr.HasAuthoredValueOpinion())
            {
                result.skipReason = "blocked or unreadable physics property: " + attr.GetPath().GetAsString();
            }
            continue;
        }
        result.attributes.emplace(attr.GetName(), attr);
        const bool method = attr.GetName() == UsdPhysicsTokens->physicsApproximation;
        if (_approximationSetting(name))
        {
            result.approximationSettings[name] = value;
        }
        if (!method && (!convexHullFallback || !_approximationSetting(name)))
        {
            result.compatibility[name] = value;
        }
    }
    TfTokenVector schemas;
    for (const auto& schema : physicsSchemas(collider))
    {
        const auto& name = schema.GetString();
        if (_bodyOrMassSchema(name) || TfStringStartsWith(name, "PhysxCookedDataAPI"))
        {
            continue;
        }
        result.schemas.push_back(schema);
        if (_approximationSchema(name))
        {
            result.approximationSettings["schema:" + name] = VtValue(true);
        }
        if (name != "PhysicsMeshCollisionAPI" && (!convexHullFallback || !_approximationSchema(name)))
        {
            schemas.push_back(schema);
        }
    }
    result.compatibility["schemas"] = VtValue(schemas);
    if (!convexHullFallback)
    {
        result.compatibility["approximation"] = VtValue(result.approximation);
    }
    return result;
}

/// Restore collider settings and simulation ownership on a merged guide mesh.
void authorColliderProperties(const UsdPrim& output, const ColliderProperties& properties, bool useConvexHull)
{
    for (const auto& schema : properties.schemas)
    {
        if (!useConvexHull || !_approximationSchema(schema.GetString()))
        {
            output.AddAppliedSchema(schema);
        }
    }
    UsdPhysicsCollisionAPI::Apply(output);
    UsdPhysicsMeshCollisionAPI::Apply(output).CreateApproximationAttr().Set(
        useConvexHull ? UsdPhysicsTokens->convexHull : properties.approximation);
    for (const auto& [name, source] : properties.attributes)
    {
        if (name == UsdPhysicsTokens->physicsApproximation || (useConvexHull && _approximationSetting(name.GetString())))
        {
            continue;
        }
        VtValue value;
        source.Get(&value);
        const auto attribute =
            output.CreateAttribute(name, source.GetTypeName(), source.IsCustom(), source.GetVariability());
        if (!attribute.Set(value))
        {
            throw std::runtime_error("Cannot preserve collider property: " + source.GetPath().GetAsString());
        }
    }
    if (!properties.simulationOwners.empty())
    {
        UsdPhysicsCollisionAPI(output).CreateSimulationOwnerRel().SetTargets(properties.simulationOwners);
    }
    UsdGeomImageable(output).CreatePurposeAttr().Set(UsdGeomTokens->guide);
}

} // namespace usd_optimize::physics_body_meshes
