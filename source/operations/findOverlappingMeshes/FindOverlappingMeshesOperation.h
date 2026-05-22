// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "FindOverlappingMeshes.h"

// Scene Optimizer
#include <omni/scene.optimizer/core/OmniOperation.h>

namespace omni::scene::optimizer
{

/// Find overlapping meshes omni operation
class FindOverlappingMeshesOperation : public OmniOperation
{
public:
    FindOverlappingMeshesOperation();
    ~FindOverlappingMeshesOperation();

    // Public overrides
    std::string getAuthor() const override;
    SOPluginVersion getVersion() const override;
    std::string getCategory() const override;
    std::string getDisplayGroup() const override;
    bool getSupportsAnalysis() const override;

protected:
    // Protected overrides
    ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context& taskGroupContext) override;
    void executePost(const TotalStats& totalStats) override;
    OperationResult executeAnalysisImpl() override;

private:
    /// @brief Generates the overlap report.
    void generate();

    /// Parameters
    bool m_reportIslands = false; ///< Whether to report islands.
    // bool m_reportFullGroups = false;
    bool m_fullStageReport = false; ///< Whether to report full stage, even if specific paths are not provided.
    // bool m_visualization = true;
    MeshTools::ClashDetectorParameters m_detectorParameters; ///< Clash detector parameters.

    /// Hold a reference to a persistent overlapping mesh detection service
    FindOverlappingMeshes& m_meshOverlapService; ///< Reference to the overlap service.

    // Output
    PXR_NS::SdfPathVector m_overlappingPrimPaths; /// Paths of meshes that were found to overlap in generate().
    PXR_NS::JsArray m_overlapGroups; ///< Overlaps stored in groups (either pairs or islands depending on settings) for
                                     ///< reporting in executePost()).
};

} // namespace omni::scene::optimizer
