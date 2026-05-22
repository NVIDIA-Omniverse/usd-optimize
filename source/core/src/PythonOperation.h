// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/Operation.h"
#include "omni/scene.optimizer/core/PythonUtils.h"


namespace omni::scene::optimizer
{


/// Python Operation
///
/// This operation is intended to be instantiated multiple times, with a configurable set of
/// arguments and a python callback, as a generic base class for python plugins. It's included
/// in the core codebase as it should be easily accessible.
class PythonOperation : public Operation
{
public:
    /// Constructor
    explicit PythonOperation(const std::string& name,
                             const std::string& displayName,
                             const std::string& description,
                             PyObject* pyObject);

    /// Destructor
    ~PythonOperation() override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Returns whether or not this operation is visible.
    bool getVisible() const override;

    /// Overrides the base execute method since we need to do some extra work to call the python function
    OperationResult execute(ExecutionContext* context, const PXR_NS::JsObject& args) override;

    /// Get the category for reporting.
    std::string getCategory() const override;

protected:
    /// Unused since PythonOperation overrides `execute` directly
    OperationResult executeImpl() override;

private:
    RefCountedPyObject m_pyObject;
    std::string m_author;
    SOPluginVersion m_version;
    bool m_visible;
    std::unordered_map<std::string, PXR_NS::JsValue> m_argDefaults;
};


} // namespace omni::scene::optimizer
