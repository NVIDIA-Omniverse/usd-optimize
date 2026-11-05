// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MergePhysicsBodyMeshes.h"

#include "MeshDiscovery.h"
#include "MeshMerge.h"

#include <pxr/base/js/json.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usdGeom/mesh.h>

#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/Log.h>
#include <usd_optimize/core/Utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize
{
namespace
{

using physics_body_meshes::PathIssue;

/// Resolve explicit absolute prim paths on a valid stage, retaining only the outermost traversal starts.
/// An empty input starts traversal at the pseudo-root. Invalid or missing paths are skipped with a warning; a non-empty
/// input with no valid traversal starts returns no paths and records every rejected input and its reason.
/// Path expressions and wildcards are not supported.
/// Duplicate and descendant paths are removed; an explicit "/" therefore supersedes all other selections.
/// This helper checks path syntax and prim existence. Traversal predicates are applied later during discovery.
/// Replace rejectedPaths with the rejected inputs and their reasons, preserving the original strings.
SdfPathVector _getEffectivePrimPaths(const UsdStageWeakPtr& stage,
                                     const std::vector<std::string>& paths,
                                     std::vector<PathIssue>& rejectedPaths)
{
    rejectedPaths.clear();
    if (paths.empty())
    {
        return { SdfPath::AbsoluteRootPath() };
    }

    SdfPathVector effective;
    for (const auto& value : paths)
    {
        if (!SdfPath::IsValidPathString(value))
        {
            rejectedPaths.push_back({ value, "invalid prim path; path expressions are not supported" });
            continue;
        }

        const SdfPath path(value);
        if (!path.IsAbsolutePath() || !path.IsAbsoluteRootOrPrimPath() || path.ContainsPrimVariantSelection())
        {
            rejectedPaths.push_back({ value, "expected an absolute prim path" });
            continue;
        }

        if (!stage->GetPrimAtPath(path))
        {
            rejectedPaths.push_back({ value, "prim does not exist" });
            continue;
        }

        effective.push_back(path);
    }
    for (const auto& rejected : rejectedPaths)
    {
        USD_OPTIMIZE_LOG_WARN("Skipping '%s': %s", rejected.path.c_str(), rejected.issue.c_str());
    }
    SdfPath::RemoveDescendentPaths(&effective);
    return effective;
}


/// Build the analysis result from candidate scopes, excluded geometry, and rejected traversal-start inputs.
OperationResult _makeAnalysisResult(const std::map<UsdPrim, std::vector<physics_body_meshes::GeometryCandidate>>& bodyGeometry,
                                    const std::vector<PathIssue>& skippedPaths,
                                    const std::vector<PathIssue>& rejectedPaths)
{
    JsArray scopes;
    for (const auto& [body, candidates] : bodyGeometry)
    {
        for (const auto& [boundary, geometry] : physics_body_meshes::groupGeometryByMergeBoundary(candidates))
        {
            JsArray visualOnlyMeshes;
            JsArray collisionMeshes;
            JsArray visualShapes;
            JsArray collisionShapes;
            for (const auto& candidate : geometry)
            {
                const bool collider = candidate.role != physics_body_meshes::GeometryRole::eVisual;
                auto& paths = candidate.prim.IsA<UsdGeomMesh>() ? (collider ? collisionMeshes : visualOnlyMeshes) :
                                                                  (collider ? collisionShapes : visualShapes);
                paths.emplace_back(candidate.prim.GetPath().GetAsString());
            }
            scopes.emplace_back(JsObject{ { "rigidBody", JsValue(body.GetPath().GetAsString()) },
                                          { "outputParent", JsValue(boundary.GetPath().GetAsString()) },
                                          { "visualOnlyMeshes", JsValue(visualOnlyMeshes) },
                                          { "collisionMeshes", JsValue(collisionMeshes) },
                                          { "visualShapes", JsValue(visualShapes) },
                                          { "collisionShapes", JsValue(collisionShapes) } });
        }
    }
    JsArray skipped;
    for (const auto& item : skippedPaths)
    {
        skipped.emplace_back(JsObject{ { "path", JsValue(item.path) }, { "reason", JsValue(item.issue) } });
    }
    JsArray rejected;
    for (const auto& item : rejectedPaths)
    {
        rejected.emplace_back(JsObject{ { "path", JsValue(item.path) }, { "reason", JsValue(item.issue) } });
    }
    const JsObject analysis{ { "scopes", JsValue(scopes) },
                             { "skipped", JsValue(skipped) },
                             { "rejectedPaths", JsValue(rejected) } };
    const JsObject result{ { "analysis", JsValue(analysis) } };
    return { true, nullptr, getCStr(JsWriteToString(result)) };
}

} // namespace


/// Register the operation and its available arguments.
MergePhysicsBodyMeshesOperation::MergePhysicsBodyMeshesOperation()
    : Operation("mergePhysicsBodyMeshes",
                "Merge Physics Body Meshes",
                "Prepare and merge visual and collision meshes within each rigid body while preserving articulation. "
                "Analysis discovers candidate geometry and output scopes.")
{
    addArgument("paths",
                "Traversal Start Prims",
                kDisplayTypePrimPaths,
                "Explicit absolute prim paths where traversal starts to discover physics bodies and their mesh scopes. "
                "Bodies are discovered at or below each start; ancestors are not searched. Invalid or missing paths are "
                "skipped. Duplicate and descendant starts are removed. An empty list starts at the stage pseudo-root. "
                "Path expressions are not supported",
                m_paths)
        .setPlaceholder("Add traversal start prim paths, or leave empty for the stage");

    addArgument("considerMaterials",
                "Keep Visual Materials Separate",
                kDisplayTypeBool,
                "Keep visual meshes with different materials in separate merge groups. When off, preserve their "
                "bindings with material subsets. Physics materials always keep collider groups separate",
                m_considerMaterials);

    addArgument("generateNormals",
                "Generate Missing Normals",
                kDisplayTypeBool,
                "Generate normals on polygon meshes that lack them when other meshes in the merge group have them. "
                "Existing normals and subdivision surfaces are preserved",
                m_generateNormals);

    addArgument("generateUVs",
                "Generate Missing UVs",
                kDisplayTypeBool,
                "Generate projection UVs on visual meshes that lack them when other meshes in the merge group have "
                "them. Generated UVs can change texture appearance",
                m_generateUVs);

    addArgument("primvarDefaults",
                "Primvar Defaults",
                kDisplayTypeTextList,
                "Values for missing visual primvars, as name=value, for example displayColor=0.18,0.18,0.18. "
                "Without a supplied value, a missing primvar keeps meshes separate",
                m_primvarDefaults);

    addArgument("makeDoubleSided",
                "Reconcile Double-Sided Rendering",
                kDisplayTypeBool,
                "Set doubleSided to true when visual meshes in a merge group disagree. This exposes previously "
                "hidden back faces. When off, the differing meshes remain separate",
                m_makeDoubleSided);

    addArgument("mergeColliders",
                "Merge Colliders",
                kDisplayTypeBool,
                "Merge compatible collider meshes within each rigid body and combine affected mass properties. "
                "When off, preserve existing collision shapes and mass properties. Merging can change contact geometry",
                m_mergeColliders);

    addArgument("collisionApproximationFallback",
                "Collision Approximation Fallback",
                kDisplayTypeEnum,
                "Action when collider approximation methods or settings differ. Keep Separate preserves separate "
                "groups. Convex Hull allows one hull, which can fill gaps between shapes. Ignored when mergeColliders "
                "is off. Other physics incompatibilities still keep colliders separate",
                m_collisionApproximationFallback)
        .setEnableIf("mergeColliders")
        .setEnumValues<CollisionApproximationFallback>(
            { { CollisionApproximationFallback::eKeepSeparate, "Keep Separate" },
              { CollisionApproximationFallback::eConvexHull, "Convex Hull" } });

    addArgument("instanceMode",
                "Instances",
                kDisplayTypeEnum,
                "Preserve leaves geometry inside instances unchanged and reports it. Make Local allows selected "
                "instance geometry to become editable local geometry. Referenced source files remain unchanged",
                m_instanceMode)
        .setEnumValues<InstanceMode>(
            { { InstanceMode::ePreserve, "Preserve Instances" }, { InstanceMode::eMakeLocal, "Make Local" } });
}


/// Describe the operation behavior and available controls.
std::string MergePhysicsBodyMeshesOperation::getDocumentation() const
{
    return R"DOC(Merge compatible geometry within each rigid body while retaining articulation, body frames, and joint targets.
This operation supports standalone rigid bodies and articulated assets.

Traversal starts and output
---------------------------

``paths`` identifies traversal start prims used to discover physics bodies and their mesh scopes.
Bodies are discovered at or below each start. Starts do not need ``RigidBodyAPI``; ancestors are not searched.
Starting below a body processes only bodies found within that subtree; geometry owned by the body above stays unchanged.
Supply explicit absolute prim paths. Path expressions and wildcards are not supported.
Invalid or missing paths are skipped with a warning. Duplicate and descendant traversal starts are removed.
Analysis reports invalid or missing inputs and their reasons in ``rejectedPaths``. Redundant starts are not rejections.
For example, ``["/Robot", "/Robot/Arm", "/Robot"]`` starts one traversal at ``/Robot``.
An empty list or an explicit ``/`` starts traversal at the stage pseudo-root. A non-empty list with no valid paths does nothing.

Within a discovered body, geometry belongs to its nearest ancestor with ``RigidBodyAPI``, including the geometry prim itself.
Each body is merged separately. Nested bodies follow USD 26.08 ownership rules even without a transform-stack reset.
Geometry without an owning body remains unchanged.

Output stays within each discovered body below a traversal start. The stage's ``defaultPrim`` does not control placement.
Additional output boundaries include articulation roots, transform-stack resets, time-sampled transforms or visibility,
referenced containers, unsupported connections, and geometry prims with hierarchy children.
Output meshes are named ``MergedVisuals`` and ``MergedColliders``, with a suffix when needed.
A scope can produce several compatible groups. Singleton groups remain unchanged.

Visual geometry
---------------

Merge visual-only meshes and the rendered representation of collider geometry separately from collision geometry.
Cube, Sphere, Cylinder, Cone, and Capsule prims can be converted to meshes for visual merging.
Their analytic collision shapes remain intact. Collider prims with purpose ``guide`` do not produce visual copies.
Colliders hidden by static visibility on themselves or an ancestor also do not produce visual copies.
Time-sampled visibility can still require a visual copy. Hidden visual-only meshes remain eligible for merging.
Other purpose values are retained and compared.

Preparation affects only groups that actually merge:

- Generate missing normals on polygon meshes when ``generateNormals`` is enabled.
- Generate missing projection UVs when ``generateUVs`` is enabled.
- Reconcile ``st`` and ``st0`` names when material readers retain their meaning.
- Fill missing primvars only from the caller's ``primvarDefaults``.
- Set ``doubleSided=true`` for a differing group only when ``makeDoubleSided`` is enabled.

Existing normals and UVs are retained. No generation is needed when the entire group lacks them.
UV generation uses the default cube projection of ``generateProjectionUVs``; texture appearance can change.
Subdivision surfaces do not receive generated polygon normals.

Primvar defaults use ``name=value``, for example ``displayColor=0.18,0.18,0.18``.
The optional ``primvars:`` prefix is accepted. Defaults must fit each existing type with that name.
Use the generation controls for normals, ``st``, and ``st0``; those names cannot be defaulted.
Unused names are ignored. Invalid syntax or values fail before input edits begin.
Missing primvars without a default and unresolved type differences keep geometry separate.
Supported primvars are arrays of bool, int, float, token, string, float2, or float3.
Meshes with scalar primvars remain unchanged.
For meshes with material subsets, non-constant primvars must use float, float2, or float3 arrays.
Such meshes remain unchanged when a non-constant primvar uses another type.
Indexed values and compatible interpolation differences are merged without dropping primvars.
Primvars with elementSize other than one remain unchanged.

Resolve inherited primvars, visibility, purpose, and material bindings before moving geometry.
Retain all-purpose, full, and preview material assignments, including face assignments through material subsets.
Enable ``considerMaterials`` to keep differing visual material assignments separate.
Custom attributes are preserved. Different types or values, or a value missing from one mesh, keep meshes separate.
The execution report names differing additional compatibility values, including custom attributes, within the scope.

Collision geometry and mass
---------------------------

By default, preserve collision geometry, its world placement, physics bindings, and mass properties.
When its visual representation merges, keep the original collider with purpose ``guide``.
The merged visual mesh and retained collider each store geometry. This can increase memory use and file size.
If the source also supplies a retained body or joint frame, move its collision geometry to a ``Collision`` child.
The source path and transform remain valid.

Enable ``mergeColliders`` to merge compatible Mesh colliders within each body.
Visual-only geometry is never added to a collider.
Collider groups must agree on physics materials, collision-enabled state, simulation owners, and other physics settings.
Direct, inherited, and collection material bindings are resolved. Visual ``considerMaterials`` does not relax physics rules.
As in the USD 26.08 physics parser, a collider or material subset needs ``MaterialBindingAPI`` for its physics binding
to apply, and the bound material needs ``PhysicsMaterialAPI``. Bindings ignored by the parser stay ignored.
Render-only material subsets do not separate collider groups; their render assignments stay on the visual output.
Approximation-specific settings and registered or authored PhysX API names are retained.
Cooked geometry caches are omitted from new colliders.

Matching approximation methods and settings are retained.
With ``collisionApproximationFallback=0``, differences keep groups separate.
With ``collisionApproximationFallback=1``, an otherwise compatible group can use one convex hull instead.
A hull can fill gaps; even merging a shared approximation can change contacts.

Collider merging requires either a complete body-level mass override or complete explicit mass properties on every input.
Complete properties have positive finite mass and diagonal inertia, a finite centre of mass, and valid principal axes.
Body overrides remain on the body. Otherwise sum collider mass, weighted centres, and rotated inertia with
parallel-axis offsets using USD physics utilities, and author one MassAPI on the output collider.
Authored centre-of-mass scaling follows USD 26.08. Authored inertia is not inferred from geometry or density.
Missing or implicit mass, density-derived values, and incomplete inertia leave the affected colliders unchanged.
An authored diagonal inertia within USD 26.08's near-zero tolerance is implicit and cannot supply a merge contribution.
Visual-only merging never aggregates mass.

Bodies, articulation prims, and joint target frames remain at their original paths and transforms.
Joint relationships and local frames therefore remain unchanged.
Unknown incoming geometry references and outgoing relationships or attribute connections prevent replacement;
material and simulation-owner relationships have explicit preservation rules.

Composition and unsupported geometry
------------------------------------

Read the currently composed, loaded stage and author results into its current edit target.
Referenced source files are not edited automatically. Replaced sources are deactivated through overrides;
retained frames keep their hierarchy and inherited values. No intermediate hierarchy cleanup is performed.
The current edit target must be editable and must map every changed prim.
Authoring failures restore its previous layer contents.

``instanceMode=0`` preserves instance hierarchies.
``instanceMode=1`` makes selected instances editable only when their geometry actually merges.
Other instances and their source assets remain unchanged. Nested instances are supported.
If making an instance local would change its composed content, preserve the instance and report the changed path.
A selection inside an instance cannot localize an ancestor outside that selection.
Shared-prototype editing and Asset Structure 3.0 layer routing are not provided.
Unloaded payloads remain unloaded and are reported.

Skeleton, camera, material, and point-instancer hierarchies are excluded.
Material definitions are omitted from the skipped-geometry report. Unsupported geometric types are reported by type.
Time-sampled geometry or primvars, deformed geometry, motion vectors, unsupported subset families,
invalid topology or primvar data, and singular or reflected relative transforms are left unchanged with reasons.
Time-sampled transform and visibility containers can still contain independent merge groups below them.
This includes a single time sample, whose numeric-time value can differ from the default-time value.

Analysis and reporting
----------------------

Analysis mode performs read-only discovery. It does not prepare geometry, test compatibility, or localize instances.
``analysis.scopes`` contains ``rigidBody``, ``outputParent``, ``visualOnlyMeshes``, ``collisionMeshes``,
``visualShapes``, and ``collisionShapes``. Singleton scopes are candidates, not confirmed merges.
CollisionAPI classifies collision geometry regardless of visibility or collision-enabled state.
``analysis.skipped`` lists excluded paths or hierarchies and reasons.
``analysis.rejectedPaths`` lists invalid or missing traversal-start inputs and reasons, preserving the original strings.

Execution returns ``merged`` entries with output, rigidBody, outputParent, role, source paths, vertexCount, and faceCount.
Counts describe each authored output mesh, separately from statistics logged by reused preparation operations.
Collision entries also include the approximation, mass source, and any aggregated mass properties.
``skipped`` lists unchanged geometry or excluded hierarchies and reasons.
Preparation changes are logged through the existing operation reporting controls.
A source can contribute merged visuals while its collision representation remains unchanged.

Examples
--------

Merge visuals while retaining colliders:

.. code-block:: json

    [{"operation": "mergePhysicsBodyMeshes", "paths": ["/World/Robot"]}]

Also merge compatible colliders:

.. code-block:: json

    [{"operation": "mergePhysicsBodyMeshes", "paths": ["/World/Robot"], "mergeColliders": true}]

Allow local instance copies and generated UVs:

.. code-block:: json

    [{"operation": "mergePhysicsBodyMeshes", "paths": ["/World/Robot"], "instanceMode": 1, "generateUVs": true}]
)DOC";
}


