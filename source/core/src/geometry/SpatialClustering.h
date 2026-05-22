// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Argument.h"
#include "omni/scene.optimizer/core/Operation.h"
#include "omni/scene.optimizer/core/RemovePrims.h"
#include "omni/scene.optimizer/core/Utils.h"
#include "omni/scene.optimizer/core/geometry/Bucket.h"
#include "omni/scene.optimizer/core/geometry/Cluster.h"
#include "omni/scene.optimizer/core/geometry/VirtualMesh.h"


namespace omni::scene::optimizer
{

// Typedefs
using PathToVirtualMeshes = std::map<PXR_NS::SdfPath, std::vector<VirtualMesh>>;


enum class MergePointOption
{
    eDefault = 0, // Use Pseudo root prim
    eXform = 1, // Use the first xformable parent
    eKindAssembly = 2, // Use the first parent of kind assembly
    eKindGroup = 3, // Use the first parent of kind group
    eKindComponent = 4, // Use the first parent of kind component
    eKindModel = 5, // Use the first parent of kind model
    eKindSubcomponent = 6, // Use the first parent of kind subcomponent
    eRootPrim = 7, // Use root prims
    eParentPrim = 8, // Use the first parent
    eOriginalPrim = 9, // Use the original prim that the meshes have been split from
};


/// Simple struct the provides a lookup from merge boundary paths to the mesh prims that are are candidates for merging
/// within that boundary. And provides lookup from mesh prim paths to their respective merge boundary paths
struct MergeBoundaryLookup
{
    // map from parent/merge boundary paths to the prims that are contained within the merge boundary
    std::map<PXR_NS::SdfPath, std::vector<PXR_NS::UsdPrim>> parentToPrims;
    // map from mesh prim paths to the parent/merge boundary paths they are under
    std::map<PXR_NS::SdfPath, PXR_NS::SdfPath> primToParent;
};

/// Constants
constexpr double SPATIAL_THRESHOLD = 10.0;
constexpr double SPATIAL_MAX_SIZE = 0.0;
constexpr int SPATIAL_VERTEX_COUNT = 10000;

// Helper class to be used by Operation that need to perform spatial clustering of meshes
class OMNI_SO_EXPORT SpatialClustering
{
public:
    // clustering parameters
    ClusterMode m_spatialMode = ClusterMode::eNone;
    bool m_considerMaterials = false;
    bool m_materialAlbedoAsVertexColors = false;
    RemoveMethod m_originalGeomOption = RemoveMethod::eDelete;
    std::string m_rootPath;
    MergePointOption m_mergePoint = MergePointOption::eDefault;
    bool m_considerAllAttributes = false;
    bool m_allowSingleMeshes = false;
    bool m_considerSkeleton = false;
    double m_spatialThreshold = SPATIAL_THRESHOLD;
    double m_spatialMaxSize = SPATIAL_MAX_SIZE;
    int m_spatialVertexCount = SPATIAL_VERTEX_COUNT;
    std::vector<std::string> m_treatAsPrimvars;
    bool m_spatialDebug = false;

    // constructor
    SpatialClustering();

    /// Adds the "Keep Materials Separate" argument to the given operation and returns a reference to it
    Argument& addConsiderMaterialsArg(Operation* operation);

    /// Adds the "Compute Display Colors" argument to the given operation and returns a reference to it
    Argument& addMaterialAlbedoAsVertexColorsArg(Operation* operation);

    /// Adds the "Original Mesh Handling" argument to the given operation and returns a reference to it
    Argument& addOriginalGeomOptionArg(Operation* operation);

    /// Adds the "Merge Boundary" argument to the given operation and returns a reference to it
    Argument& addMergePointArg(Operation* operation);

    /// Adds the "Output Name" argument to the given operation and returns a reference to it
    Argument& addRootPathArg(Operation* operation);

    /// Adds the "Strict Attribute Mode" argument to the given operation and returns a reference to it
    Argument& addConsiderAllAttributesArg(Operation* operation);

    /// Adds the "Allow Single Meshes" argument to the given operation and returns a reference to it
    Argument& addAllowSingleMeshesArg(Operation* operation);

    /// Adds the "Spatial Clustering Mode" argument to the given operation and returns a reference to it
    Argument& addSpatialModeArg(Operation* operation);

    /// Adds the "Spatial Threshold" argument to the given operation and returns a reference to it
    Argument& addSpatialThresholdArg(Operation* operation, const std::string& enableIf = "spatialMode == 1");

