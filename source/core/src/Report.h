// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/Log.h"

// C++
#include <condition_variable>
#include <fstream>
#include <queue>
#include <thread>


namespace omni::scene::optimizer
{


/// Report Object
///
/// Convenience class to maintain a handle to an output file and allow appending log information to it.
/// It's main purpose is to allow writing out easily parseable data that something (eg a UI) can read
/// and break down.
class Report
{

public:
    /// Constructor
    ///
    /// Create an uninitialized report, intended to be saved at \p path. Call \ref initialize before
    /// attempting to log messages.
    ///
    /// \param path The output filename to use
    OMNI_SO_EXPORT
    explicit Report(const std::string& path);

    /// Copy constructor
    OMNI_SO_EXPORT
    Report(const Report& other) = delete;

    /// Destructor
    OMNI_SO_EXPORT
    ~Report();

    /// Initialize the report for use.
    ///
    /// This function must be called before calling \ref log, otherwise the output handle will not be open.
    ///
    /// \return Success
    OMNI_SO_EXPORT
    bool initialize();

    /// Log a message
    ///
    /// Allows logging a message based on severity, with an arbitrary category attached. This category is intended
    /// to allow tools that read the log to provide filtering options for types of messages.
    ///
    /// The \p multiline flag allows a message to be wrapped by begin/end markers to enable explicit parsing of
    /// multiline/complex strings, eg bulk output.
    ///
    /// \param level The logging level
    /// \param category An arbitrary category to allow filtering messages
    /// \param message The log message
    /// \param multiline Whether the message should be treated as multiline
    OMNI_SO_EXPORT
    void log(LogLevel level, const std::string& category, const std::string& message, bool multiline = false);

private:
    std::string m_path;
    std::ofstream m_out;
    std::queue<std::string> m_queue;
    std::mutex m_queueMutex;
    std::condition_variable m_queueCv;
    std::thread m_outThread;
    bool m_shutdown = false;
};


} // namespace omni::scene::optimizer
