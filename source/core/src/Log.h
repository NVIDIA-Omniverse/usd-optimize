// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"

// Carbonite
#include <carb/logging/Log.h>


namespace omni::scene::optimizer
{

/// Free function entry point for the SO_LOG_X macros.
///
/// This is intentionally a free function (rather than a static member of Operation) so that
/// lightweight headers such as CudaUtils.h can use the SO_LOG macros without pulling in the
/// full Operation / USD dependency chain.
///
/// @param level The level. See carb::logging::kLevelVerbose etc.
/// @param fmt The format string
OMNI_SO_EXPORT void soLog(int32_t level, const char* fmt, ...);

} // namespace omni::scene::optimizer

#define SO_LOG(level, fmt, ...) omni::scene::optimizer::soLog(level, fmt, ##__VA_ARGS__);

#define SO_LOG_VERBOSE(fmt, ...) SO_LOG(carb::logging::kLevelVerbose, fmt, ##__VA_ARGS__)
#define SO_LOG_INFO(fmt, ...) SO_LOG(carb::logging::kLevelInfo, fmt, ##__VA_ARGS__)
#define SO_LOG_WARN(fmt, ...) SO_LOG(carb::logging::kLevelWarn, fmt, ##__VA_ARGS__)
#define SO_LOG_ERROR(fmt, ...) SO_LOG(carb::logging::kLevelError, fmt, ##__VA_ARGS__)
#define SO_LOG_FATAL(fmt, ...) SO_LOG(carb::logging::kLevelFatal, fmt, ##__VA_ARGS__)

namespace omni::scene::optimizer
{

/// Log Level
enum class LogLevel
{
    eDebug = 0, // Debug message
    eInfo = 1, // General useful info message
    eWarning = 2, // Warning message
    eError = 3, // Error
};


/// Convert SO log level enum to a carb int.
///
/// \param level The SO log level
/// \return The carb logging level
inline int32_t carbLevelFromLogLevel(const LogLevel level)
{
    switch (level)
    {
    case LogLevel::eDebug:
        return carb::logging::kLevelVerbose;
    case LogLevel::eInfo:
        return carb::logging::kLevelInfo;
    case LogLevel::eWarning:
        return carb::logging::kLevelWarn;
    case LogLevel::eError:
        return carb::logging::kLevelError;
    }

    // Fall back to the "default" carb level
    return carb::logging::kLevelWarn;
}

} // namespace omni::scene::optimizer
