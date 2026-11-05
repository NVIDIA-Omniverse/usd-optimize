// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MeshAuthoring.h"

#include "ColliderProperties.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdPhysics/collisionAPI.h>
#include <pxr/usd/usdPhysics/massAPI.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>
#include <pxr/usd/usdPhysics/tokens.h>

#include <algorithm>
#include <set>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (Collision)
);
// LCOV_EXCL_STOP
// clang-format on


/// Choose an unused child path without replacing an existing prim.
SdfPath uniqueChildPath(const UsdStageWeakPtr& stage, const SdfPath& parent, const TfToken& name)
{
    SdfPath path = parent.AppendChild(name);
    for (size_t suffix = 1; stage->GetPrimAtPath(path); ++suffix)
    {
        path = parent.AppendChild(TfToken(name.GetString() + "_" + std::to_string(suffix)));
    }
    return path;
}

namespace
{

/// Preserve the input layer if authoring fails after planning has succeeded.
class EditRollback
{
public:
    /// Snapshot the target layer before authoring any replacements.
    explicit EditRollback(const SdfLayerHandle& layer)
        : m_layer(layer)
        , m_backup(SdfLayer::CreateAnonymous())
    {
        m_backup->TransferContent(layer);
    }
    /// Restore the original layer unless all replacements were committed.
    ~EditRollback()
    {
        if (!m_committed)
        {
            m_layer->TransferContent(m_backup);
        }
    }
    /// Keep the authored replacements when the rollback guard is destroyed.
    void commit()
    {
        m_committed = true;
    }

private:
    SdfLayerHandle m_layer;
    SdfLayerRefPtr m_backup;
    bool m_committed = false;
};

/// Remove a source's geometry while retaining its transform, body, and any hierarchy that uses it.
void _retainFrame(UsdPrim prim)
{
    // Inherited primvars can still supply values to unmerged descendants.
    if (!prim.SetTypeName(UsdGeomTokens->Xform) || prim.GetTypeName() != UsdGeomTokens->Xform)
    {
        throw std::runtime_error("Cannot retain source frame in this edit target: " + prim.GetPath().GetAsString());
    }
}

/// Author composed targets through USD so reference and variant edit targets map their namespaces correctly.
void _copyConnections(const UsdPrim& source, const UsdPrim& destination)
{
    for (const auto& relationship : source.GetAuthoredRelationships())
    {
        SdfPathVector targets;
        if (relationship.GetTargets(&targets) &&
            !destination.CreateRelationship(relationship.GetName(), relationship.IsCustom()).SetTargets(targets))
        {
            throw std::runtime_error("Cannot author relationship: " + relationship.GetPath().GetAsString());
        }
    }
    for (const auto& attribute : source.GetAuthoredAttributes())
    {
        if (attribute.HasAuthoredConnections())
        {
            SdfPathVector targets;
            if (!attribute.GetConnections(&targets) ||
                !destination.GetAttribute(attribute.GetName()).SetConnections(targets))
            {
                throw std::runtime_error("Cannot author connections: " + attribute.GetPath().GetAsString());
            }
        }
    }
}

/// Remove physics from replaced geometry while keeping body and articulation properties on retained frames.
void _removeCollision(UsdPrim prim)
{
    const bool body = prim.HasAPI<UsdPhysicsRigidBodyAPI>();
    for (const auto& schema : physicsSchemas(prim))
    {
        const auto& name = schema.GetString();
        if (name.find("RigidBody") != std::string::npos || name.find("Articulation") != std::string::npos ||
            (body && name.find("MassAPI") != std::string::npos))
        {
            continue;
        }
        prim.RemoveAppliedSchema(schema);
    }
    const auto bodyAttrs = UsdPhysicsRigidBodyAPI::GetSchemaAttributeNames(false);
    const auto massAttrs = UsdPhysicsMassAPI::GetSchemaAttributeNames(false);
    std::set<TfToken> retained;
    if (body)
    {
        retained.insert(bodyAttrs.begin(), bodyAttrs.end());
        retained.insert(massAttrs.begin(), massAttrs.end());
    }
    for (const auto& property : prim.GetAuthoredProperties())
    {
        const auto& name = property.GetName().GetString();
        if (isPhysicsProperty(name) && !retained.count(property.GetName()) &&
            !TfStringStartsWith(name, "physxRigidBody:") && !TfStringStartsWith(name, "physxArticulation:"))
        {
            prim.RemoveProperty(property.GetName());
        }
    }
    if (prim.HasAPI<UsdPhysicsCollisionAPI>())
    {
        throw std::runtime_error("Cannot replace collision behavior in this edit target: " + prim.GetPath().GetAsString());
    }
}

/// Copy the source's own properties and material subsets, excluding its transform and hierarchy children.
/// The collision child inherits the retained source frame's transform instead.
/// Keep MassAPI on both the body and collider: it supplies body overrides and the collider's mass contribution.
void _copyCollisionGeometry(const UsdPrim& source, UsdPrim destination)
{
    destination.SetTypeName(source.GetTypeName());
    SdfTokenListOp schemas;
    auto applied = source.GetAppliedSchemas();
    const auto physics = physicsSchemas(source);
    for (const auto& schema : physics)
    {
        if (std::find(applied.begin(), applied.end(), schema) == applied.end())
        {
            applied.push_back(schema);
        }
    }
    applied.erase(std::remove_if(applied.begin(),
                                 applied.end(),
                                 /* Keep body and articulation APIs on their original frames. */
                                 [](const TfToken& schema)
                                 {
                                     const auto& name = schema.GetString();
                                     return name.find("RigidBody") != std::string::npos ||
                                            name.find("Articulation") != std::string::npos;
                                 }),
                  applied.end());
    schemas.SetExplicitItems(applied);
    destination.SetMetadata(UsdTokens->apiSchemas, schemas);
    const auto editTarget = destination.GetStage()->GetEditTarget();
    const auto layer = editTarget.GetLayer();
    for (const auto& property : source.GetAuthoredProperties())
    {
        const auto& name = property.GetName().GetString();
        if (TfStringStartsWith(name, "xformOp") || TfStringStartsWith(name, "physxRigidBody:") ||
            property.GetName() == UsdPhysicsTokens->physicsRigidBodyEnabled ||
            property.GetName() == UsdPhysicsTokens->physicsKinematicEnabled ||
            property.GetName() == UsdPhysicsTokens->physicsVelocity ||
            property.GetName() == UsdPhysicsTokens->physicsAngularVelocity)
        {
            continue;
        }
        if (!SdfCopySpec(source.GetStage()->GetRootLayer(),
                         property.GetPath(),
                         layer,
                         editTarget.MapToSpecPath(destination.GetPath().AppendProperty(property.GetName()))))
        {
            throw std::runtime_error("Cannot retain collider property: " + property.GetPath().GetAsString());
        }
    }
    _copyConnections(source, destination);
    for (const auto& child : source.GetChildren())
    {
        if (!child.IsA<UsdGeomSubset>())
        {
            continue;
        }
        const auto subset =
            UsdGeomSubset::Define(destination.GetStage(), destination.GetPath().AppendChild(child.GetName())).GetPrim();
        SdfTokenListOp subsetSchemas;
        if (child.GetMetadata(UsdTokens->apiSchemas, &subsetSchemas) &&
            !subset.SetMetadata(UsdTokens->apiSchemas, subsetSchemas))
        {
            throw std::runtime_error("Cannot retain collider subset APIs: " + child.GetPath().GetAsString());
        }
        for (const auto& property : child.GetAuthoredProperties())
        {
            if (!SdfCopySpec(source.GetStage()->GetRootLayer(),
                             property.GetPath(),
                             layer,
                             editTarget.MapToSpecPath(subset.GetPath().AppendProperty(property.GetName()))))
            {
                throw std::runtime_error("Cannot retain collider subset: " + child.GetPath().GetAsString());
            }
        }
        _copyConnections(child, subset);
    }
    UsdGeomImageable(destination).CreatePurposeAttr().Set(UsdGeomTokens->guide);
}

} // namespace