/// Return the registered plugin author.
std::string MergePhysicsBodyMeshesOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


/// Return the operation plugin version.
UsdOptimizePluginVersion MergePhysicsBodyMeshesOperation::getVersion() const
{
    return { 0, 1, 0 };
}


/// Return the category used in operation reports.
std::string MergePhysicsBodyMeshesOperation::getCategory() const
{
    return "MERGE_PHYSICS_BODY_MESHES";
}


/// Place the operation in the geometry display group.
std::string MergePhysicsBodyMeshesOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


/// Merge compatible geometry under the requested preparation and physics controls.
OperationResult MergePhysicsBodyMeshesOperation::executeImpl()
{
    const auto stage = getUsdStage();
    if (!stage)
    {
        return { false, getCStr("A stage is required to merge physics body meshes") };
    }

    // Resolve path parameters once; merging consumes valid traversal starts.

    physics_body_meshes::MergeOptions options;
    std::vector<PathIssue> rejectedPaths;
    options.startPaths = _getEffectivePrimPaths(stage, m_paths, rejectedPaths);
    options.considerMaterials = m_considerMaterials;
    options.preparation.generateNormals = m_generateNormals;
    options.preparation.generateUVs = m_generateUVs;
    options.preparation.makeDoubleSided = m_makeDoubleSided;
    options.mergeColliders = m_mergeColliders;
    options.convexHullFallback = m_collisionApproximationFallback == CollisionApproximationFallback::eConvexHull;
    options.makeLocal = m_instanceMode == InstanceMode::eMakeLocal;
    for (const auto& entry : m_primvarDefaults)
    {
        const auto split = entry.find('=');
        if (split == std::string::npos || split == 0)
        {
            return { false, getCStr("Expected primvar default as name=value: " + entry) };
        }
        auto name = TfStringTrim(entry.substr(0, split));
        if (TfStringStartsWith(name, "primvars:"))
        {
            name = name.substr(9);
        }
        if (!SdfPath::IsValidNamespacedIdentifier(name) || name == "st" || name == "st0" || name == "normals")
        {
            return { false, getCStr("Expected a primvar name other than st, st0, or normals: " + name) };
        }
        options.preparation.primvarDefaults[TfToken(name)] = TfStringTrim(entry.substr(split + 1));
    }
    return physics_body_meshes::mergeMeshes(stage, getContext(), options);
}


