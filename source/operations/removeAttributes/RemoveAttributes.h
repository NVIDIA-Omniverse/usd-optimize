// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>


namespace omni::scene::optimizer
{

/// Remove Attributes
///
/// A simple operation to remove user-specified attributes from prims.
class RemoveAttributesOperation : public Operation
{

    /// Operation to perform on matching attributes
    ///
    /// \ref Mode::eRemove Removes the property from the current edit target.
    /// \ref Mode::eBlock Authors a block, such that the attribute exists but has no value.
    enum class Mode
    {
        eRemove = 0, //< Remove the attribute
        eBlock = 1, //< Block the attribute
    };

public:
    /// Constructor
    RemoveAttributesOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    /// Entry point
    OperationResult executeImpl() override;

private:
    Mode m_mode = Mode::eRemove;
    std::vector<std::string> m_primPaths;
    std::vector<std::string> m_attributes;
};


} // namespace omni::scene::optimizer
