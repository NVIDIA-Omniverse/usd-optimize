// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>
#include <omni/scene.optimizer/core/geometry/MeshProcessor.h>
#include <omni/scene.optimizer/core/geometry/SpatialClustering.h>


namespace omni::scene::optimizer
{


/// How to express the disjoint meshes identified
enum class SplitMeshesMethod
{
    eGeomSubset = 0, // Create UsdGeom Subsets
    eMeshPrim = 1, // Create UsdGeom Mesh Prims
};


/// Helper struct for running multiple configurations of clustering
struct ClusterArgs
{
    std::vector<std::string> paths;
    ClusterMode mode = ClusterMode::eNone;
    float threshold = SPATIAL_THRESHOLD;
    float maxSize = SPATIAL_MAX_SIZE;
    int vertCount = SPATIAL_VERTEX_COUNT;
};


/// Custom userdata for inter-plugin communication
struct SplitMeshesUserData
{
    std::vector<std::string> paths;
    bool splitCollocatedPoints = false;
};


/// Split Meshes Operation
///
/// Operation to detect disjoint objects within a mesh. It can be configured to split
/// the original mesh in to individual meshes, or create UsdGeomSubsets to visualize the
/// results.
class SplitMeshesOperation : public Operation
{

public:
    /// Constructor
    explicit SplitMeshesOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    /// Set custom user data.
    ///
    /// Expects a pointer to a valid \ref SplitMeshesUserData object.
    ///
    /// \param userData
    void setUserData(void* userData) override;

protected:
    /// Entry point
    OperationResult executeImpl() override;

private:
    /// Split the specified meshes.
    void splitMeshes(const std::vector<PXR_NS::UsdPrim>& prims, OperationResult& result);

    /// Create new meshes for disjoint meshes
    void createMeshesFromDisjointMeshes(const std::vector<MeshData>& disjointMeshes);

    /// Create UsdGeomSubsets to visualize disjoint meshes
    void createSubsetsFromDisjointMeshes(const std::vector<MeshData>& disjointMeshes);

    /// Check for and set up multi-cluster configuration.
    bool processMultiClusterConfiguration();

    /// Clears the counters used for reporting
    void clearCounters();

    // spatial clustering helper
    SpatialClustering m_clustering;

    std::vector<std::string> m_paths;
    SplitMeshesSplitOn m_splitOn = SplitMeshesSplitOn::eVertices;
    SplitMeshesMethod m_method = SplitMeshesMethod::eMeshPrim;
    bool m_splitCollocatedPoints = false;
    std::string m_multiCluster;
    std::vector<ClusterArgs> m_clusterArgs;

    // counters for reporting
    size_t m_numPrimsCreated = 0;
    size_t m_numSubsetsCreated = 0;
    size_t m_numPrimsRemoved = 0;

    SplitMeshesUserData* m_userData = nullptr;
};


} // namespace omni::scene::optimizer
