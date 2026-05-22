// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/RemovePrims.h>

// C++
#include <string>
#include <vector>


namespace omni::scene::optimizer
{


/// Find hidden (occluded) meshes using MeshTools.
class FindOccludedMeshesOperation : public Operation
{
public:
    /// Constructor
    FindOccludedMeshesOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Support Analysis
    bool getSupportsAnalysis() const override;

protected:
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    std::vector<std::string> m_meshPrimPaths;
    bool m_clustered = true;
    float m_minimumGapSize = 0.01f;
    float m_maximumGridResolution = 500.0f;
    RemoveMethod m_action = RemoveMethod::eHide;
    bool m_checkTransparency = false;
    std::vector<std::string> m_attributePaths;
    bool m_useGpu = true;
};

} // namespace omni::scene::optimizer
