// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "SplitMeshes.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/geometry/DisjointSet.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/base/work/utils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdSkel/root.h>

// TBB
#include <tbb/parallel_for.h>

// C++
#include <algorithm>
#include <iostream>
#include <numeric>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::SplitMeshesOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_category = "SPLIT_MESHES";

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((diffuseColor, "diffuseColor"))
    ((Shader, "Shader"))
    ((surface, "surface"))
    ((usdPreviewSurface, "UsdPreviewSurface"))
    (ShadowAPI)
    (MaterialBindingAPI)
    (typeName)
    (specifier)
);
// LCOV_EXCL_STOP
// clang-format on


/// Splits the subset VirtualMeshes of the given mesh data into mesh data that should be split and clustered and mesh
/// data that should only be split
static void _separateSplitAndClusterMeshes(const MeshData& meshData,
                                           const MergeBoundaryLookup& meshLookup,
                                           ClusterMode clusterMode,
                                           std::vector<MeshData>& toSplit,
                                           std::vector<VirtualMesh>& toCluster)
{
    MeshData splitData;
    for (VirtualMesh subsetMesh : meshData.subsetMeshes)
    {
        // Check if this is in the merge boundary lookup - this will skip anything like invisible meshes
        // that shouldn't be merged.
        // Also check the mode, as individual configs can be "split only".
        if (meshLookup.primToParent.find(subsetMesh.getSourcePath()) != meshLookup.primToParent.end() &&
            clusterMode != ClusterMode::eNone)
        {
            toCluster.push_back(subsetMesh);
        }
        else
        {
            splitData.subsetMeshes.push_back(subsetMesh);
        }
    }
    // are there meshes to only split?
    if (!splitData.subsetMeshes.empty())
    {
        splitData.valid = true;
        splitData.baseMesh = meshData.baseMesh;
        toSplit.push_back(splitData);
    }
}


void SplitMeshesOperation::createMeshesFromDisjointMeshes(const std::vector<MeshData>& disjointMeshes)
{
    UsdStageWeakPtr stage = getUsdStage();

    // perform a first loop to set unique names on the new meshes (this needs to be done first since its not
    // SdfChangeBlock safe)
    for (const MeshData& meshData : disjointMeshes)
    {
        // Skip any input meshes that didn't contain multiple disjoint meshes.
        if (meshData.subsetMeshes.size() < 2)
        {
            continue;
        }

        const SdfPath& meshPath = meshData.baseMesh.getSourcePath();

        SO_LOG_INFO("%s contains %s disjoint meshes",
                    meshPath.GetAsString().c_str(),
                    std::to_string(meshData.subsetMeshes.size()).c_str());

        // Populate a vector with the required number of names so that all the unique paths can be computed at once.
        const TfToken baseName(meshPath.GetName() + "_part");
        const TfTokenVector preferredNames(meshData.subsetMeshes.size(), baseName);

        // Compute the unique paths for all the mesh parts.
        const SdfPath& parentPath = meshPath.GetParentPath();

        SdfPathVector uniquePaths = _getUniqueChildPaths(stage, parentPath, preferredNames);

        // set the unique paths on the subset virtual meshes
        size_t i = 0;
        for (VirtualMesh subsetMesh : meshData.subsetMeshes)
        {
            subsetMesh.setDestinationPath(uniquePaths[i++]);
        }
    }

    // list of prim paths to remove
    std::vector<SdfPath> removePaths;
    removePaths.reserve(disjointMeshes.size());

    // enter a SdfChangeBlock and create the meshes
    {
        SdfChangeBlock _changeBlock;
        SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
        for (const MeshData& meshData : disjointMeshes)
        {
            // Skip any input meshes that didn't contain multiple disjoint meshes.
            if (meshData.subsetMeshes.size() < 2)
            {
                continue;
            }

            for (VirtualMesh virtualMesh : meshData.subsetMeshes)
            {
                virtualMesh.createInLayer(stage, editLayer);
            }

            // record the original prim path to deactivate
            removePaths.push_back(meshData.baseMesh.getSourcePath());

            // update reporting counters
            m_numPrimsCreated += meshData.subsetMeshes.size();
            m_numPrimsRemoved += 1;
        }
    }

    // retrieve the actual prims that exist to remove
    std::vector<UsdPrim> removePrims;
    removePrims.reserve(removePaths.size());
    for (const SdfPath& path : removePaths)
    {
        UsdPrim prim = stage->GetPrimAtPath(path);
        if (prim)
        {
            removePrims.push_back(prim);
        }
    }

    // finally remove prims in a change block
    {
        SdfChangeBlock changeBlock;
        _removePrims(m_clustering.m_originalGeomOption, stage, removePrims);
    }
}


