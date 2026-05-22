// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

/*! @file
Public API functions to pass JSON configuration data to Scene Optimizer in
order to execute operations.
*/

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"


namespace omni::scene::optimizer
{

/// Execute one or more operations on a stage based on a JSON configuration.
///
/// The \a str parameter can either be a path to a file on disk containing JSON
/// or it can be a JSON string.
///
/// If no ExecutionContext is provided then a default one will be created.
///
/// \param usdStage The Usd Stage to operate on.
/// \param str Filepath or JSON data
/// \param context The execution context
/// \return Success
OMNI_SO_EXPORT
bool _parseJson(const PXR_NS::UsdStageWeakPtr& usdStage, const std::string& str, ExecutionContext* context = nullptr);


/// Execute one or more operations on a stage based on the contents of a JSON document.
///
/// The JSON document is expected to be an Array containing a number of commands to
/// execute.
///
/// If no ExecutionContext is provided then a default one will be created.
///
/// \param usdStage The Usd Stage to operate on.
/// \param document A JSON document
/// \param context The execution context
OMNI_SO_EXPORT
bool _parseJson(const PXR_NS::UsdStageWeakPtr& usdStage,
                const PXR_NS::JsValue& document,
                ExecutionContext* context = nullptr);

} // namespace omni::scene::optimizer
