// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "SparseMeshes.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/Utils.h>
#include <omni/scene.optimizer/core/geometry/MeshProcessor.h>
#include <omni/scene.optimizer/core/geometry/SpatialClustering.h>

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

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::SparseMeshesOperation);


namespace omni::scene::optimizer
{


/// Constants
constexpr const char* s_category = "SPARSE_MESHES";


// Calculates and returns the distance between 2 bounding boxes by using the pre-calculated intersection of the bounds
static float _getIntersectionDistance(const GfRange3d& intersection)
{
    float distanceSq = 0.0f;
    const GfVec3d diff = intersection.GetMin() - intersection.GetMax();
    for (size_t i = 0; i < 3; ++i)
    {
        if (diff[i] > 0.0)
        {
            distanceSq += static_cast<float>(diff[i] * diff[i]);
        }
    }
    return sqrt(distanceSq);
}


SparseMeshesOperation::SparseMeshesOperation()
    : Operation("sparseMeshes",
                "Sparse Meshes",
                "Hidden operation used for analyzing the sparse meshes of a scene and suggesting optimizations")
{
}


std::string SparseMeshesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion SparseMeshesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string SparseMeshesOperation::getCategory() const
{
    return s_category;
}


std::string SparseMeshesOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


bool SparseMeshesOperation::getSupportsAnalysis() const
{
    return true;
}


bool SparseMeshesOperation::getVisible() const
{
    return false;
}


OperationResult SparseMeshesOperation::executeImpl()
{
    // operation is only intended for analysis mode - so return an error
    OperationResult result{ false, getCStr("SparseMeshesOperation is only intended to be used in analysis mode"), nullptr };
    return result;
}


OperationResult SparseMeshesOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|sparseMeshAnalysis");

    // constants that can be fine-tuned
    // ----------------------------------------------------------
    // Density threshold of disjoint meshes that will be considered sparse
    static const float s_disjointMeshDensityThreshold = 40.0f;
    // Multiplier for the median extent size to use when computing the spatial threshold for small disjoint meshes
    static const float s_smallSpatialThresholdMult = 2.0f;
    // Multiplier for the median extent size to use when computing the spatial threshold for large disjoint meshes
    static const float s_largeSpatialThresholdMult = 3.0f;
    // the threshold in relation to the median extent volume where a disjoint mesh is considered large
    static const float s_largeDisjointMeshThreshold = pow(s_largeSpatialThresholdMult, 3.0f);
    // Multiplier for the median extent size to use when computing the spatial max size for small disjoint meshes
    static const float s_smallSpatialMaxSizeMult = 3.0f;
    // Multiplier for the median extent size to use when computing the spatial max size for large disjoint meshes
    static const float s_largeSpatialMaxSizeMult = 10.0f;
    // the relative size of a mesh's extent volume in relation to the median extent volume that is considered a large
    // mesh - take the cube of the value to get a relative size
    static const float s_largeMeshThreshold = pow(15.0f, 3.0f);
    // the density threshold of large meshes that will be considered sparse
    static const float s_largeMeshDensityThreshold = 5.0f;
    // the multiplier for the median extent size to use when computing the dice size
    static const float s_diceSizeMultiplier = 10.0f;
    // ----------------------------------------------------------

    // use optimization structures for VirtualMesh
    VirtualMesh::OptLifetime optimizationLifetime;

    // setup mesh processor and run
    MeshProcessor meshProcessor({ getUsdStage()->GetPseudoRoot() }, // operate on the entire stage
                                SplitMeshesSplitOn::eVertices, // split on vertices
                                false, // do not split collocated points
                                true, // compute median extent volume
                                true // compute geometry volumes
    );
    meshProcessor.execute();
    const std::vector<MeshData>& processedMeshes = meshProcessor.getOutputMeshData();
    const float medianExtentVolume = meshProcessor.getMedianExtentVolume();
    const float medianExtentSize = std::pow(medianExtentVolume, 1.0f / 3.0f);
    const float smallSpatialThreshold = medianExtentSize * s_smallSpatialThresholdMult;
    const float largeSpatialThreshold = medianExtentSize * s_largeSpatialThresholdMult;

