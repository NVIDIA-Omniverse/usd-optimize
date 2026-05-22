// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>


namespace omni::scene::optimizer
{

/// Move internal instance prototypes to a user-specified namespace.
class OrganizePrototypesOperation : public Operation
{

public:
    /// Constructor
    explicit OrganizePrototypesOperation();

    /// Destructor
    ~OrganizePrototypesOperation() override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

private:
    std::string m_protosNamespace;
    // Number of immediate prototype ancestors to preserve
    int m_hierarchyLevels;
};


} // namespace omni::scene::optimizer
