// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>

// C++
#include <limits>


namespace omni::scene::optimizer
{


/// Print statistics about \p stage to stdout.
///
/// If \p verbose is enabled on the \p ExecutionContext then include some extra stats that may be slower to calculate
/// (for example counting disjoint meshes).
class StatsOperation : public Operation
{
public:
    /// Constructor
    StatsOperation();

    /// Set this operation invisible.
    bool getVisible() const override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Analysis mode
    bool getSupportsAnalysis() const override;

protected:
    /// Execute operation
    OperationResult executeImpl() override;

    /// Execute analysis
    OperationResult executeAnalysisImpl() override;

private:
    bool m_countPrimvars = false;
    bool m_splitCollocated = false;
    double m_time = std::numeric_limits<double>::quiet_NaN();
};

} // namespace omni::scene::optimizer