void SplitMeshesOperation::createSubsetsFromDisjointMeshes(const std::vector<MeshData>& disjointMeshes)
{
    // Track the max subsets across all prims so that we know how many Materials to define.
    size_t maxSubsets = 0;

    // Define prims to represent the Mesh subsets.
    // Depending on the "method" these could be of type "Mesh" or "Subset".
    std::map<SdfPath, std::vector<UsdPrim>> meshSubsetPrims;
    for (const MeshData& meshData : disjointMeshes)
    {
        // Skip any meshes that have one or no subsets.
        if (meshData.subsetMeshes.size() < 2)
        {
            continue;
        }

        const SdfPath& meshPath = meshData.baseMesh.getSourcePath();

        // TODO: Ensure we use unique names for subsets and that there are no existing subsets in the same family.
        size_t subsetIndex = 0;
        for (const VirtualMesh& subsetMesh : meshData.subsetMeshes)
        {
            // pull the face indices from the virtual mesh
            const std::vector<int>& faceVertexCountIndices = subsetMesh.getSubsetFaceVertexCountIndices();
            VtIntArray indices(faceVertexCountIndices.begin(), faceVertexCountIndices.end());

            // Construct the Subset.
            const auto& subsetName = "subset_" + std::to_string(subsetIndex);
            const auto& subsetPath = meshPath.AppendChild(TfToken(subsetName));
            UsdGeomSubset subset = UsdGeomSubset::Define(getUsdStage(), subsetPath);

            // Populate the attributes of the Subset.
            subset.GetElementTypeAttr().Set(UsdGeomTokens->face);
            subset.GetFamilyNameAttr().Set(UsdShadeTokens->materialBind);
            subset.GetIndicesAttr().Set(indices);

            // Stash the prim and move on to the next subset.
            meshSubsetPrims[meshPath].push_back(subsetMesh.getPrim());
            subsetIndex++;
        }

        maxSubsets = std::max(maxSubsets, subsetIndex);

        // update reporting counters
        m_numSubsetsCreated += meshData.subsetMeshes.size();
    }

    // Pick colors based on golden ratio for better distribution
    static constexpr float s_goldenRatio = 0.618033988749895f;
    // Starting color hue. Can be a fixed number for repeatable colors, or random.
    float hue = 0;

    // Create unique materials for each subset.
    SdfPath materialParentPath("/"); // TODO: Have user input for the materials parent.
    std::vector<SdfPath> materialPaths;
    for (size_t i = 0; i < maxSubsets; ++i)
    {
        // Generate the next "random" color.
        hue += s_goldenRatio;
        hue = fmodf(hue, 1.0);

        // Use the hue with a fixed saturation/value to have vibrant colors.
        GfVec3f color = _hsvToRgb(hue, 0.99f, 0.95f);

        // Construct the Material.
        // TODO: Get unused names rather than assuming there are no existing prims.
        const auto& materialName = "Material_" + std::to_string(i);
        const auto& materialPath = materialParentPath.AppendChild(TfToken(materialName));
        UsdShadeMaterial material = UsdShadeMaterial::Define(getUsdStage(), materialPath);

        // Construct the Shader.
        const auto& shaderPath = materialPath.AppendChild(_tokens->Shader);
        UsdShadeShader shader = UsdShadeShader::Define(getUsdStage(), shaderPath);

        // Set the Shader parameters and connect it to the Material
        shader.CreateIdAttr().Set(_tokens->usdPreviewSurface);
        shader.CreateInput(_tokens->diffuseColor, SdfValueTypeNames->Color3f).Set(color);
        material.CreateSurfaceOutput().ConnectToSource(shader.ConnectableAPI(), _tokens->surface);

        // Stash the Material so that we can bind it to multiple Mesh subsets.
        materialPaths.push_back(material.GetPrim().GetPath());
    }

    // Bind the Subsets to corresponding Materials
    for (const auto& iter : meshSubsetPrims)
    {
        for (size_t i = 0; i < iter.second.size(); ++i)
        {
            // Bind the prim to the material path at the matching index
            const UsdPrim& prim = iter.second[i];
            const SdfPath& materialPath = materialPaths[i];
            prim.CreateRelationship(UsdShadeTokens->materialBinding).SetTargets({ materialPath });
        }
    }
}