    /// Adds the "Spatial Max Size" argument to the given operation and returns a reference to it
    Argument& addSpatialMaxSizeArg(Operation* operation, const std::string& enableIf = "spatialMode == 1");

    /// Adds the "Spatial Vertex Count" argument to the given operation and returns a reference to it
    Argument& addSpatialVertexCountArg(Operation* operation, const std::string& enableIf = "spatialMode == 2");

    /// Adds the "Treat As Primvars" argument to the given operation and returns a reference to it
    Argument& addTreatAsPrimvarsArg(Operation* operation);

    /// Adds the "Spatial Debug Mode" argument to the given operation and returns a reference to it
    Argument& addSpatialDebugArg(Operation* operation);

    /// Sets the name to use for newly created clustered prims if the rootPath argument has not been specified
    /// This defaults to "clustered"
    void setDefaultPrimName(const PXR_NS::TfToken& name);

    /// Sets a pre-constructed instance of the Bucketer to use during spatial clustering (this will be cleared after
    /// \p execute or \p write are called)
    void setBucketer(BucketerPtr& bucketer);

    /// Clear an existing bucketer.
    void clearBucketer();

    /// Prepass that discovers all mesh prims that are candidates for merging and their merge boundaries and returns
    /// this data in the MergeBoundaryLookup which provides lookup and reverse lookup to mesh prims by merge boundary
    /// paths
    MergeBoundaryLookup discoverMergeBoundaries(const PXR_NS::UsdStageWeakPtr& stage,
                                                const std::vector<std::string>& primPaths);

    /// Performs bucketing for the given operation on a VirtualMeshes and writes the results to the given layer in the
    /// stage
    ///
    /// Calling this function is equivalent to calling \p bucket followed by \p write. They are provided so that
    /// multiple buckets can be processed prior to calling a single write to author the new data.
    ///
    /// \param operation The pointer to the Scene Optimizer operation which is calling this function - used for logging
    /// \param lookup MergeBoundaryLookup object which is used as look table to determine merge boundaries for clustered
    ///               VirtualMeshes.
    /// \param virtualMeshes lists of VirtualMeshes grouped by parent paths - where the parent path is the merge boundry
    ///                      of the VirtualMeshes.
    /// \param stage the Usd stage that the write layer is within.void SpatialClustering::clearBucketer()

    /// \param layer the Usd layer in the stage to write results to.
    ///
    /// \return the list of prims that were created as a result of clustering
    std::vector<PXR_NS::UsdPrim> execute(const Operation* operation,
                                         const MergeBoundaryLookup& lookup,
                                         const std::vector<VirtualMesh>& virtualMeshes,
                                         const PXR_NS::UsdStageWeakPtr& stage,
                                         PXR_NS::SdfLayerHandle& layer);

    /// See \p execute
    void bucket(const Operation* operation,
                const MergeBoundaryLookup& lookup,
                const std::vector<VirtualMesh>& virtualMeshes,
                const PXR_NS::UsdStageWeakPtr& stage,
                std::map<PXR_NS::SdfPath, std::vector<VirtualMesh>>& groupedMeshes,
                std::vector<VirtualMesh>& mergeableMeshes);

    /// See \p execute
    std::vector<PXR_NS::UsdPrim> write(std::map<PXR_NS::SdfPath, std::vector<VirtualMesh>>& groupedMeshes,
                                       std::vector<VirtualMesh>& mergeableMeshes,
                                       const PXR_NS::UsdStageWeakPtr& stage,
                                       PXR_NS::SdfLayerHandle& layer);

    /// Returns the output VirtualMeshes as a result of clustering
    const std::vector<VirtualMesh>& getOutput() const;

    /// Clears the output VirtualMeshes
    void clearOutput();

    /// Returns the number of prims created by clustering
    ///
    /// \note This is only valid after `write` has been called.
    size_t getNumPrimsCreated() const;

    /// Returns the number of prims removed by clustering
    ///
    /// \note This is only valid after `write` has been called.
    size_t getNumPrimsRemoved() const;

private:
    /// Reset the prim created/removed counters
    void clearCounters();

    // bucketer for merging
    BucketerPtr m_bucketer;
    // the default name to use for newly clustered prims
    PXR_NS::TfToken m_defaultPrimName;

    // the resulting VirtualMeshes after clustering
    std::vector<VirtualMesh> m_output;

    // counter for the number of new prims created by clustering
    size_t m_numPrimsCreated = 0;
    // counter for the number of prims removed by clustering
    size_t m_numPrimsRemoved = 0;
};

} // namespace omni::scene::optimizer
