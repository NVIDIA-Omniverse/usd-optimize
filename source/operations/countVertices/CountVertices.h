// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>

namespace omni::scene::optimizer
{

/// Count Vertices
///
/// Determine the vertex count of prims and produce output noting anything over the configured
/// thresholds.
class CountVerticesOperation : public Operation
{

public:
    /// Constructor
    CountVerticesOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get whether operation is visible
    bool getVisible() const override;

    /// Support analysis
    bool getSupportsAnalysis() const override;

protected:
    /// Entry point
    OperationResult executeImpl() override;

    /// Entry point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    uint64_t m_levelHigh = 100000;
    uint64_t m_levelVeryHigh = 500000;
    uint64_t m_levelExtreme = 1000000;
};


} // namespace omni::scene::optimizer
