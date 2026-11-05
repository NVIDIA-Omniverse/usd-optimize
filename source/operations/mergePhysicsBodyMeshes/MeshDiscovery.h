// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "PathIssue.h"

#include <map>
#include <string>
#include <usd_optimize/core/UsdIncludes.h>
#include <vector>

namespace usd_optimize::physics_body_meshes
{

/// A relationship or attribute connection that requires its target to remain valid after merging.
struct PrimReference
{
    PXR_NS::SdfPath source;
    PXR_NS::SdfPath target;
    bool jointBody = false;
};

/// Merge boundaries and exclusions required by connections across the loaded stage.
struct ConnectionMergeLimits
{
    std::map<PXR_NS::SdfPath, std::vector<PrimReference>> incoming;
    std::map<PXR_NS::SdfPath, std::string> unsupportedSources;

    /// Return whether connections require this prim to remain an output boundary.
    bool isBoundary(const PXR_NS::UsdPrim& prim) const;
};

/// Whether geometry participates in visual merging, collider merging, or both.
enum class GeometryRole
{
    eVisual,
    eCollider,
    eVisualAndCollider,
};

/// Geometry that can be considered for merging, with the boundary that must contain its output.
struct GeometryCandidate
{
    PXR_NS::UsdPrim prim;
    PXR_NS::UsdPrim mergeBoundary;
    GeometryRole role = GeometryRole::eVisual;
};

/// Return bodies at or below a valid traversal start, including the start prim when it is a body.
/// Ancestors are not searched. Nested bodies are collected separately.
/// Append issues for excluded hierarchies and geometry outside the discovered bodies.
std::vector<PXR_NS::UsdPrim> getDescendantPhysicsBodies(const PXR_NS::UsdPrim& startPrim,
                                                        std::vector<PathIssue>& skipped);

/// Find connection targets and sources that must remain valid, including those outside the traversal starts.
ConnectionMergeLimits getMergeLimitsFromConnections(const PXR_NS::UsdStageWeakPtr& stage);

/// Return supported meshes and analytic shapes owned by a body, including geometry on the body prim.
/// Retain merge boundaries, exclude nested bodies, and report geometry constrained by connections.
/// Unsupported geometric prim types are reported; material definitions are omitted from geometry diagnostics.
/// Guide-purpose colliders and colliders hidden by static visibility have only a collision role.
/// Other colliders also have a visual role, including those whose visibility can change over time.
std::vector<GeometryCandidate> getGeometryWithinPhysicsBody(const PXR_NS::UsdPrim& rigidBody,
                                                            const ConnectionMergeLimits& mergeLimits,
                                                            std::vector<PathIssue>& skipped);

/// Group candidates by merge boundary without checking geometry compatibility.
std::map<PXR_NS::UsdPrim, std::vector<GeometryCandidate>> groupGeometryByMergeBoundary(
    const std::vector<GeometryCandidate>& candidates);

/// Collect descendant bodies from valid, deduplicated traversal starts, then their geometry.
/// The caller resolves explicit path parameters on the supplied stage.
/// Output remains inside each traversal subtree and below rigid bodies, articulation roots, and other merge boundaries.
/// Unsupported hierarchies and connections are reported without editing the stage.
std::map<PXR_NS::UsdPrim, std::vector<GeometryCandidate>> discoverMeshes(const PXR_NS::UsdStageWeakPtr& stage,
                                                                         const PXR_NS::SdfPathVector& startPaths,
                                                                         std::vector<PathIssue>& skipped);

} // namespace usd_optimize::physics_body_meshes
