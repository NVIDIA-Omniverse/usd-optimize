// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Log.h"


namespace omni::scene::optimizer
{

/// Check if a CUDA-capable GPU is available.
///
/// This function checks if there is at least one CUDA-capable GPU available on the system
/// by loading the CUDA driver library and querying for devices.
/// The result is cached after the first call for efficiency. Thread-safe.
///
/// NOTE: This function is defined in CudaUtils.cpp and lives in the core plugin shared
/// library so that the static state (once_flag + cached result) is shared across all
/// operation plugins. A header-only definition would give each plugin .so its own copy
/// of the statics, defeating the call_once caching.
///
/// \return true if a CUDA GPU is available, false otherwise
OMNI_SO_EXPORT bool isCudaAvailable();

} // namespace omni::scene::optimizer