    // mutex to protect the result maps/lists
    std::mutex resultMutex;
    // map from prim paths to density of meshes that are disjoint and sparse
    std::map<std::string, float> disjointSparseMeshes;
    // map from prim paths to density of meshes that are considered large and sparse
    std::map<std::string, float> largeSparseMeshes;

    // simple struct to hold thread-safe lists of paths for clustering/splitting
    struct ThreadVector
    {
        std::vector<std::string> paths;
        std::mutex mutex;

        void add(const std::string& path)
        {
            std::lock_guard<std::mutex> lock(mutex);
            paths.push_back(path);
        }
    };
    // lists that will be used to categorize meshes based on how they should be split or clustered
    ThreadVector toSplit;
    ThreadVector toClusterLarge;
    ThreadVector toClusterSmall;

    // process each mesh in a multi-threaded context to determine if its large/sparse/disjoint
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, processedMeshes.size()),
        [&](tbb::blocked_range<size_t> r)
        {
            for (size_t i = r.begin(); i < r.end(); ++i)
            {
                const MeshData& meshData = processedMeshes[i];

                if (!meshData.valid)
                {
                    continue;
                }

                // if the mesh has disjoint subsets determine whether it needs to be split or clustered
                if (meshData.subsetMeshes.size() > 1)
                {
                    // perform a first pass to compute the extent bboxes of all the disjoint VirtualMesh children
                    std::vector<GfBBox3d> disjointBBoxes;
                    disjointBBoxes.reserve(meshData.subsetMeshes.size());
                    for (VirtualMesh disjointMesh : meshData.subsetMeshes)
                    {
                        // compute extent and store it as a bbox
                        disjointMesh.validateAndComputeExtent();
                        const VtVec3fArray& subsetExtent = disjointMesh.getWorldExtent();
                        disjointBBoxes.emplace_back(GfRange3f(subsetExtent[0], subsetExtent[1]));
                    }

                    // For each disjoint mesh compute how much of the base mesh's volume it occupies (minus any other
                    // disjoint meshes this intersects with) and also record the nearest disjoint mesh neighbour so we
                    // can determine clustering parameters afterwards.
                    // We do this by doing an ordered iteration over the disjoint meshes and for each mesh we only need
                    // to check the meshes that come after it in the array - this avoid taking intersections into
                    // account multiple times and doing un-needed nearest neighbor checks
                    float subsetExtentVolumeSum = 0.0f;
                    float minNearestNeighbor = std::numeric_limits<float>::infinity();
                    float maxNearestNeighbor = 0.0f;
                    for (size_t i = 0; i < disjointBBoxes.size(); ++i)
                    {
                        const GfBBox3d& bbox = disjointBBoxes[i];
                        double volume = bbox.GetVolume();
                        float nearestNeighbor = std::numeric_limits<float>::infinity();

                        // only iterate over disjoint bboxes we haven't checked yet
                        for (size_t j = i + 1; j < disjointBBoxes.size(); ++j)
                        {
                            const GfBBox3d& otherBbox = disjointBBoxes[j];
                            // compute intersection
                            GfRange3d intersection = GfRange3d::GetIntersection(bbox.GetRange(), otherBbox.GetRange());
                            double intersectionVolume = GfBBox3d(intersection).GetVolume();
                            // the bounds intersect if the volume of the bbox is non-zero
                            if (intersectionVolume >= std::numeric_limits<double>::epsilon())
                            {
                                // subtract the volume of this intersection (but we won't subtract it for the other
                                // bounding box since we never check the same pairs twice)
                                volume -= intersectionVolume;

                                // the nearest neighbour is intersecting
                                nearestNeighbor = 0.0f;
                            }
                            // otherwise check if this is the current nearest neighbour
                            else if (nearestNeighbor > 0.0f)
                            {
                                const float distance = _getIntersectionDistance(intersection);
                                if (distance < nearestNeighbor)
                                {
                                    nearestNeighbor = distance;
                                }
                            }
                        }

                        // add to the volume sum
                        subsetExtentVolumeSum += static_cast<float>(volume);

                        // update min and max nearest neighbors
                        if (nearestNeighbor != std::numeric_limits<float>::infinity())
                        {
                            if (nearestNeighbor < minNearestNeighbor)
                            {
                                minNearestNeighbor = nearestNeighbor;
                            }
                            if (nearestNeighbor > maxNearestNeighbor)
                            {
                                maxNearestNeighbor = nearestNeighbor;
                            }
                        }
                    }

                    // compute density of the disjoint meshes
                    float density = 0.0f;
                    // if the subset volumes are larger than the extent volume, we call it a density of 100%
                    if (subsetExtentVolumeSum >= meshData.extentVolume)
                    {
                        density = 100.0f;
                    }
                    // otherwise compute the density as a percentage of the geometry volume to extent volume
                    else
                    {
                        density = (subsetExtentVolumeSum / meshData.extentVolume) * 100.0f;
                    }

                    // if the density is below the threshold, we consider this a sparse disjoint mesh - otherwise skip
                    if (density >= s_disjointMeshDensityThreshold)
                    {
                        continue;
                    }

                    // compute the relative size of the mesh's extent volume in relation to the median extent volume
                    float relativeVolume = 1.0f;
                    if (medianExtentVolume > 0.0f)
                    {
                        relativeVolume = meshData.extentVolume / medianExtentVolume;
                    }

                    // resolve whether this is a large or small disjoint mesh based on the relative volume
                    ThreadVector* clusterVector = &toClusterLarge;
                    float spatialThreshold = largeSpatialThreshold;
                    if (relativeVolume < s_largeDisjointMeshThreshold)
                    {
                        clusterVector = &toClusterSmall;
                        spatialThreshold = smallSpatialThreshold;
                    }

                    // if the min and max nearest neighbors are not within the spatial threshold, we will just split it
                    if (minNearestNeighbor > spatialThreshold || maxNearestNeighbor < spatialThreshold)
                    {
                        clusterVector = &toSplit;
                    }

                    // add the mesh to the appropriate vector
                    clusterVector->add(meshData.baseMesh.getSourcePath().GetAsString());

                    // record the density for reporting
                    std::lock_guard<std::mutex> lock(resultMutex);
                    disjointSparseMeshes.emplace(meshData.baseMesh.getSourcePath().GetAsString(), density);
                }
                // mesh is not disjoint, determine if it needs dicing
                else
                {
                    // compute the relative size of the mesh's extent volume in relation to the median extent volume
                    float relativeVolume = 1.0f;
                    if (medianExtentVolume > 0.0f)
                    {
                        relativeVolume = meshData.extentVolume / medianExtentVolume;
                    }
                    if (relativeVolume < s_largeMeshThreshold)
                    {
                        // not a large mesh, skip
                        continue;
                    }

                    // compute the density of the mesh's actual geometry volume in relation to its extent volume
                    float density = 0.0f;
                    // if the geometry volume is larger than the extent volume, we call it a density of 100%
                    if (meshData.geometryVolume >= meshData.extentVolume)
                    {
                        density = 100.0f;
                    }
                    // otherwise compute the density as a percentage of the geometry volume to extent volume
                    else
                    {
                        density = (meshData.geometryVolume / meshData.extentVolume) * 100.0f;
                    }

                    // if the density is below the threshold, we consider it a sparse mesh
                    if (density < s_largeMeshDensityThreshold)
                    {
                        std::lock_guard<std::mutex> lock(resultMutex);
                        largeSparseMeshes.emplace(meshData.baseMesh.getSourcePath().GetAsString(), density);
                    }
                }
            }
        });

    // write the analysis as json
    JsObject analysisResult;
    analysisResult["medianExtentVolume"] = _toJson(medianExtentVolume);
    analysisResult["medianExtentSize"] = _toJson(medianExtentSize);
    analysisResult["largeMeshDensityThreshold"] = _toJson(s_largeMeshDensityThreshold);
    analysisResult["disjointSparseMeshes"] = _toJson(disjointSparseMeshes);
    analysisResult["largeSparseMeshes"] = _toJson(largeSparseMeshes);

    // write the suggested operations as json
    JsArray suggestedOperations;
    //  if we found any disjoint sparse meshes, suggest a split operation
    if (!disjointSparseMeshes.empty())
    {
        JsObject splitArgs;
        splitArgs["splitCollocatedPoints"] = _toJson(false);
        splitArgs["mergePoint"] = _toJson(static_cast<int>(MergePointOption::eXform));

        std::vector<JsValue> multiCluster;
        for (const std::string& path : toSplit.paths)
        {
            std::map<std::string, JsValue> clusteringArgs;
            std::vector<std::string> paths = { path };
            clusteringArgs["paths"] = _toJson(paths);
            clusteringArgs["spatialMode"] = _toJson(static_cast<int>(ClusterMode::eNone));
            multiCluster.emplace_back(clusteringArgs);
        }
        for (const std::string& path : toClusterSmall.paths)
        {
            std::map<std::string, JsValue> clusteringArgs;
            std::vector<std::string> paths = { path };
            clusteringArgs["paths"] = _toJson(paths);
            clusteringArgs["spatialMode"] = _toJson(static_cast<int>(ClusterMode::eBoundingBox));
            clusteringArgs["spatialThreshold"] = _toJson(smallSpatialThreshold);
            clusteringArgs["spatialMaxSize"] = _toJson(medianExtentSize * s_smallSpatialMaxSizeMult);
            multiCluster.emplace_back(clusteringArgs);
        }
        for (const std::string& path : toClusterLarge.paths)
        {
            std::map<std::string, JsValue> clusteringArgs;
            std::vector<std::string> paths = { path };
            clusteringArgs["paths"] = _toJson(paths);
            clusteringArgs["spatialMode"] = _toJson(static_cast<int>(ClusterMode::eBoundingBox));
            clusteringArgs["spatialThreshold"] = _toJson(largeSpatialThreshold);
            clusteringArgs["spatialMaxSize"] = _toJson(medianExtentSize * s_largeSpatialMaxSizeMult);
            multiCluster.emplace_back(clusteringArgs);
        }

        splitArgs["multiCluster"] = _toJson(JsWriteToString(multiCluster));

        JsObject splitConfig;
        splitConfig["name"] = _toJson("splitMeshes");
        splitConfig["args"] = splitArgs;
        suggestedOperations.push_back(splitConfig);
    }
    // if we found any large sparse meshes, suggest a dice operation
    if (!largeSparseMeshes.empty())
    {
        JsObject diceArgs;
        std::vector<std::string> dicePaths;
        for (const auto& pair : largeSparseMeshes)
        {
            dicePaths.push_back(pair.first);
        }
        diceArgs["paths"] = _toJson(dicePaths);
        diceArgs["splitDices"] = _toJson(false); // TODO: we should split dices when performance is better
        JsValue diceSize = _toJson(medianExtentSize * s_diceSizeMultiplier);
        diceArgs["gridCellX"] = diceSize;
        diceArgs["gridCellY"] = diceSize;
        diceArgs["gridCellZ"] = diceSize;

        JsObject diceConfig;
        diceConfig["name"] = _toJson("diceMeshes");
        diceConfig["args"] = diceArgs;
        suggestedOperations.push_back(diceConfig);
    }
    analysisResult["suggestedOperations"] = suggestedOperations;

    // write under the analysis key
    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    // return result
    OperationResult result{ true, nullptr, getCStr(JsWriteToString(resultJson)) };
    return result;
}


} // namespace omni::scene::optimizer
