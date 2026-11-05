// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MeshMerge.h"

#include "ColliderProperties.h"
#include "InstanceLocalization.h"
#include "MassProperties.h"
#include "MaterialBindings.h"
#include "MeshAuthoring.h"
#include "MeshCompatibility.h"
#include "MeshDiscovery.h"

#include <pxr/base/js/json.h>
#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdPhysics/massAPI.h>
#include <pxr/usd/usdPhysics/meshCollisionAPI.h>
#include <pxr/usd/usdPhysics/rigidBodyAPI.h>
#include <pxr/usd/usdPhysics/tokens.h>
#include <pxr/usd/usdUtils/stageCache.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/Log.h>
#include <usd_optimize/core/Operation.h>
#include <usd_optimize/core/Utils.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((materialBindingPhysics, "material:binding:physics"))
    (MergedVisuals)
    (MergedColliders)
);
// LCOV_EXCL_STOP
// clang-format on

namespace
{

/// An editable snapshot and the runtime context needed by existing preparation operations.
class WorkingStage
{
public:
    /// Create an editable stage snapshot with a context for reused preparation operations.
    WorkingStage(const UsdStageWeakPtr& source, ExecutionContext* parent)
        : stage(UsdStage::Open(source->Flatten()))
    {
        if (!stage)
        {
            throw std::runtime_error("Cannot create a working stage for physics mesh merging");
        }
        usd_optimize_execution_context_copy(&context, parent);
        usd_optimize_execution_context_free(&context);
        context.analysisMode = 0;
        context.generateReport = 0;
        m_cacheId = UsdUtilsStageCache::Get().Insert(stage);
        context.usdStageId = m_cacheId.ToLongInt();
    }

    /// Release the snapshot cache registration and execution context.
    ~WorkingStage()
    {
        UsdUtilsStageCache::Get().Erase(m_cacheId);
        usd_optimize_execution_context_free(&context);
    }

    /// Keep each stage snapshot under one owner.
    WorkingStage(const WorkingStage&) = delete;
    /// Prevent assignment from sharing a snapshot's cache registration.
    WorkingStage& operator=(const WorkingStage&) = delete;