/// Report whether candidate discovery is supported.
bool MergePhysicsBodyMeshesOperation::getSupportsAnalysis() const
{
    return true;
}


/// Return candidate physics body scopes without editing the stage.
OperationResult MergePhysicsBodyMeshesOperation::executeAnalysisImpl()
{
    const auto stage = getUsdStage();
    if (!stage)
    {
        return { false, getCStr("A stage is required to discover physics body meshes") };
    }

    // Resolve the traversal starts before collecting any physics body scopes.
    std::vector<PathIssue> rejectedPaths;
    const auto startPaths = _getEffectivePrimPaths(stage, m_paths, rejectedPaths);
    if (startPaths.empty())
    {
        return _makeAnalysisResult({}, {}, rejectedPaths);
    }

    // Find physics bodies at or below each traversal start.
    std::vector<PathIssue> skippedPaths;
    std::vector<UsdPrim> physicsBodies;
    for (const auto& path : startPaths)
    {
        const auto bodies = physics_body_meshes::getDescendantPhysicsBodies(stage->GetPrimAtPath(path), skippedPaths);
        physicsBodies.insert(physicsBodies.end(), bodies.begin(), bodies.end());
    }

    // Keep only bodies with candidate geometry, using connection boundaries shared across all bodies.
    const auto mergeLimits = physicsBodies.empty() ? physics_body_meshes::ConnectionMergeLimits{} :
                                                     physics_body_meshes::getMergeLimitsFromConnections(stage);
    std::map<UsdPrim, std::vector<physics_body_meshes::GeometryCandidate>> bodyGeometry;
    for (const auto& body : physicsBodies)
    {
        auto geometry = physics_body_meshes::getGeometryWithinPhysicsBody(body, mergeLimits, skippedPaths);
        if (!geometry.empty())
        {
            bodyGeometry.emplace(body, std::move(geometry));
        }
    }

    // Prepare and return the analysis report.
    return _makeAnalysisResult(bodyGeometry, skippedPaths, rejectedPaths);
}


} // namespace usd_optimize

USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::MergePhysicsBodyMeshesOperation);
