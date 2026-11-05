// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "Merge.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/JsonUtils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>

// C++
#include <set>

PXR_NAMESPACE_USING_DIRECTIVE

USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::MergeOperation);


namespace usd_optimize
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((merged, "merged"))
);
// LCOV_EXCL_STOP
// clang-format on

/// Constants
constexpr const char* s_categoryMerge = "MERGE";


// Check whether a mesh below a prim, including inside instances, was left out, ignoring the prims in \p excluded and
// the meshes below them.
static bool _hasMeshLeftOut(const UsdPrim& prim, const std::set<SdfPath>& excluded)
{
    UsdPrimRange range(prim, UsdTraverseInstanceProxies(UsdPrimIsActive && UsdPrimIsLoaded));
    for (auto iter = range.begin(); iter != range.end(); ++iter)
    {
        if (*iter != prim && excluded.count(iter->GetPath()))
        {
            iter.PruneChildren();
            continue;
        }
        if (iter->IsA<UsdGeomMesh>())
        {
            return true;
        }
    }
    return false;
}


// Check whether discovery reached an instance's meshes where they are defined: the instance's only arcs are references
// to prims on this stage, and discovery found or skipped a prim below each of them. \p excluded holds those prims.
static bool _sourcesReached(const UsdPrim& instance, const std::set<SdfPath>& excluded)
{
    // Other arcs on the instance can add meshes that are not below any reference.
    if (instance.HasAuthoredPayloads() || instance.HasAuthoredInherits() || instance.HasAuthoredSpecializes() ||
        instance.HasVariantSets())
    {
        return false;
    }
    // As in discovery: a reference authored in another file targets a prim in that file, even with no asset path.
    const auto arcs = UsdPrimCompositionQuery::GetDirectReferences(instance).GetCompositionArcs();
    for (const UsdPrimCompositionQueryArc& arc : arcs)
    {
        const SdfPath target = arc.GetTargetNode().GetPath();
        auto findIt = excluded.lower_bound(target);
        if (arc.GetTargetNode().GetLayerStack() != instance.GetPrimIndex().GetRootNode().GetLayerStack() ||
            findIt == excluded.end() || !findIt->HasPrefix(target))
        {
            return false;
        }
    }
    return !arcs.empty();
}


// Log each prim merge left out, and why. The lines are verbose: they go to the report and, with the verbose flag, to
// the console. One INFO line says how many there are. \p merged holds the source paths of the meshes that merged.
static void _logSkippedMeshes(const UsdStageWeakPtr& stage,
                              const MergeBoundaryLookup& lookup,
                              const std::vector<VirtualMesh>& output,
                              const std::set<SdfPath>& merged)
{
    std::vector<SkippedPrim> skipped = lookup.skipped;

    // Meshes accounted for on their own, such as a point instancer's prototypes, do not make a skipped prim worth
    // logging: those merge wrote, those discovery found, and those skipped for their own reason.
    std::set<SdfPath> excluded;
    for (const VirtualMesh& mesh : output)
    {
        excluded.insert(mesh.getDestinationPath());
    }

    // Meshes that discovery found but that did not merge with another mesh are skipped too.
    for (const auto& [boundary, prims] : lookup.parentToPrims)
    {
        for (const UsdPrim& prim : prims)
        {
            excluded.insert(prim.GetPath());
            if (!merged.count(prim.GetPath()))
            {
                skipped.push_back({ prim.GetPath(),
                                    prims.size() == 1 ? "only mesh in its merge boundary" :
                                                        "no matching mesh in its merge boundary" });
            }
        }
    }
    for (const SkippedPrim& prim : lookup.skipped)
    {
        excluded.insert(prim.path);
    }

    std::set<SdfPath> logged;
    for (const auto& [path, reason] : skipped)
    {
        const UsdPrim prim = stage->GetPrimAtPath(path);
        if (!prim || logged.count(path) || (prim.IsInstance() && _sourcesReached(prim, excluded)) ||
            !_hasMeshLeftOut(prim, excluded))
        {
            continue;
        }
        logged.insert(path);
        USD_OPTIMIZE_LOG_VERBOSE("Not merged (%s): %s", reason.c_str(), path.GetAsString().c_str());
    }
    if (!logged.empty())
    {
        USD_OPTIMIZE_LOG_INFO("Left meshes unmerged at %s prim%s; generate a report or use verbose logging to see why",
                              std::to_string(logged.size()).c_str(),
                              logged.size() == 1 ? "" : "s");
    }
}