    UsdStageRefPtr stage;
    ExecutionContext context;

private:
    UsdStageCache::Id m_cacheId;
};

/// Require success from a reused operation and release its C API result before continuing.
void _runOperation(WorkingStage& work, const char* name, const SdfPathVector& paths)
{
    if (paths.empty())
    {
        return;
    }
    auto operation = UsdOptimizeCore::getInstance().getOperation(name);
    if (!operation)
    {
        throw std::runtime_error(std::string("Required operation is unavailable: ") + name);
    }
    JsArray values;
    for (const auto& path : paths)
    {
        values.emplace_back(path.GetAsString());
    }
    auto result = operation->execute(&work.context, { { "paths", JsValue(values) } });
    const std::string error = result.error ? result.error : "operation failed";
    const bool success = result.success;
    usd_optimize_operation_result_free(&result);
    if (!success)
    {
        throw std::runtime_error(std::string(name) + ": " + error);
    }
}

/// Physics properties belong to the original collision representation, not its prepared visual copy.
void _removePhysics(UsdPrim prim)
{
    const auto schemas = physicsSchemas(prim);
    for (const auto& schema : schemas)
    {
        prim.RemoveAppliedSchema(schema);
    }
    for (const auto& property : prim.GetAuthoredProperties())
    {
        if (isPhysicsProperty(property.GetName().GetString()) || property.GetName() == _tokens->materialBindingPhysics)
        {
            prim.RemoveProperty(property.GetName());
        }
    }
}

/// Return the unique source prims contributing to one merged mesh.
std::vector<UsdPrim> _sourcePrims(const VirtualMesh& bucket, const UsdStageWeakPtr& stage)
{
    std::set<SdfPath> paths;
    for (const auto& child : bucket.getSupersetChildren())
    {
        paths.insert(child.getSourcePath());
    }
    std::vector<UsdPrim> prims;
    for (const auto& path : paths)
    {
        prims.push_back(stage->GetPrimAtPath(path));
    }
    return prims;
}

/// Exclude the retained output parent's visibility, which the new child will continue to inherit at every time.
TfToken _visibilityBelow(UsdPrim prim, const UsdPrim& parent)
{
    for (; prim && prim != parent; prim = prim.GetParent())
    {
        TfToken visibility;
        const UsdGeomImageable imageable(prim);
        if (imageable && imageable.GetVisibilityAttr().Get(&visibility) && visibility == UsdGeomTokens->invisible)
        {
            return visibility;
        }
    }
    return UsdGeomTokens->inherited;
}

/// Convert and prepare only candidates; originals are changed only for groups that actually merge.
void _mergeVisualScope(WorkingStage& visual,
                       WorkingStage& result,
                       MaterialBindings& materialBindings,
                       const UsdPrim& rigidBody,
                       const UsdPrim& outputParent,
                       const std::vector<GeometryCandidate>& candidates,
                       const MergeOptions& options,
                       std::map<SdfPath, SourceGeometry>& sources,
                       std::vector<MergedGeometry>& merged,
                       std::vector<PathIssue>& skipped)
{
    std::vector<UsdPrim> prims;
    SdfPathVector shapes;
    for (const auto& geometry : candidates)
    {
        if (geometry.role == GeometryRole::eCollider)
        {
            continue;
        }
        const auto& candidate = geometry.prim;
        const auto path = candidate.GetPath();
        auto reason = geometrySkipReason(candidate);
        if (reason.empty())
        {
            reason = transformSkipReason(candidate, outputParent);
        }
        if (!reason.empty())
        {
            skipped.push_back({ path.GetAsString(), reason });
            continue;
        }
        const auto prim = visual.stage->GetPrimAtPath(path);
        reason = candidate.IsA<UsdGeomMesh>() ? meshDataSkipReason(candidate) : primvarDataSkipReason(candidate);
        if (!reason.empty())
        {
            skipped.push_back({ path.GetAsString(), reason });
            continue;
        }
        resolveInheritedPrimvars(prim);
        const auto visibility = UsdGeomImageable(prim).CreateVisibilityAttr();
        visibility.Clear();
        visibility.Set(_visibilityBelow(candidate, outputParent));
        UsdGeomImageable(prim).CreatePurposeAttr().Set(UsdGeomImageable(prim).ComputePurpose());
        _removePhysics(prim);
        if (!prim.IsA<UsdGeomMesh>())
        {
            shapes.push_back(path);
        }
        prims.push_back(prim);
    }
    if (prims.size() < 2)
    {
        return;
    }
    _runOperation(visual, "primitivesToMeshes", shapes);
    prims.erase(std::remove_if(prims.begin(),
                               prims.end(),
                               /* Remove invalid working geometry before final compatibility grouping. */
                               [&](const UsdPrim& prim)
                               {
                                   const auto reason = meshDataSkipReason(prim);
                                   if (reason.empty())
                                   {
                                       return false;
                                   }
                                   skipped.push_back({ prim.GetPath().GetAsString(), reason });
                                   return true;
                               }),
                prims.end());
    for (const auto& group : bucketMeshes(visual.stage,
                                          &visual.context,
                                          prims,
                                          outputParent.GetPath(),
                                          options.preparation,
                                          options.considerMaterials,
                                          MeshMergePhase::ePreparation,
                                          skipped))
    {
        const auto groupPrims = _sourcePrims(group, visual.stage);
        if (groupPrims.size() < 2)
        {
            continue;
        }
        prepareMeshes(visual.context, outputParent.GetPath().GetAsString(), groupPrims, options.preparation);
    }
    for (const auto& prim : prims)
    {
        materialBindings.normalize(prim);
    }
    for (auto& bucket : bucketMeshes(visual.stage,
                                     &visual.context,
                                     prims,
                                     outputParent.GetPath(),
                                     options.preparation,
                                     options.considerMaterials,
                                     MeshMergePhase::eMerge,
                                     skipped))
    {
        const auto inputs = _sourcePrims(bucket, visual.stage);
        if (inputs.size() < 2 || !bucket.validateAndComputeExtent())
        {
            continue;
        }
        MergedGeometry output;
        output.rigidBody = rigidBody.GetPath();
        output.outputParent = outputParent.GetPath();
        output.output = uniqueChildPath(result.stage, output.outputParent, _tokens->MergedVisuals);
        bucket.setDestinationPath(output.output);
        auto layer = result.stage->GetRootLayer();
        bucket.createInLayer(result.stage, layer);
        copyMergeProperties(inputs.front(), result.stage->GetPrimAtPath(output.output));
        materialBindings.restore(result.stage->GetPrimAtPath(output.output));
        for (const auto& input : inputs)
        {
            const auto path = input.GetPath();
            output.sources.push_back(path);
            sources.at(path).visualMerged = true;
        }
        merged.push_back(std::move(output));
    }
}

/// Collider geometry has its own compatibility rules and mass calculation, independent of visual preparation.
void _mergeCollisionScope(WorkingStage& collision,
                          WorkingStage& result,
                          MaterialBindings& materialBindings,
                          const UsdPrim& rigidBody,
                          const UsdPrim& outputParent,
                          const std::vector<GeometryCandidate>& candidates,
                          const MergeOptions& options,
                          std::map<SdfPath, SourceGeometry>& sources,
                          std::vector<MergedGeometry>& merged,
                          std::vector<PathIssue>& skipped)
{
    std::vector<UsdPrim> prims;
    std::map<SdfPath, ColliderProperties> properties;
    std::map<SdfPath, VtDictionary> physicsValues;
    for (const auto& geometry : candidates)
    {
        const auto& candidate = geometry.prim;
        if (geometry.role == GeometryRole::eVisual || !candidate.IsA<UsdGeomMesh>())
        {
            continue;
        }
        auto reason = geometrySkipReason(candidate, true);
        if (reason.empty())
        {
            reason = meshDataSkipReason(candidate, true);
        }
        if (reason.empty())
        {
            reason = transformSkipReason(candidate, outputParent);
        }
        if (!reason.empty())
        {
            skipped.push_back({ candidate.GetPath().GetAsString(), reason });
            continue;
        }
        // Material resolution requires valid face indices, checked above.
        auto behavior = resolveColliderProperties(candidate, rigidBody, options.convexHullFallback);
        if (!behavior.skipReason.empty())
        {
            skipped.push_back({ candidate.GetPath().GetAsString(), behavior.skipReason });
            continue;
        }
        physicsValues.emplace(candidate.GetPath(), behavior.compatibility);
        properties.emplace(candidate.GetPath(), std::move(behavior));
        auto prim = collision.stage->GetPrimAtPath(candidate.GetPath());
        const auto visibility = UsdGeomImageable(prim).CreateVisibilityAttr();
        visibility.Clear();
        visibility.Set(_visibilityBelow(candidate, outputParent));
        materialBindings.normalize(prim);
        _removePhysics(prim);
        UsdGeomImageable(prim).CreatePurposeAttr().Set(UsdGeomTokens->guide);
        for (const auto& attr : prim.GetAuthoredAttributes())
        {
            if (TfStringStartsWith(attr.GetName().GetString(), "primvars:") || attr.GetName() == UsdGeomTokens->normals)
            {
                prim.RemoveProperty(attr.GetName());
            }
        }
        prims.push_back(prim);
    }
    for (auto& bucket : bucketMeshes(collision.stage,
                                     &collision.context,
                                     prims,
                                     outputParent.GetPath(),
                                     options.preparation,
                                     false,
                                     MeshMergePhase::eMerge,
                                     skipped,
                                     physicsValues))
    {
        const auto inputs = _sourcePrims(bucket, result.stage);
        if (inputs.size() < 2 || !bucket.validateAndComputeExtent())
        {
            continue;
        }
        const auto mass = combineMassProperties(rigidBody, outputParent, inputs);
        if (!mass.skipReason.empty())
        {
            for (const auto& input : inputs)
            {
                skipped.push_back({ input.GetPath().GetAsString(), mass.skipReason });
            }
            continue;
        }
        const auto& first = properties.at(inputs.front().GetPath());
        if (std::any_of(inputs.begin(),
                        inputs.end(),
                        /* Compare each collider with the representative group settings. */
                        [&](const UsdPrim& input)
                        { return properties.at(input.GetPath()).compatibility != first.compatibility; }))
        {
            continue;
        }
        const bool fallback =
            std::any_of(inputs.begin(),
                        inputs.end(),
                        /* Compare each collider with the representative group settings. */
                        [&](const UsdPrim& input)
                        { return properties.at(input.GetPath()).approximationSettings != first.approximationSettings; });
        MergedGeometry output;
        output.collider = true;
        output.rigidBody = rigidBody.GetPath();
        output.outputParent = outputParent.GetPath();
        output.output = uniqueChildPath(result.stage, output.outputParent, _tokens->MergedColliders);
        if (fallback && !options.convexHullFallback)
        {
            continue;
        }
        output.approximation = fallback ? UsdPhysicsTokens->convexHull : first.approximation;
        output.approximationChanged = fallback;
        bucket.setDestinationPath(output.output);
        auto layer = result.stage->GetRootLayer();
        bucket.createInLayer(result.stage, layer);
        const auto prim = result.stage->GetPrimAtPath(output.output);
        copyMergeProperties(collision.stage->GetPrimAtPath(inputs.front().GetPath()), prim);
        materialBindings.restore(prim);
        authorColliderProperties(prim, first, fallback);
        if (mass.properties)
        {
            authorMassProperties(prim, *mass.properties);
            output.mass = mass.properties;
        }
        for (const auto& input : inputs)
        {
            output.sources.push_back(input.GetPath());
            sources.at(input.GetPath()).collisionMerged = true;
        }
        merged.push_back(std::move(output));
    }
}

/// Build the source table before working representations are edited or converted.
std::map<SdfPath, SourceGeometry> _collectSources(const std::map<UsdPrim, std::vector<GeometryCandidate>>& geometryByBody)
{
    std::map<SdfPath, SourceGeometry> sources;
    for (const auto& [body, candidates] : geometryByBody)
    {
        for (const auto& candidate : candidates)
        {
            const bool isCollider = candidate.role != GeometryRole::eVisual;
            const bool hasVisualGeometry = candidate.role != GeometryRole::eCollider;
            sources.emplace(candidate.prim.GetPath(),
                            SourceGeometry{ body.GetPath(),
                                            candidate.mergeBoundary.GetPath(),
                                            isCollider,
                                            !candidate.prim.IsA<UsdGeomMesh>(),
                                            hasVisualGeometry });
        }
    }
    return sources;
}

/// Serialize successful merges and unchanged geometry with their reasons.
JsObject _report(const UsdStageWeakPtr& stage,
                 const std::vector<MergedGeometry>& merged,
                 const std::vector<PathIssue>& skipped)
{
    JsArray outputs;
    for (const auto& group : merged)
    {
        JsArray sources;
        for (const auto& path : group.sources)
        {
            sources.emplace_back(path.GetAsString());
        }
        const UsdGeomMesh mesh(stage->GetPrimAtPath(group.output));
        VtVec3fArray points;
        VtIntArray faces;
        mesh.GetPointsAttr().Get(&points);
        mesh.GetFaceVertexCountsAttr().Get(&faces);
        JsObject item{ { "output", JsValue(group.output.GetAsString()) },
                       { "rigidBody", JsValue(group.rigidBody.GetAsString()) },
                       { "outputParent", JsValue(group.outputParent.GetAsString()) },
                       { "role", JsValue(group.collider ? "collision" : "visual") },
                       { "vertexCount", JsValue(static_cast<uint64_t>(points.size())) },
                       { "faceCount", JsValue(static_cast<uint64_t>(faces.size())) },
                       { "sources", JsValue(sources) } };
        if (group.collider)
        {
            item["approximation"] = JsValue(group.approximation.GetString());
            if (group.approximationChanged)
            {
                item["approximationChange"] =
                    JsValue("convexHull fallback: source methods or approximation settings differ");
            }
            item["massSource"] = JsValue(group.mass ? "colliders" : "bodyOverride");
            if (group.mass)
            {
                const auto& mass = *group.mass;
                JsArray center, inertia, axes;
                for (int axis = 0; axis != 3; ++axis)
                {
                    center.emplace_back(static_cast<double>(mass.centerOfMass[axis]));
                    inertia.emplace_back(static_cast<double>(mass.diagonalInertia[axis]));
                }
                axes.emplace_back(static_cast<double>(mass.principalAxes.GetReal()));
                for (int axis = 0; axis != 3; ++axis)
                {
                    axes.emplace_back(static_cast<double>(mass.principalAxes.GetImaginary()[axis]));
                }
                item["mass"] = JsValue(JsObject{ { "mass", JsValue(static_cast<double>(mass.mass)) },
                                                 { "centerOfMass", JsValue(center) },
                                                 { "diagonalInertia", JsValue(inertia) },
                                                 { "principalAxes", JsValue(axes) } });
            }
        }
        outputs.emplace_back(item);
        USD_OPTIMIZE_LOG_INFO("Merged %s %s sources into %s (%zu vertices, %zu faces)",
                              std::to_string(group.sources.size()).c_str(),
                              group.collider ? "collision" : "visual",
                              group.output.GetAsString().c_str(),
                              points.size(),
                              faces.size());
    }
    JsArray unchanged;
    for (const auto& item : skipped)
    {
        unchanged.emplace_back(JsObject{ { "path", JsValue(item.path) }, { "reason", JsValue(item.issue) } });
        USD_OPTIMIZE_LOG_VERBOSE("Not merged (%s): %s", item.issue.c_str(), item.path.c_str());
    }
    return { { "merged", JsValue(outputs) }, { "skipped", JsValue(unchanged) } };
}

/// Reject unusable explicit defaults before any preparation, against each existing type they would need to fill.
void _validatePrimvarDefaults(const std::map<UsdPrim, std::vector<GeometryCandidate>>& geometryByBody,
                              const MergeOptions& options)
{
    for (const auto& [body, candidates] : geometryByBody)
    {
        for (const auto& candidate : candidates)
        {
            if (candidate.role == GeometryRole::eCollider)
            {
                continue;
            }
            for (const auto& primvar : UsdGeomPrimvarsAPI(candidate.prim).FindPrimvarsWithInheritance())
            {
                const auto found = options.preparation.primvarDefaults.find(primvar.GetPrimvarName());
                if (found != options.preparation.primvarDefaults.end() &&
                    parsePrimvarDefault(primvar.GetTypeName(), found->second).IsEmpty())
                {
                    throw std::runtime_error("Invalid default for " + primvar.GetAttr().GetPath().GetAsString() + " (" +
                                             primvar.GetTypeName().GetAsToken().GetString() + ")");
                }
            }
        }
    }
}

} // namespace

