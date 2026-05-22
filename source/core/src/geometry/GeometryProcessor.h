// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"
#include "omni/scene.optimizer/core/geometry/VirtualMesh.h"


namespace omni::scene::optimizer
{


/// Helper object that can be used to process geometry in a USD stage for various configurable metrics and analysis
class OMNI_SO_EXPORT GeometryProcessor
{
public:
    /// Constructor
    ///
    /// \param stage The USD stage to process.
    /// \param inputPaths Optional list of prim paths to restrict processing to.
    GeometryProcessor(const PXR_NS::UsdStageWeakPtr& stage, const std::vector<std::string>& inputPaths = {});

    /// Destructor
    ~GeometryProcessor();

    /// Sets whether to compute the RTX mesh count
    void setComputeRtxMeshCount(bool state);

    /// Runs the geometry processor
    void execute();

    /// Clears the current results of the geometry processor
    void clear();

    /// Returns the list of RTX mesh prim paths found during processing
    /// \note This is only computed if `setComputeRtxMeshCount(true)` was called before `execute()`.
    const std::vector<PXR_NS::SdfPath>& getRtxMeshPrims() const;

    /// Returns the computed RTX acceleration structure count
    /// \note This is only computed if `setComputeRtxMeshCount(true)` was called before `execute()`.
    size_t getRtxAccelStructCount() const;

    /// Returns the computed RTX mesh count, not considering duplicates
    /// \note This is only computed if `setComputeRtxMeshCount(true)` was called before `execute()`.
    size_t getRtxMeshCount() const;

    /// Returns the number of unique RTX duplicate meshes found during processing
    /// \note This is only computed if `setComputeRtxMeshCount(true)` was called before `execute()`.
    size_t getRtxUniqueMeshCount() const;

private:
    PXR_NS::UsdStageWeakPtr m_stage;
    std::vector<std::string> m_inputPaths;

    // various options that control what processing is done
    bool m_computeRtxMeshCount = false;

    size_t m_rtxAccelStructCount = 0;
    std::vector<PXR_NS::SdfPath> m_rtxMeshPrims;
    std::vector<PXR_NS::SdfPath> m_cameras;
    std::vector<PXR_NS::SdfPath> m_invisibleGPrims;
    std::vector<PXR_NS::SdfPath> m_degenerateGPrims;
    std::vector<PXR_NS::SdfPath> m_badExtentsGPrims;
    std::unordered_map<size_t, std::set<PXR_NS::SdfPath>> m_rtxDuplicateHashes;

    // keeps track of the visited prims during scene traversal
    std::set<PXR_NS::SdfPath> m_visitedPrims;
    // keeps track of visited instance prototypes during scene traversal and the number of acceleration structures in
    // the prototype
    std::map<PXR_NS::SdfPath, size_t> m_visitedPrototypes;

    // internal function used to traverse a prim and its children in the stage for processing and returns the number of
    // acceleration structures found under the prim
    //
    // \param inPointInstancer Whether we are currently traversing under a point instancer prototype
    size_t traversePrims(const PXR_NS::UsdPrim& prim,
                         PXR_NS::UsdGeomXformCache& xformCache,
                         PXR_NS::UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                         PXR_NS::UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache,
                         bool inPointInstancer = false);

    // Traverse the prim of a instance prototype and returns the number of acceleration structures found.
    //
    // \param inPointInstancer Whether we are currently traversing a point instancer prototype
    size_t traverseInstancePrototype(const PXR_NS::UsdPrim& prototypePrim,
                                     PXR_NS::UsdGeomXformCache& xformCache,
                                     PXR_NS::UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                                     PXR_NS::UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache,
                                     bool inPointInstancer);

    // Records this prim path as an RTX mesh and inserts it into the duplicate hash map
    void recordRtxMesh(const PXR_NS::SdfPath& primPath, size_t hash);
};

} // namespace omni::scene::optimizer