void SplitMeshesOperation::splitMeshes(const std::vector<UsdPrim>& prims, OperationResult& result)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|splitMeshes");

    // use optimization structures for VirtualMesh
    VirtualMesh::OptLifetime optimizationLifetime;

    // use the mesh processor to split meshes and compute volumes
    MeshProcessor meshProcessor(prims, m_splitOn, m_splitCollocatedPoints);
    meshProcessor.execute();
    std::vector<MeshData> disjointMeshes = meshProcessor.getOutputMeshData();
    meshProcessor.clear();

    // the start of the message used to report how many prims were created/removed
    std::string reportPrefix = "Splitting";

    // multiple cluster configuration?
    if (!m_clusterArgs.empty())
    {
        std::vector<VirtualMesh> mergeableMeshes;
        std::map<SdfPath, std::vector<VirtualMesh>> groupedMeshes;
        std::vector<MeshData> toSplit;

        bool multipleClusters = m_clusterArgs.size() > 1;

        // NOTE: this will no longer work if at some point support different merge points per cluster config - then we
        //       would need to group paths by their merge point
        // discover merge boundaries from the original stage
        ScopedTimer mergeBoundaryTimer("MultiClustering::Discover Merge Boundaries", s_category, LogLevel::eDebug);
        MergeBoundaryLookup meshLookup = m_clustering.discoverMergeBoundaries(getUsdStage(), m_paths);
        mergeBoundaryTimer.stop();

        // get mesh data

        // for better performance build a lookup table from prims paths to mesh data
        std::unordered_map<std::string, MeshData> meshDataLookup;
        for (auto& meshData : disjointMeshes)
        {
            meshDataLookup[meshData.baseMesh.getSourcePath().GetAsString()] = meshData;
        }

        // set up accumulative timers for the various parts of clustering
        ScopedTimer separateMeshesTimer("MultiClustering::Separate Meshes", s_category, LogLevel::eDebug, true);
        ScopedTimer bucketTimer("MultiClustering::Bucket", s_category, LogLevel::eDebug, true);

        for (const auto& config : m_clusterArgs)
        {
            // Update m_clustering with the current set of arguments.
            // In the case of a standard/single cluster this is pointless, but keeps the code simple when
            // processing multiple configs.
            m_clustering.m_spatialMaxSize = config.maxSize;
            m_clustering.m_spatialVertexCount = config.vertCount;
            m_clustering.m_spatialMode = config.mode;
            m_clustering.m_spatialThreshold = config.threshold;

            separateMeshesTimer.start();

            // separate meshes that are in the merge boundary look up (should be clustered) and those that aren't
            // (should only be split)
            std::vector<VirtualMesh> toCluster;
            // if we're using multiple clusters we can look up meshes by their paths for better performance
            if (multipleClusters)
            {
                for (const auto& path : config.paths)
                {
                    auto it = meshDataLookup.find(path);
                    if (it == meshDataLookup.end())
                    {
                        continue; // LCOV_EXCL_LINE
                    }
                    _separateSplitAndClusterMeshes(it->second, meshLookup, config.mode, toSplit, toCluster);
                }
            }
            else
            {
                for (auto& meshData : disjointMeshes)
                {
                    _separateSplitAndClusterMeshes(meshData, meshLookup, config.mode, toSplit, toCluster);
                }
            }

            separateMeshesTimer.pause();
            bucketTimer.start();

            // Bucket the prims.
            m_clustering.bucket(this, meshLookup, toCluster, getUsdStage(), groupedMeshes, mergeableMeshes);

            bucketTimer.pause();

            // Clear the bucketer. We're done with it, but also it needs to be null before calling bucket again
            // so that a new one will be created with the updated arguments.
            m_clustering.clearBucketer();
        }

        // Write the clustering results
        // Now that each config has been bucketed we can do the write in one go.
        ScopedTimer writeClusteredTimer("MultiClustering::Write Clustered", s_category, LogLevel::eDebug);
        SdfLayerHandle editLayer = getUsdStage()->GetEditTarget().GetLayer();
        m_clustering.write(groupedMeshes, mergeableMeshes, getUsdStage(), editLayer);
        writeClusteredTimer.stop();

        // update counters after clustering
        m_numPrimsCreated += m_clustering.getNumPrimsCreated();
        m_numPrimsRemoved += m_clustering.getNumPrimsRemoved();

        // write split-only results
        ScopedTimer writeSplitTimer("MultiClustering::Write Split", s_category, LogLevel::eDebug);
        if (!toSplit.empty())
        {
            createMeshesFromDisjointMeshes(toSplit);
        }
        writeSplitTimer.stop();

        // clear vectors so the following detached thread is cleaning up the last reference count of VirtualMesh
        // instances
        m_clustering.clearOutput();
        groupedMeshes.clear();
        WorkRunDetachedTask([p = std::move(mergeableMeshes)]() {});

        // include clustering in the report message
        reportPrefix += " and clustering";
    }
    else
    {
        // Everything is now calculated, so we can author something based on the method.
        switch (m_method)
        {
        case SplitMeshesMethod::eMeshPrim:
            createMeshesFromDisjointMeshes(disjointMeshes);
            break;
        case SplitMeshesMethod::eGeomSubset:
            createSubsetsFromDisjointMeshes(disjointMeshes);
            break;
        }
    }

    // determine plurals for report numbers
    const std::string createdPrimsStr = m_numPrimsCreated == 1 ? "prim" : "prims";
    const std::string removedPrimsStr = m_numPrimsRemoved == 1 ? "prim" : "prims";

    // report the number of prims created/removed
    SO_LOG_INFO("%s meshes created %u new %s and removed %u original %s",
                reportPrefix.c_str(),
                m_numPrimsCreated,
                createdPrimsStr.c_str(),
                m_numPrimsRemoved,
                removedPrimsStr.c_str());

    // deleting the mesh processor (and internal VirtualMeshes) is slow, so we do it in another thread so while the
    // rest of the operations / stage export can continue
    WorkRunDetachedTask([p = std::move(disjointMeshes)]() {});
}


