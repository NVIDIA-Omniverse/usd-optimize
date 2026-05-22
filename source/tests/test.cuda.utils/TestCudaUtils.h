// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#if defined(_WIN32)
#    ifdef TEST_CUDA_UTILS_EXPORTS
#        define TEST_CUDA_UTILS_API __declspec(dllexport)
#    else
#        define TEST_CUDA_UTILS_API __declspec(dllimport)
#    endif
#else
#    define TEST_CUDA_UTILS_API __attribute__((visibility("default")))
#endif

extern "C"
{
    // Exported wrapper that calls isCudaAvailable() from a separate shared library,
    // used to verify thread-safety of the cached result across library boundaries.
    TEST_CUDA_UTILS_API bool testUtilsIsCudaAvailable();
}