/// Author prepared outputs and replace source contributions while retaining required frames.
void authorResults(const UsdStageWeakPtr& stage,
                   const UsdStageWeakPtr& preparedStage,
                   const std::map<SdfPath, SourceGeometry>& sources,
                   const std::vector<MergedGeometry>& merged,
                   const SdfPathVector& localizedInstances)
{
    if (merged.empty())
    {
        return;
    }
    const auto editTarget = stage->GetEditTarget();
    auto editLayer = editTarget.GetLayer();
    if (!editLayer->PermissionToEdit())
    {
        throw std::runtime_error("The current edit target is not editable");
    }
    for (const auto& output : merged)
    {
        if (editTarget.MapToSpecPath(output.output).IsEmpty())
        {
            throw std::runtime_error("Cannot author merged output in this edit target: " + output.output.GetAsString());
        }
    }
    for (const auto& [path, source] : sources)
    {
        if ((source.visualMerged || source.collisionMerged) && editTarget.MapToSpecPath(path).IsEmpty())
        {
            throw std::runtime_error("Cannot replace source geometry in this edit target: " + path.GetAsString());
        }
    }
    EditRollback rollback(editLayer);
    for (const auto& path : localizedInstances)
    {
        const bool used =
            std::any_of(merged.begin(),
                        merged.end(),
                        /* Both output authoring and source replacement require editable prims. */
                        [&](const MergedGeometry& output)
                        {
                            return output.output.HasPrefix(path) ||
                                   std::any_of(output.sources.begin(),
                                               output.sources.end(),
                                               /* Include instances containing replaced sources. */
                                               [&](const SdfPath& source) { return source.HasPrefix(path); });
                        });
        const auto prim = stage->GetPrimAtPath(path);
        if (used && (!prim.SetInstanceable(false) || prim.IsInstance()))
        {
            throw std::runtime_error("Cannot make selected instance local: " + path.GetAsString());
        }
    }
    for (const auto& output : merged)
    {
        const auto destination = editTarget.MapToSpecPath(output.output);
        SdfCreatePrimInLayer(editLayer, destination.GetParentPath());
        if (!SdfCopySpec(preparedStage->GetRootLayer(), output.output, editLayer, destination))
        {
            throw std::runtime_error("Cannot author merged output: " + output.output.GetAsString());
        }
        for (const auto& source : UsdPrimRange(preparedStage->GetPrimAtPath(output.output)))
        {
            _copyConnections(source, stage->GetPrimAtPath(source.GetPath()));
        }
    }
    for (const auto& [path, source] : sources)
    {
        if (!source.visualMerged && !source.collisionMerged)
        {
            continue;
        }
        auto prim = stage->GetPrimAtPath(path);
        const bool retain = path == source.outputParent || path == source.rigidBody;
        const bool visualReplaced = !source.visualApplicable || source.visualMerged;
        const bool collisionReplaced = !source.collider || source.collisionMerged;
        if (visualReplaced && collisionReplaced)
        {
            if (retain)
            {
                _retainFrame(prim);
                _removeCollision(prim);
            }
            else if (!prim.SetActive(false) || prim.IsActive())
            {
                throw std::runtime_error("Cannot deactivate replaced source: " + path.GetAsString());
            }
        }
        else if (visualReplaced)
        {
            if (retain)
            {
                const auto collisionPath = uniqueChildPath(stage, path, _tokens->Collision);
                const auto collision = stage->DefinePrim(collisionPath);
                _copyCollisionGeometry(preparedStage->GetPrimAtPath(path), collision);
                _retainFrame(prim);
                _removeCollision(prim);
            }
            else
            {
                if (!UsdGeomImageable(prim).CreatePurposeAttr().Set(UsdGeomTokens->guide) ||
                    UsdGeomImageable(prim).ComputePurpose() != UsdGeomTokens->guide)
                {
                    throw std::runtime_error("Cannot retain collider as guide geometry: " + path.GetAsString());
                }
            }
        }
        else if (collisionReplaced)
        {
            _removeCollision(prim);
        }
    }
    rollback.commit();
}

} // namespace usd_optimize::physics_body_meshes