MergeOperation::MergeOperation()
    : Operation("merge", "Merge Static Meshes", "Merge individual meshes.")
{
    addArgument("meshPrimPaths",
                "Static Meshes to Merge",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider for merging",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    m_clustering.addConsiderMaterialsArg(this);
    m_clustering.addMaterialAlbedoAsVertexColorsArg(this);
    m_clustering.addOriginalGeomOptionArg(this);
    m_clustering.addMergePointArg(this);
    m_clustering.addRootPathArg(this).setPlaceholder("Merged_0");
    m_clustering.addConsiderAllAttributesArg(this);
    m_clustering.addAllowSingleMeshesArg(this);
    m_clustering.addSpatialModeArg(this, /*includeCoincidentBoundary*/ true);
    m_clustering.addSpatialThresholdArg(this, "spatialMode == 1");
    m_clustering.addSpatialMaxSizeArg(this, "spatialMode == 1");
    m_clustering.addSpatialVertexCountArg(this, "spatialMode == 2");
    m_clustering.addBoundaryToleranceArg(this, "spatialMode == 3");
    m_clustering.addBoundaryMinSharedVerticesArg(this, "spatialMode == 3");
    m_clustering.addTreatAsPrimvarsArg(this);
    m_clustering.addSpatialDebugArg(this);
}


MergeOperation::~MergeOperation(){};


std::string MergeOperation::getDocumentation() const
{
    return std::string(R"DOC(The merge static meshes operation replaces multiple meshes that
share common properties with a single merged mesh. This reduces scene prim count
and can improve overall stage performance.

Spatial clustering can further subdivide merges to keep merged meshes a
reasonable size. The supported spatial modes are: None (no spatial grouping),
BoundingBox (group meshes within a distance threshold, capping cluster size),
VertexCount (cap the vertex count per merged mesh), and CoincidentBoundary (only
merge meshes that share a seam).

In CoincidentBoundary mode two meshes are merged only when they share at least
boundaryMinSharedVertices boundary vertices that coincide in world space within
boundaryTolerance, where a boundary vertex is one lying on a boundary edge (an
edge used by exactly one face). Connectivity is transitive, so a chain of
abutting meshes collapses into one, while meshes that share no seam are left
untouched. This targets bad CAD output where a single part is exported as
several abutting pieces, such as a washer split into two half-rings.

Meshes left unmerged
--------------------

Merge says how many prims it left meshes unmerged at. Generate a report, or
enable verbose logging, to see each one and why, for example
``Not merged (time-sampled transform): /World/Anim``. Meshes skipped together,
such as those below an invisible prim, are listed once under that prim.

Recommended pipelines
---------------------

Merge then ``computeExtents`` (to author bounds on the new prims); merge then ``shrinkwrap`` to turn a
group of parts into one watertight shell.

Starting configurations
-----------------------

Merge everything by material:

.. code-block:: json

    [{"operation": "merge", "considerMaterials": true}]

Spatial merge capped by vertex count:

.. code-block:: json

    [{"operation": "merge", "spatialMode": 2, "spatialVertexCount": 50000}]
)DOC") + kMergePointDocumentation;
}


std::string MergeOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion MergeOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string MergeOperation::getCategory() const
{
    return s_categoryMerge;
}


std::string MergeOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


void MergeOperation::setUserData(void* userData)
{
    // Configure operation based on merge data
    auto mergeData = reinterpret_cast<MergeUserData*>(userData);
    m_clustering.setBucketer(mergeData->bucketer);
    m_clustering.m_considerSkeleton = mergeData->considerSkeleton;

    // Record for later - required for output
    m_userData = mergeData;
}


OperationResult MergeOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|MergeOperation|Execute");

    // build the lookup table for mesh merge boundaries which also contains the mesh prims to be merged
    MergeBoundaryLookup meshLookup = m_clustering.discoverMergeBoundaries(getUsdStage(), m_meshPrimPaths);

    m_clustering.setDefaultPrimName(_tokens->merged);

    // create VirtualMeshes for each mesh prim
    std::vector<VirtualMesh> virtualMeshes;
    UsdGeomXformCache xformCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
    for (const auto& it : meshLookup.parentToPrims)
    {
        for (const auto& prim : it.second)
        {
            virtualMeshes.emplace_back(prim, xformCache, bindingsCache, collQueryCache);
        }
    }

    // call common clustering module
    SdfLayerHandle editLayer = getUsdStage()->GetEditTarget().GetLayer();
    std::vector<UsdPrim> mergedPrims = m_clustering.execute(this, meshLookup, virtualMeshes, getUsdStage(), editLayer);

    // If there is userData, copy the merged prims as an output.
    if (m_userData != nullptr)
    {
        m_userData->mergedPrims = mergedPrims;
    }

    if (getContext()->generateReport)
    {
        for (const VirtualMesh& mesh : m_clustering.getOutput())
        {
            const std::vector<VirtualMesh>& children = mesh.getSupersetChildren();

            USD_OPTIMIZE_LOG_INFO("Output Mesh: %s contains %s",
                                  mesh.getDestinationPath().GetAsString().c_str(),
                                  std::to_string(children.size()).c_str());

            for (const VirtualMesh& child : children)
            {
                USD_OPTIMIZE_LOG_INFO(child.getSourcePath().GetAsString().c_str());
            }
        }
    }

    // Count the meshes that merged, by source path: a mesh with material subsets is a child once per subset.
    std::set<SdfPath> merged;
    for (const VirtualMesh& mesh : m_clustering.getOutput())
    {
        for (const VirtualMesh& child : mesh.getSupersetChildren())
        {
            merged.insert(child.getSourcePath());
        }
    }

    const auto plural = [](size_t count) { return count == 1 ? "" : "es"; };
    USD_OPTIMIZE_LOG_INFO("Merged %s mesh%s into %s mesh%s",
                          std::to_string(merged.size()).c_str(),
                          plural(merged.size()),
                          std::to_string(mergedPrims.size()).c_str(),
                          plural(mergedPrims.size()));

    _logSkippedMeshes(getUsdStage(), meshLookup, m_clustering.getOutput(), merged);

    // Create result
    OperationResult result{ true, nullptr, nullptr };

    // build the result to return
    if (!mergedPrims.empty())
    {
        result.output = _toJsonStr(mergedPrims);
    }

    return result;
}


} // namespace usd_optimize
