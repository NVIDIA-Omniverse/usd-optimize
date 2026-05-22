// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>


namespace omni::scene::optimizer
{

class OptimizeSkelRootsOperation : public Operation
{
public:
    /// Constructor
    explicit OptimizeSkelRootsOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    std::string getCategory() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;
};

} // namespace omni::scene::optimizer