SplitMeshesOperation::SplitMeshesOperation()
    : Operation("splitMeshes",
                "Split Meshes",
                "This operation determines whether meshes in a stage contain multiple disjoint mesh descriptions, "
                "specifically parts of a mesh that don't share any vertices. These can then be replaced with multiple "
                "mesh prims that contain just their part of the geometry.")
{

    addArgument("paths", "Meshes to split", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_paths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("splitOn", "Split On", kDisplayTypeEnum, "The method by which to detect disjoint meshes", m_splitOn)
        .setEnumValues<SplitMeshesSplitOn>(
            { { SplitMeshesSplitOn::eVertices, "Vertices" }, { SplitMeshesSplitOn::eGeomSubsets, "Geom Subsets" } });

    addArgument("method", "Output Method", kDisplayTypeEnum, "How to express mesh subsets in the stage", m_method)
        .setVisible(false)
        .setEnumValues<SplitMeshesMethod>(
            { { SplitMeshesMethod::eGeomSubset, "Geom Subsets" }, { SplitMeshesMethod::eMeshPrim, "Meshes" } });

    addArgument("splitCollocatedPoints",
                "Split Collocated Points",
                kDisplayTypeBool,
                "Should points that are collocated be considered part of a disjoint mesh",
                m_splitCollocatedPoints);

    m_clustering.addOriginalGeomOptionArg(this);

    // add spatial clustering arguments
    m_clustering.addSpatialModeArg(this).setEnableIf("method == 1");
    m_clustering.addConsiderMaterialsArg(this).setVisibleIf("spatialMode != 0").setEnableIf("spatialMode != 0");
    m_clustering.addMergePointArg(this).setVisibleIf("spatialMode != 0").setEnableIf("spatialMode != 0");
    m_clustering.addRootPathArg(this)
        .setPlaceholder("clustered")
        .setVisibleIf("spatialMode != 0")
        .setEnableIf("spatialMode != 0");
    m_clustering.addConsiderAllAttributesArg(this).setVisibleIf("spatialMode != 0").setEnableIf("spatialMode != 0");
    m_clustering.addSpatialThresholdArg(this, "spatialMode == 1 and method == 1");
    m_clustering.addSpatialMaxSizeArg(this, "spatialMode == 1 and method == 1");
    m_clustering.addSpatialVertexCountArg(this, "spatialMode == 2 and method == 1");
    m_clustering.addTreatAsPrimvarsArg(this);
    m_clustering.addSpatialDebugArg(this);

    addArgument("multiCluster", "Multi Cluster", kDisplayTypeText, "Multiple cluster configuration settings", m_multiCluster)
        .setVisible(false);
}


std::string SplitMeshesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion SplitMeshesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string SplitMeshesOperation::getCategory() const
{
    return s_category;
}


std::string SplitMeshesOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


void SplitMeshesOperation::setUserData(void* userData)
{
    m_userData = reinterpret_cast<SplitMeshesUserData*>(userData);
}


bool SplitMeshesOperation::processMultiClusterConfiguration()
{
    m_clusterArgs.clear();

    // Deal with the default case of no multi-cluster setup
    if (m_multiCluster.empty())
    {
        // If spatial clustering is enabled then create a single ClusterArgs instance
        // from the direct clustering arguments.
        if (m_clustering.m_spatialMode != ClusterMode::eNone && m_method != SplitMeshesMethod::eGeomSubset)
        {
            ClusterArgs& args = m_clusterArgs.emplace_back();
            args.paths = m_paths;
            args.mode = m_clustering.m_spatialMode;
            args.threshold = m_clustering.m_spatialThreshold;
            args.maxSize = m_clustering.m_spatialMaxSize;
            args.vertCount = m_clustering.m_spatialVertexCount;
        }

        return true;
    }

    // Ensure that paths is clear so we can replace it with the combined paths from all clusters
    m_paths.clear();

    JsValue multiCluster = JsParseString(m_multiCluster);

    if (!multiCluster.IsArray())
    {
        SO_LOG_ERROR("Invalid multiCluster attribute value specified, expected array");
        return false;
    }

    JsArray clusterArray = multiCluster.GetJsArray();

    // FYI there is little error-handling here, this is intended as an internal
    // optimization not a user-facing thing.
    m_clusterArgs.reserve(clusterArray.size());
    m_paths.reserve(clusterArray.size());
    for (const auto& config : clusterArray)
    {
        JsObject _config = config.GetJsObject();

        ClusterMode mode = static_cast<ClusterMode>(_config["spatialMode"].GetInt());

        ClusterArgs& args = m_clusterArgs.emplace_back();
        args.mode = mode;

        JsArray pathsArray = _config["paths"].GetJsArray();

        // Update m_paths with the combination of all paths - this will be used for disjoint set processing.
        // Also add to each of the per-cluster paths.
        args.paths.reserve(pathsArray.size());
        for (const auto& pathObject : pathsArray)
        {
            const std::string& path = pathObject.GetString();
            m_paths.emplace_back(path);
            args.paths.emplace_back(path);
        }

        // Only copy the various settings if clustering is enabled
        // Minor, just means they can be omitted from the JSON for clarity
        if (args.mode != ClusterMode::eNone)
        {
            JsValue threshold = _config["spatialThreshold"];
            if (threshold.IsReal())
            {
                args.threshold = threshold.GetReal();
            }

            JsValue maxSize = _config["spatialMaxSize"];
            if (maxSize.IsReal())
            {
                args.maxSize = maxSize.GetReal();
            }

            JsValue vertCount = _config["spatialVertexCount"];
            if (vertCount.IsInt())
            {
                args.vertCount = vertCount.GetInt();
            }
        }
    }

    return true;
}


void SplitMeshesOperation::clearCounters()
{
    m_numPrimsCreated = 0;
    m_numSubsetsCreated = 0;
    m_numPrimsRemoved = 0;
}


OperationResult SplitMeshesOperation::executeImpl()
{
    clearCounters();

    // is there user data? If so use that to override any arguments
    if (m_userData != nullptr)
    {
        m_paths = m_userData->paths;
        m_splitCollocatedPoints = m_userData->splitCollocatedPoints;
        m_userData = nullptr;
    }

    // Redundant operations.
    if (m_splitOn == SplitMeshesSplitOn::eGeomSubsets && m_method == SplitMeshesMethod::eGeomSubset)
    {
        SO_LOG_WARN("Cannot split on subsets and then author subsets.");
        return { false };
    }

    if (m_clustering.m_spatialMode != ClusterMode::eNone && m_method == SplitMeshesMethod::eGeomSubset)
    {
        SO_LOG_WARN("Cannot write results of spatial clustering to geom subsets.");
        return { false };
    }

    if (!processMultiClusterConfiguration())
    {
        return { false };
    }

    // Resolve paths to prims.
    std::vector<UsdPrim> prims;
    for (const auto& path : m_paths)
    {
        // Skip any paths that are not valid prim paths.
        // This means that we are not supporting regex, but that feature is a bit flaky anyway.
        if (!SdfPath::IsValidPathString(path))
        {
            continue;
        }

        // Check that the path points to a valid prim in the stage before adding it to the list of prims.
        const UsdPrim& prim = getUsdStage()->GetPrimAtPath(SdfPath(path));
        if (prim.IsValid())
        {
            prims.push_back(prim);
        }
    }

    // If no paths were provided start traversal from the pseudo root so that the whole stage is traversed.
    if (m_paths.empty())
    {
        prims.push_back(getUsdStage()->GetPseudoRoot());
    }

    // Prepare default result
    OperationResult result{ true, nullptr, nullptr };

    // If we have found one or more valid prims call overloaded function.
    if (!prims.empty())
    {
        splitMeshes(prims, result);
    }

    return result;
}

} // namespace omni::scene::optimizer
