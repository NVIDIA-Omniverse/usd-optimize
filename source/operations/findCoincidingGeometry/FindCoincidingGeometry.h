// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>
#include <omni/scene.optimizer/core/geometry/DeduplicateUtils.h>


namespace omni::scene::optimizer
{


/// Find groups of coinciding prims.
class FindCoincidingGeometryOperation : public Operation
{
public:
    /// Constructor
    explicit FindCoincidingGeometryOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    // Returns whether this operation supports analysis mode
    bool getSupportsAnalysis() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    PrimVectors computeCoincidingGeometry(const PrimVectors& equalMeshPrimSets);

    std::vector<std::string> m_paths;
    float m_tolerance = 0.001f;
    float m_offset = 0.0f;
    bool m_fuzzy = false;
    std::vector<std::vector<PXR_NS::UsdPrim>> m_coincidingPrims;
};


} // namespace omni::scene::optimizer
