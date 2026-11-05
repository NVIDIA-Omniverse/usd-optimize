// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MeshDiscovery.h"

#include "PrimPhysicsInfo.h"

#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/gprim.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/sphere.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdPhysics/collisionGroup.h>
#include <pxr/usd/usdPhysics/joint.h>
#include <pxr/usd/usdPhysics/tokens.h>
#include <pxr/usd/usdSkel/root.h>

#include <set>
#include <utility>
PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{
namespace
{

/// Shapes that the existing primitivesToMeshes operation can convert for visual merging.
bool _isSupportedShape(const UsdPrim& prim)
{
    return prim.IsA<UsdGeomCube>() || prim.IsA<UsdGeomSphere>() || prim.IsA<UsdGeomCylinder>() ||
           prim.IsA<UsdGeomCapsule>() || prim.IsA<UsdGeomCone>();
}

/// These relationships describe compatibility. Their effective values are resolved during later bucketing.
bool _isCompatibilityRelationship(const UsdRelationship& relationship, const PrimPhysicsInfo& physics)
{
    const auto& name = relationship.GetName().GetString();
    if (name == "material:binding" || TfStringStartsWith(name, "material:binding:"))
    {
        return true;
    }
    if (relationship.GetName() == UsdPhysicsTokens->physicsSimulationOwner)
    {
        return physics.isRigidBody() || physics.isCollider();
    }
    return false;
}


/// A direct joint target to a retained Mesh frame is safe; other references need a preservation policy.
/// Subset dependencies also constrain replacement of their owning mesh geometry.
std::string _getConnectionSkipReason(const UsdPrim& prim, bool retainedJointFrame, const ConnectionMergeLimits& mergeLimits)
{
    const auto outgoing = mergeLimits.unsupportedSources.find(prim.GetPath());
    if (outgoing != mergeLimits.unsupportedSources.end())
    {
        return outgoing->second;
    }
    const auto incoming = mergeLimits.incoming.find(prim.GetPath());
    if (incoming != mergeLimits.incoming.end())
    {
        for (const auto& reference : incoming->second)
        {
            if (!(retainedJointFrame && reference.jointBody && reference.target == prim.GetPath()))
            {
                return "referenced by " + reference.source.GetAsString() + ": " + reference.target.GetAsString();
            }
        }
    }
    return {};
}

/// A Mesh used as the output parent retains its identity and transform, including any joint frame it defines.
std::string _getGeometrySkipReason(const UsdPrim& mesh,
                                   const UsdPrim& outputParent,
                                   const ConnectionMergeLimits& mergeLimits)
{
    auto reason = _getConnectionSkipReason(mesh, mesh == outputParent, mergeLimits);
    if (!reason.empty())
    {
        return reason;
    }
    for (const auto& child : mesh.GetAllChildren())
    {
        if (child.IsA<UsdGeomSubset>())
        {
            reason = _getConnectionSkipReason(child, false, mergeLimits);
            if (!reason.empty())
            {
                return reason;
            }
        }
    }
    return {};
}

/// Keep non-subset descendants attached to a Mesh when its own geometry is later replaced.
bool _hasHierarchyChildren(const UsdPrim& mesh)
{
    for (const auto& child : mesh.GetAllChildren())
    {
        if (!child.IsA<UsdGeomSubset>() || !child.GetAllChildren().empty())
        {
            return true;
        }
    }
    return false;
}

/// Bound output within a body at articulation roots and transforms or visibility that cannot be baked into geometry.
/// Repeat the transform and visibility checks from generic Merge's private boundary helper, with additional cases
/// below.
bool _isMergeBoundary(const UsdPrim& prim, const PrimPhysicsInfo& physics)
{
    for (const auto& attr : prim.GetAuthoredAttributes())
    {
        if (TfStringStartsWith(attr.GetName().GetString(), "primvars:") && attr.GetResolveInfo().ValueIsBlocked())
        {
            // Moving above this prim could restore a primvar value that this hierarchy deliberately blocks.
            return true;
        }
        if ((TfStringStartsWith(attr.GetName().GetString(), "xformOp") || attr.GetName() == UsdGeomTokens->visibility) &&
            attr.GetNumTimeSamples() != 0)
        {
            // Even one numeric time sample can differ from the default-time value used when merging geometry.
            return true;
        }
    }
    if (physics.isArticulationRoot())
    {
        // Keep geometry within the articulation hierarchy that contains it.
        return true;
    }
    const UsdGeomXformable xformable(prim);
    if (xformable && xformable.GetResetXformStack())
    {
        // Moving above a reset would make geometry inherit transforms that do not currently affect it.
        return true;
    }
    if (xformable && xformable.TransformMightBeTimeVarying())
    {
        // Baked points cannot replace a transform that changes over time.
        return true;
    }
    // Keep geometry below the prim that supplies its animated visibility.
    const UsdGeomImageable imageable(prim);
    return imageable && imageable.GetVisibilityAttr().ValueMightBeTimeVarying();
}

/// Return whether a static visibility opinion keeps this prim invisible at every time.
bool _isAlwaysInvisible(UsdPrim prim)
{
    for (; prim; prim = prim.GetParent())
    {
        const UsdGeomImageable imageable(prim);
        if (!imageable)
        {
            continue;
        }
        const auto visibility = imageable.GetVisibilityAttr();
        TfToken value;
        // Even one numeric sample can differ from the default-time visibility.
        if (visibility.Get(&value) && value == UsdGeomTokens->invisible && visibility.GetNumTimeSamples() == 0)
        {
            return true;
        }
    }
    return false;
}

/// Return whether this hierarchy must be excluded, with a report reason for geometry exclusions.
bool _shouldPruneTraversal(const UsdPrim& prim, std::string& reason)
{
    if (!prim.IsLoaded())
    {
        reason = "unloaded payload hierarchy";
        return true;
    }
    if (prim.IsA<UsdShadeMaterial>())
    {
        // Material definitions are not geometry candidates and do not need a skipped-geometry entry.
        reason.clear();
        return true;
    }
    if (prim.IsA<UsdSkelRoot>() || prim.IsA<UsdGeomCamera>())
    {
        reason = "skeleton or camera hierarchy";
        return true;
    }
    if (prim.IsInstance() || prim.IsInstanceProxy() || prim.IsA<UsdGeomPointInstancer>())
    {
        reason = "instance hierarchy is preserved";
        return true;
    }
    reason.clear();
    return false;
}


/// Collect geometry within one merge boundary, recursing when a descendant defines another boundary.
/// Nested bodies are pruned because they have their own entries from body discovery.
void _collectGeometry(const UsdPrim& startPrim,
                      const ConnectionMergeLimits& mergeLimits,
                      std::vector<GeometryCandidate>& candidates,
                      std::vector<PathIssue>& skipped)
{
    const auto range = UsdPrimRange(startPrim);
    for (auto iter = range.begin(); iter != range.end(); ++iter)
    {
        const UsdPrim& prim = *iter;
        std::string reason;
        if (_shouldPruneTraversal(prim, reason))
        {
            iter.PruneChildren();
            continue;
        }

        const PrimPhysicsInfo physics(prim);
        if (prim != startPrim && physics.isRigidBody())
        {
            // Body discovery gives each nested body its own mesh-collection pass.
            iter.PruneChildren();
            continue;
        }
        const bool isGeometry = prim.IsA<UsdGeomMesh>() || _isSupportedShape(prim);
        if (prim != startPrim && (_isMergeBoundary(prim, physics) || mergeLimits.isBoundary(prim) ||
                                  (isGeometry && _hasHierarchyChildren(prim))))
        {
            // Keep descendant geometry within its nearest merge boundary.
            iter.PruneChildren();
            _collectGeometry(prim, mergeLimits, candidates, skipped);
            continue;
        }

        if (isGeometry)
        {
            reason = _getGeometrySkipReason(prim, startPrim, mergeLimits);
            if (!reason.empty())
            {
                skipped.push_back({ prim.GetPath().GetAsString(), reason });
                continue;
            }
            GeometryRole role = GeometryRole::eVisual;
            if (physics.isCollider())
            {
                const bool colliderOnly =
                    UsdGeomImageable(prim).ComputePurpose() == UsdGeomTokens->guide || _isAlwaysInvisible(prim);
                role = colliderOnly ? GeometryRole::eCollider : GeometryRole::eVisualAndCollider;
            }
            candidates.push_back({ prim, startPrim, role });
        }
        else if (prim.IsA<UsdGeomGprim>())
        {
            skipped.push_back(
                { prim.GetPath().GetAsString(), "unsupported geometry type: " + prim.GetTypeName().GetString() });
        }
    }
}


/// Collect bodies at or below this start; each recursive call owns one discovered body's subtree.
/// Report excluded hierarchies and geometry outside the bodies found by this traversal.
void _collectPhysicsBodies(const UsdPrim& startPrim, std::vector<UsdPrim>& physicsBodies, std::vector<PathIssue>& skipped)
{
    const bool startsAtRigidBody = !startPrim.IsPseudoRoot() && PrimPhysicsInfo(startPrim).isRigidBody();
    // Include unloaded prims so their omitted hierarchies can be reported explicitly.
    const auto traversalPredicate = UsdPrimIsActive && UsdPrimIsDefined && !UsdPrimIsAbstract;
    const auto range = UsdPrimRange(startPrim, traversalPredicate);
    for (auto iter = range.begin(); iter != range.end(); ++iter)
    {
        const UsdPrim& prim = *iter;
        if (prim.IsPseudoRoot())
        {
            // The pseudo-root is a traversal start only; it cannot own geometry.
            continue;
        }
        std::string reason;
        if (_shouldPruneTraversal(prim, reason))
        {
            // Report a skipped hierarchy once, without reporting all its descendants separately.
            if (!reason.empty())
            {
                skipped.push_back({ prim.GetPath().GetAsString(), reason });
            }
            iter.PruneChildren();
            continue;
        }
        if (prim == startPrim)
        {
            if (startsAtRigidBody)
            {
                // A start prim can itself be the body; ancestors are deliberately not considered.
                physicsBodies.push_back(startPrim);
            }
        }
        else if (PrimPhysicsInfo(prim).isRigidBody())
        {
            // The child call discovers this body and its nested bodies. Pruning prevents duplicate traversal.
            iter.PruneChildren();
            _collectPhysicsBodies(prim, physicsBodies, skipped);
            continue;
        }
        if (!startsAtRigidBody && prim.IsA<UsdGeomGprim>())
        {
            // This geometry is outside the bodies discovered below the requested traversal start.
            skipped.push_back({ prim.GetPath().GetAsString(), "no rigid body found within traversal start" });
        }
    }
}

} // namespace

/// Return whether incoming or unsupported outgoing connections retain this prim as a boundary.
bool ConnectionMergeLimits::isBoundary(const UsdPrim& prim) const
{
    return incoming.count(prim.GetPath()) || unsupportedSources.count(prim.GetPath());
}

/// Return bodies at or below the start prim and append issues for excluded paths.
std::vector<UsdPrim> getDescendantPhysicsBodies(const UsdPrim& startPrim, std::vector<PathIssue>& skipped)
{
    std::vector<UsdPrim> physicsBodies;
    _collectPhysicsBodies(startPrim, physicsBodies, skipped);
    return physicsBodies;
}

/// Scan the loaded stage for connections that require a merge boundary or prevent replacing Mesh geometry.
/// Record incoming targets and unsupported outgoing connections; empty connections are benign.
ConnectionMergeLimits getMergeLimitsFromConnections(const UsdStageWeakPtr& stage)
{
    ConnectionMergeLimits mergeLimits;
    std::set<SdfPath> materialCollections;
    std::set<SdfPath> collisionCollections;
    // Instance proxies can consume geometry outside the instance even though they are not merge candidates.
    const auto range = UsdPrimRange::Stage(stage, UsdTraverseInstanceProxies());
    for (const auto& prim : range)
    {
        const UsdPhysicsCollisionGroup group(prim);
        if (group)
        {
            const auto collection = group.GetCollidersCollectionAPI();
            collisionCollections.insert(collection.GetCollectionPath());
            const auto query = collection.ComputeMembershipQuery();
            const auto& includedCollections = query.GetIncludedCollections();
            collisionCollections.insert(includedCollections.begin(), includedCollections.end());
        }
        for (const auto& relationship : prim.GetAuthoredRelationships())
        {
            if (TfStringStartsWith(relationship.GetName().GetString(), "material:binding:collection:"))
            {
                SdfPathVector targets;
                relationship.GetTargets(&targets);
                if (targets.size() == 2 && targets.front().IsPropertyPath())
                {
                    materialCollections.insert(targets.front());
                }
            }
        }
    }
    for (const auto& prim : range)
    {
        const PrimPhysicsInfo physics(prim);
        for (const auto& relationship : prim.GetAuthoredRelationships())
        {
            const auto& name = relationship.GetName().GetString();
            const auto separator = name.rfind(':');
            if (separator != std::string::npos &&
                (name.substr(separator) == ":includes" || name.substr(separator) == ":excludes"))
            {
                const auto collectionPath = prim.GetPath().AppendProperty(TfToken(name.substr(0, separator)));
                if (materialCollections.count(collectionPath) && !collisionCollections.count(collectionPath))
                {
                    // Face bindings can be retained after moving geometry; collision-group membership cannot.
                    continue;
                }
            }
            if (!relationship.HasAuthoredTargets())
            {
                continue;
            }
            SdfPathVector targets;
            const bool resolved = relationship.GetTargets(&targets);
            if ((!resolved || !targets.empty()) && !_isCompatibilityRelationship(relationship, physics))
            {
                mergeLimits.unsupportedSources.emplace(
                    prim.GetPath(),
                    "unsupported relationship: " + relationship.GetPath().GetAsString());
            }
            const bool jointBody =
                prim.IsA<UsdPhysicsJoint>() && (relationship.GetName() == UsdPhysicsTokens->physicsBody0 ||
                                                relationship.GetName() == UsdPhysicsTokens->physicsBody1);
            for (const auto& target : targets)
            {
                mergeLimits.incoming[target.GetPrimPath()].push_back({ relationship.GetPath(), target, jointBody });
            }
        }
        for (const auto& attribute : prim.GetAuthoredAttributes())
        {
            if (!attribute.HasAuthoredConnections())
            {
                continue;
            }
            SdfPathVector targets;
            const bool resolved = attribute.GetConnections(&targets);
            if (!resolved || !targets.empty())
            {
                mergeLimits.unsupportedSources.emplace(
                    prim.GetPath(),
                    "unsupported attribute connection: " + attribute.GetPath().GetAsString());
            }
            for (const auto& target : targets)
            {
                mergeLimits.incoming[target.GetPrimPath()].push_back({ attribute.GetPath(), target, false });
            }
        }
    }
    return mergeLimits;
}

/// Collect one body's geometry without crossing into a nested body.
std::vector<GeometryCandidate> getGeometryWithinPhysicsBody(const UsdPrim& rigidBody,
                                                            const ConnectionMergeLimits& mergeLimits,
                                                            std::vector<PathIssue>& skipped)
{
    std::vector<GeometryCandidate> candidates;
    _collectGeometry(rigidBody, mergeLimits, candidates, skipped);
    return candidates;
}

/// Group candidates by merge boundary without checking geometry compatibility.
std::map<UsdPrim, std::vector<GeometryCandidate>> groupGeometryByMergeBoundary(const std::vector<GeometryCandidate>& candidates)
{
    std::map<UsdPrim, std::vector<GeometryCandidate>> groups;
    for (const auto& candidate : candidates)
    {
        groups[candidate.mergeBoundary].push_back(candidate);
    }
    return groups;
}

/// Collect candidate geometry by owning body without editing the stage or retaining empty bodies.
std::map<UsdPrim, std::vector<GeometryCandidate>> discoverMeshes(const UsdStageWeakPtr& stage,
                                                                 const SdfPathVector& startPaths,
                                                                 std::vector<PathIssue>& skipped)
{
    std::map<UsdPrim, std::vector<GeometryCandidate>> geometryByBody;
    std::vector<UsdPrim> physicsBodies;
    for (const auto& path : startPaths)
    {
        const auto descendants = getDescendantPhysicsBodies(stage->GetPrimAtPath(path), skipped);
        physicsBodies.insert(physicsBodies.end(), descendants.begin(), descendants.end());
    }
    if (physicsBodies.empty())
    {
        return geometryByBody;
    }
    const auto mergeLimits = getMergeLimitsFromConnections(stage);
    for (const auto& body : physicsBodies)
    {
        auto candidates = getGeometryWithinPhysicsBody(body, mergeLimits, skipped);
        if (!candidates.empty())
        {
            geometryByBody.emplace(body, std::move(candidates));
        }
    }
    return geometryByBody;
}

} // namespace usd_optimize::physics_body_meshes
