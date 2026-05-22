// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/UsdIncludes.h"
#include "omni/scene.optimizer/core/geometry/DisjointSet.h"
#include "omni/scene.optimizer/core/geometry/VirtualMesh.h"


namespace omni::scene::optimizer
{


/// How to consider what to split
enum class SplitMeshesSplitOn
{
    eVertices = 0, // Split on topology
    eGeomSubsets = 1, // Split on UsdGeom Subsets
};


/// Structure that holds data about a mesh that has been processed
struct MeshData
{
    bool valid = false; // whether this is valid data
    VirtualMesh baseMesh; // the VirtualMesh that this data is based on
    float extentVolume = 0.0f; // the volume of the mesh's extent
    float geometryVolume = 0.0f; // the volume of the mesh's geometry (if computed otherwise 0.0)
    std::vector<VirtualMesh> subsetMeshes; // the disjoint subset meshes that have been found in the base mesh
};


/// Helper class that processes meshes to find disjoint mesh subsets and scene and mesh volume data
class OMNI_SO_EXPORT MeshProcessor
{
public:
    /// Constructor
    /// \param prims The root prims, child mesh prims will be discovered under these prims and processed
    /// \param splitOn How to split the meshes, either on vertices or on UsdGeom Subsets
    /// \param splitCollocatedPoints Whether to consider collocated points as part of a disjoint mesh
    /// \param computeMedianExtentVolume Whether to compute the median extent volume of all meshes
    /// \param computeMeshVolumes Whether to compute the geometry volumes of the meshes
    MeshProcessor(const std::vector<PXR_NS::UsdPrim>& prims,
                  SplitMeshesSplitOn splitOn,
                  bool splitCollocatedPoints,
                  bool computeMedianExtentVolume = false,
                  bool computeMeshVolumes = false);

    /// Destructor
    ~MeshProcessor();

    /// Returns the median extent volume of all meshes processed
    /// note: this is only valid if `computeMedianExtentVolume` was set to true in the constructor and execute has been
    ///       called.
    float getMedianExtentVolume() const;

    /// Returns the resulting MeshData processed by `execute`
    const std::vector<MeshData>& getOutputMeshData() const;

    /// Runs the mesh processor
    void execute();

    /// Clears the internal data of this MeshProcessor
    void clear();

private:
    std::vector<PXR_NS::UsdPrim> m_inputPrims;
    SplitMeshesSplitOn m_splitOn;
    bool m_splitCollocatedPoints;
    bool m_computeMedianExtentVolume;
    bool m_computeMeshVolumes;

    // output data
    float m_medianExtentVolume = 0.0f;
    std::vector<MeshData> m_outputMeshData;
};


/// Find Disjoint Meshes
///
/// Given a meshData with a populated virtual mesh, and a disjoint set that has been initialized
/// with the faceVertexIndices, find any disjoint meshes.
///
/// After calling this function \p disjointSet is populated and can be queried.
///
/// \param meshData A MeshData object with a baseMesh configured
/// \param disjointSet Disjoint set initialized with the faceVertexIndices
/// \param splitCollocatedPoints Whether to split collocated points
OMNI_SO_EXPORT
void _findDisjointMeshes(const MeshData& meshData, DisjointSet& disjointSet, bool splitCollocatedPoints);


} // namespace omni::scene::optimizer
