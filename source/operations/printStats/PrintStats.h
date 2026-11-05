// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Usd Optimize Core
#include <usd_optimize/core/Operation.h>


namespace usd_optimize
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
    UsdOptimizePluginVersion getVersion() const override;

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
    bool m_useDefaultTime = true;
    double m_time = 0.0;
};

} // namespace usd_optimize
