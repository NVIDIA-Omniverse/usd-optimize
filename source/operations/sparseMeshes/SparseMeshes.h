// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>


namespace omni::scene::optimizer
{


/// Sparse Meshes Operation
///
/// Special hidden operation that only performs analysis to find sparse meshes in the scene and suggest optimizations to
/// fix them.
class SparseMeshesOperation : public Operation
{
public:
    /// Constructor
    explicit SparseMeshesOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    // Returns whether or not this operation supports analysis mode
    bool getSupportsAnalysis() const override;

    /// Returns whether or not this operation is visible.
    bool getVisible() const override;

protected:
    /// Entry point
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;
};


} // namespace omni::scene::optimizer
