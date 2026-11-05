// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MassProperties.h"

#include <pxr/base/gf/transform.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdPhysics/massAPI.h>
#include <pxr/usd/usdPhysics/massProperties.h>

#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{
namespace
{

/// Return whether all three components are finite.
bool _finite(const GfVec3f& value)
{
    return std::isfinite(value[0]) && std::isfinite(value[1]) && std::isfinite(value[2]);
}

/// Read complete static mass properties, recording when USD would derive inertia instead of using the authored value.
bool _readExplicitMass(const UsdPrim& prim, MassProperties& result, std::string& skipReason)
{
    skipReason.clear();
    if (!prim.HasAPI<UsdPhysicsMassAPI>())
    {
        return false;
    }
    const UsdPhysicsMassAPI mass(prim);
    if (!mass.GetMassAttr().Get(&result.mass) || !mass.GetCenterOfMassAttr().Get(&result.centerOfMass) ||
        !mass.GetDiagonalInertiaAttr().Get(&result.diagonalInertia))
    {
        return false;
    }
    for (const auto& name : UsdPhysicsMassAPI::GetSchemaAttributeNames(false))
    {
        if (prim.GetAttribute(name).ValueMightBeTimeVarying())
        {
            return false;
        }
    }
    GfQuatf axes(0.0f);
    mass.GetPrincipalAxesAttr().Get(&axes);
    const float length = axes.GetLength();
    if (std::isfinite(length) && std::abs(length - 1.0f) < 1e-4f)
    {
        result.principalAxes = axes.GetNormalized();
    }
    else if (length != 0.0f)
    {
        return false;
    }
    if (!std::isfinite(result.mass) || result.mass <= 0.0f || !_finite(result.centerOfMass) ||
        !_finite(result.diagonalInertia) || result.diagonalInertia[0] <= 0.0f || result.diagonalInertia[1] <= 0.0f ||
        result.diagonalInertia[2] <= 0.0f)
    {
        return false;
    }
    // Match USD 26.08 rigidBodyAPI.cpp::_ParseMassApi for collider values and complete body overrides.
    constexpr float inertiaTolerance = 1e-5f;
    if (GfIsClose(result.diagonalInertia, GfVec3f(0.0f), inertiaTolerance))
    {
        skipReason = "diagonal inertia is within USD's implicit-inertia tolerance (1e-5)";
        return false;
    }
    return true;
}

/// Physics mass frames use rotation and translation; scale is applied to the authored centre of mass.
GfMatrix4d _physicsFrame(const GfTransform& transform)
{
    GfMatrix4d frame(1.0);
    frame.SetRotate(transform.GetRotation());
    frame.SetTranslateOnly(transform.GetTranslation());
    return frame;
}

} // namespace

/// Preserve a complete body override or combine explicit collider mass in the output frame.
MassMergeResult combineMassProperties(const UsdPrim& body,
                                      const UsdPrim& outputParent,
                                      const std::vector<UsdPrim>& colliders)
{
    MassProperties bodyProperties;
    std::string bodyIssue;
    if (_readExplicitMass(body, bodyProperties, bodyIssue))
    {
        return {};
    }
    UsdGeomXformCache xforms;
    const GfTransform parentTransform(xforms.GetLocalToWorldTransform(outputParent));
    const auto parentScale = parentTransform.GetScale();
    for (int axis = 0; axis != 3; ++axis)
    {
        if (!std::isfinite(parentScale[axis]) || std::abs(parentScale[axis]) < 1e-12)
        {
            return { {}, "output frame has a singular or non-finite scale" };
        }
    }
    const auto inverseParent = _physicsFrame(parentTransform).GetInverse();
    std::vector<UsdPhysicsMassProperties> components;
    std::vector<GfMatrix4f> transforms;
    for (const auto& collider : colliders)
    {
        MassProperties properties;
        std::string colliderIssue;
        const bool explicitMass = _readExplicitMass(collider, properties, colliderIssue);
        if (collider == body || !explicitMass)
        {
            std::string detail;
            if (!colliderIssue.empty())
            {
                detail = "; " + colliderIssue;
            }
            else if (!bodyIssue.empty())
            {
                detail = "; body " + body.GetPath().GetAsString() + ": " + bodyIssue;
            }
            return { {},
                     "collider merging requires explicit mass, centre of mass, and inertia, or a complete body "
                     "override: " +
                         collider.GetPath().GetAsString() + detail };
        }
        const GfTransform transform(xforms.GetLocalToWorldTransform(collider));
        const auto scale = transform.GetScale();
        const GfVec3f center(static_cast<float>(properties.centerOfMass[0] * scale[0]),
                             static_cast<float>(properties.centerOfMass[1] * scale[1]),
                             static_cast<float>(properties.centerOfMass[2] * scale[2]));
        GfMatrix3f inertia(0.0f);
        inertia.SetDiagonal(properties.diagonalInertia);
        inertia = UsdPhysicsMassProperties::RotateInertia(inertia, properties.principalAxes);
        components.emplace_back(properties.mass, inertia, center);
        transforms.emplace_back(_physicsFrame(transform) * inverseParent);
    }
    const auto combined =
        UsdPhysicsMassProperties::Sum(components.data(), transforms.data(), static_cast<uint32_t>(components.size()));
    MassProperties result;
    result.mass = combined.GetMass();
    for (int axis = 0; axis != 3; ++axis)
    {
        result.centerOfMass[axis] = static_cast<float>(combined.GetCenterOfMass()[axis] / parentScale[axis]);
    }
    result.diagonalInertia =
        UsdPhysicsMassProperties::GetMassSpaceInertia(combined.GetInertiaTensor(), result.principalAxes);
    if (!std::isfinite(result.mass) || !_finite(result.centerOfMass) || !_finite(result.diagonalInertia))
    {
        return { {}, "combined mass properties are not finite" };
    }
    return { result, {} };
}

/// Author the combined mass, centre of mass, inertia, and principal axes.
void authorMassProperties(const UsdPrim& collider, const MassProperties& properties)
{
    const auto mass = UsdPhysicsMassAPI::Apply(collider);
    mass.CreateMassAttr().Set(properties.mass);
    mass.CreateCenterOfMassAttr().Set(properties.centerOfMass);
    mass.CreateDiagonalInertiaAttr().Set(properties.diagonalInertia);
    mass.CreatePrincipalAxesAttr().Set(properties.principalAxes);
}

} // namespace usd_optimize::physics_body_meshes