/// Plan merges on working stages, author successful groups, and return the change report.
OperationResult mergeMeshes(const UsdStageWeakPtr& stage, ExecutionContext* context, const MergeOptions& options)
{
    // No valid traversal starts means no discovery, preparation, or edits.
    if (options.startPaths.empty())
    {
        return { true, nullptr, getCStr(JsWriteToString(_report(stage, {}, {}))) };
    }

    // Discover geometry within each body before making any editable stage snapshots.
    std::vector<PathIssue> originalSkipped;
    discoverMeshes(stage, options.startPaths, originalSkipped);
    WorkingStage result(stage, context);
    std::vector<PathIssue> skipped;
    const auto localizedInstances =
        options.makeLocal ? makeInstancesLocal(stage, result.stage, options.startPaths, skipped) : SdfPathVector{};
    const auto localizationIssueCount = skipped.size();
    const auto geometryByBody = discoverMeshes(result.stage, options.startPaths, skipped);
    // Keep detailed localization failures instead of duplicate instance-preservation notices from discovery.
    const auto discoveryIssues = skipped.begin() + localizationIssueCount;
    skipped.erase(std::remove_if(discoveryIssues,
                                 skipped.end(),
                                 /* Suppress only discovery's generic notice for an unsafe instance. */
                                 [&](const PathIssue& issue)
                                 {
                                     return issue.issue == "instance hierarchy is preserved" &&
                                            std::any_of(skipped.begin(),
                                                        discoveryIssues,
                                                        /* Match a previously reported localization failure. */
                                                        [&](const PathIssue& localized)
                                                        { return localized.path == issue.path; });
                                 }),
                  skipped.end());
    for (const auto& skip : originalSkipped)
    {
        if (skip.issue == "unloaded payload hierarchy")
        {
            skipped.push_back(skip);
        }
    }
    _validatePrimvarDefaults(geometryByBody, options);
    auto sources = _collectSources(geometryByBody);
    if (sources.empty())
    {
        return { true, nullptr, getCStr(JsWriteToString(_report(stage, {}, skipped))) };
    }
    WorkingStage visual(result.stage, context);
    MaterialBindings visualBindings(visual.stage);
    std::vector<UsdPrim> visualPrims;
    for (const auto& [path, source] : sources)
    {
        visualPrims.push_back(visual.stage->GetPrimAtPath(path));
    }
    visualBindings.bake(visualPrims);
    std::vector<MergedGeometry> merged;
    for (const auto& [body, candidates] : geometryByBody)
    {
        for (const auto& [outputParent, group] : groupGeometryByMergeBoundary(candidates))
        {
            _mergeVisualScope(visual, result, visualBindings, body, outputParent, group, options, sources, merged, skipped);
        }
    }
    if (options.mergeColliders)
    {
        WorkingStage collision(result.stage, context);
        MaterialBindings collisionBindings(collision.stage, true);
        std::vector<UsdPrim> collisionPrims;
        for (const auto& [path, source] : sources)
        {
            if (source.collider && !source.shape)
            {
                collisionPrims.push_back(collision.stage->GetPrimAtPath(path));
            }
        }
        collisionBindings.bake(collisionPrims);
        for (const auto& [body, candidates] : geometryByBody)
        {
            for (const auto& [outputParent, group] : groupGeometryByMergeBoundary(candidates))
            {
                _mergeCollisionScope(collision,
                                     result,
                                     collisionBindings,
                                     body,
                                     outputParent,
                                     group,
                                     options,
                                     sources,
                                     merged,
                                     skipped);
            }
        }
    }
    for (const auto& [path, source] : sources)
    {
        std::string reason;
        if (source.visualApplicable && !source.visualMerged)
        {
            reason = "no compatible visual geometry within its output scope";
        }
        if (source.collider && !source.collisionMerged)
        {
            if (!reason.empty())
            {
                reason += "; ";
            }
            reason += !options.mergeColliders ? "collision merging is disabled" :
                      source.shape            ? "analytic collision shape is preserved" :
                                                "no compatible collider to merge within its output scope";
        }
        if (!reason.empty() && std::none_of(skipped.begin(),
                                            skipped.end(),
                                            /* Preserve an existing, more specific reason for this source. */
                                            [&](const PathIssue& skip) { return skip.path == path.GetAsString(); }))
        {
            skipped.push_back({ path.GetAsString(), reason });
        }
    }
    authorResults(stage, result.stage, sources, merged, localizedInstances);
    return { true, nullptr, getCStr(JsWriteToString(_report(stage, merged, skipped))) };
}

} // namespace usd_optimize::physics_body_meshes
