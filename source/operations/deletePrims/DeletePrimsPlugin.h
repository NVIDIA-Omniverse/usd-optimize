// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>


namespace omni::scene::optimizer
{

/// Generate Projection UVs
class DeletePrimsOperation : public Operation
{
public:
    /// Constructor
    explicit DeletePrimsOperation();

    /// Destructor
    ~DeletePrimsOperation() override;

    /// Set this operation invisible.
    bool getVisible() const override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the reporting category
    std::string getCategory() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

private:
    std::vector<std::string> m_primPaths;
};


} // namespace omni::scene::optimizer
