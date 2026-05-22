// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#ifdef _MSC_VER
#    pragma warning(disable : 4996) // warnings that strncpy is unsafe
#endif

// C++
#include <iostream>

// Carbonite
#include <carb/Assert.h>


#ifdef _MSC_VER
#    define OMNI_SO_EXPORT __declspec(dllexport)
#else
#    define OMNI_SO_EXPORT __attribute__((visibility("default")))
#endif

#define _OMNI_SO_TO_STRING(X) #X
#define OMNI_SO_TO_STRING(X) _OMNI_SO_TO_STRING(X)


/// Operation Execution Context
/// C-types only to allow usage via Carbonite
struct ExecutionContext
{
    // Stage ID
    long int usdStageId = -1;

    // Execution Options
    int generateReport = 0;
    int verbose = 0;
    int singleThreaded = 0;
    int debug = 0;
    int captureStats = 0;
    int analysisMode = 0;
    // For future compatibility
    int unused1 = 0;
    int unused2 = 0;

    // Output report path
    char* reportPath = nullptr;
};


/// Operation Result
struct OperationResult
{
    // Overall operation success
    bool success = false;
    // Optional error message
    char* error = nullptr;
    // Optional arbitrary JSON output data
    char* output = nullptr;
};


/// Cleanup function for execution context
inline void so_execution_context_free(ExecutionContext* context)
{
    if (context->reportPath != nullptr)
    {
        free(context->reportPath);
        context->reportPath = nullptr;
    }
}


/// Copies the data from ExecutionContext b onto a
inline void so_execution_context_copy(ExecutionContext* a, ExecutionContext* b)
{
    // do nothing if either is null
    if (a == nullptr || b == nullptr)
    {
        return;
    }

    // ensure any data on a is freed first
    so_execution_context_free(a);

    a->usdStageId = b->usdStageId;
    a->generateReport = b->generateReport;
    a->verbose = b->verbose;
    a->singleThreaded = b->singleThreaded;
    a->debug = b->debug;
    a->captureStats = b->captureStats;
    a->analysisMode = b->analysisMode;
    a->unused1 = b->unused1;
    a->unused2 = b->unused2;
    if (b->reportPath != nullptr)
    {
        size_t len = strlen(b->reportPath) + 1;
        a->reportPath = (char*)malloc(sizeof(char) * len);
        strncpy(a->reportPath, b->reportPath, len - 1);
        a->reportPath[len - 1] = '\0';
    }
}


/// Cleanup function for result
inline void so_operation_result_free(OperationResult* result)
{
    if (result->output != nullptr)
    {
        free(result->output);
        result->output = nullptr;
    }

    if (result->error != nullptr)
    {
        free(result->error);
        result->error = nullptr;
    }
}


/// Simple semantic version description
struct SOPluginVersion
{
    int major;
    int minor;
    int rev;
};
