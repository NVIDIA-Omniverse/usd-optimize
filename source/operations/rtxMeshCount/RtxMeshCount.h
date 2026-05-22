// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>


namespace omni::scene::optimizer
{


/// Analysis operation for counting the number of RTX Meshes in the stage and how many are unique.
/// \note RTX refers to all geometry as meshes, however this operation inspects all point based prims and cameras since
///       these can count to towards the RTX mesh limit.
class RtxMeshCountOperation : public Operation
{
public:
    /// Constructor
    explicit RtxMeshCountOperation();

    /// Destructor
    ~RtxMeshCountOperation() override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    /// Returns whether or not this operation is visible.
    bool getVisible() const override;

    // Returns whether or not this operation supports analysis mode
    bool getSupportsAnalysis() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    std::vector<std::string> m_paths;
};

} // namespace omni::scene::optimizer
