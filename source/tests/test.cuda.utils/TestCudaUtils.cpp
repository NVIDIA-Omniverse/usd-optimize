// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#define TEST_CUDA_UTILS_EXPORTS

#include "TestCudaUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/CudaUtils.h>


extern "C"
{
    // Exported wrapper around isCudaAvailable() for cross-library thread-safety testing.
    // Since isCudaAvailable() now lives in the core plugin shared library, all callers
    // (including this helper library) share the same static state.
    TEST_CUDA_UTILS_API bool testUtilsIsCudaAvailable()
    {
        return omni::scene::optimizer::isCudaAvailable();
    }
}
