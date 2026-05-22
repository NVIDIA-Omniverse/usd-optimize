// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/Report.h"
#include "omni/scene.optimizer/core/Types.h"
#include "omni/scene.optimizer/core/geometry/Cluster.h"
#include "omni/scene.optimizer/core/geometry/VirtualMesh.h"


namespace omni::scene::optimizer
{


// Typedefs
using PathAndPrim = std::pair<PXR_NS::SdfPath, PXR_NS::UsdPrim>;
using PathAndPrimVector = std::vector<PathAndPrim>;


// Helper class
struct PreBucket
{
    size_t hash = 0;
    VirtualMesh virtualMesh;
    PXR_NS::VtDictionary attributes;
    PXR_NS::VtDictionary authoredAttributes;
    PXR_NS::SdfPath boundMaterialPath;
    PXR_NS::SdfPath parentPath;
    int spatialCluster = INVALID_CLUSTER;

    PreBucket() = delete;

    PreBucket(VirtualMesh& virtualMesh_)
        : virtualMesh(virtualMesh_)
    {
    }
};


/// Class to manage bucketing VirtualMeshes
///
/// This class can be instantiated and then given a list of VirtualMeshs with a path they can be merged at via
/// \ref AddVirtualMeshes. This can be used multiple times to specify VirtualMeshs at various paths. When all the
/// VirtualMeshs have been added, calling \ref Bucket will perform the bucket.
class OMNI_SO_EXPORT Bucketer
{
public:
    /// Constructor
    explicit Bucketer(ExecutionContext* context);

    ~Bucketer();

    void Bucket(const PXR_NS::UsdStageWeakPtr& stage);

    /// Get the output Virtual meshes that were created by bucketing
    const std::vector<VirtualMesh>& GetOutputData() const;

    /// Set an optional Report object to append report data to.
    void SetReport(const std::shared_ptr<Report>& report);

    /// Set a custom output mesh name.
    void SetOutputName(const std::string& outputName);

    /// Set considerMaterials flag
    void SetConsiderMaterials(bool considerMaterials);

    /// Set considerPrimAttributes flag.
    ///
    /// If true all attributes authored on prims being bucketed will be considered value attributes, unless they are
    /// covered by the "ignore" or "authored" attribute criteria.
    ///
    /// Defaults to False during construction.
    void SetConsiderPrimAttributes(bool value);

    /// Add names to the list of attributes whose values influences the bucketing logic
    void AddValueAttributes(const PXR_NS::TfTokenVector& names);

    /// Add names to the list of attributes whose presence influences the bucketing logic
    void AddAuthoredAttributes(const PXR_NS::TfTokenVector& names);

    /// Add \p names to the criteria for attributes to ignore in the bucketing logic.
    void AddIgnoreAttributeNames(const PXR_NS::TfTokenVector& names);

    /// Add \p namespaces to the criteria for attributes to ignore in the bucketing logic.
    void AddIgnoreAttributeNamespaces(const PXR_NS::TfTokenVector& namespaces);

    /// Whether or not "merging" of single meshes should be allowed
    void SetAllowSingleMeshes(bool allow);

    /// Whether or not Mesh attributes should be populated on Buckets produced.
    ///
    /// If Mesh attributes are not collected then the data volume of each prim is considered to be 0.
    /// Meaning that there is no limit to the number of VirtualMeshes that can be placed in a single bucket.
    void SetCollectMeshInfo(bool value);

    /// Configure how spatial merging should work.
    ///
    /// See MergeOperation for a description of these arguments.
    ///
    /// \param mode The clustering mode to use
    /// \param threshold The max distance between neighboring meshes
    /// \param maxSize The max size of spatially clustered meshes
    void SetSpatialArgs(ClusterMode mode, double threshold, double maxSize);

    /// Whether the max data volume for buckets will be ignored
    ///
    /// note: this function can be removed once VirtualMesh supports SetCollectMeshInfo
    void setIgnoreDataVolume(bool state);

    /// Add the \p VirtualMeshes, which can be merged at \p rootPath, to the list of meshes to bucket.
    void AddVirtualMeshes(const std::vector<VirtualMesh>& virtualMeshes, const PXR_NS::SdfPath& parentPath);

private:
    class Impl;

    Impl* pImpl;
};

// Typedefs
using BucketerPtr = std::shared_ptr<Bucketer>;

// Populate the value, authored and ignore attributes for a bucketer in the manner that makes sense to merge geometry.
OMNI_SO_EXPORT
void _populateMergeBucketerAttributes(const BucketerPtr& bucketer, bool considerPrimAttributes);


} // namespace omni::scene::optimizer
