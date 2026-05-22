// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Report.h"

// C++
#include <iostream>
#include <mutex>
#include <sstream>


namespace omni::scene::optimizer
{


/// Constants
constexpr const char* s_debug("DEBUG");
constexpr const char* s_info("INFO");
constexpr const char* s_warning("WARNING");
constexpr const char* s_error("ERROR");

constexpr const char* s_multilineBegin("[[");
constexpr const char* s_multilineEnd("]]");

Report::Report(const std::string& path)
    : m_path(path)
{
}


Report::~Report()
{
    // Mark shutdown and join thread
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_shutdown = true;
    }

    m_queueCv.notify_one();
    if (m_outThread.joinable())
    {
        m_outThread.join();
    }

    // Close output stream
    m_out.close();
}


bool Report::initialize()
{
    // Open the stream
    m_out.open(m_path, std::ios::out | std::ios::app);

    // Validate it was opened successfully
    if (!m_out.is_open())
    {
        return false;
    }

    // Create background thread to monitor message queue
    // This allows maintaining a single thread to write to the output file, otherwise
    // we can end up corrupting the stream.
    m_outThread = std::move(std::thread(
        [&]
        {
            while (true)
            {
                std::unique_lock<std::mutex> lock(m_queueMutex);
                m_queueCv.wait(lock, [&]() { return !m_queue.empty() || m_shutdown; });

                if (m_shutdown)
                {
                    // Flush queue, just in case anything is left
                    while (!m_queue.empty())
                    {
                        auto msg = m_queue.front();
                        m_queue.pop();
                        m_out << msg;
                    }

                    return;
                }

                // Write next message
                auto msg = m_queue.front();
                m_queue.pop();
                lock.unlock();
                m_out << msg;
            }
        }));

    return true;
}

void Report::log(LogLevel level, const std::string& category, const std::string& message, bool multiline)
{
    // Create final string that can be logged
    std::ostringstream oss;

    // Start with the severity
    switch (level)
    {
    case LogLevel::eDebug:
        oss << s_debug;
        break;
    case LogLevel::eInfo:
        oss << s_info;
        break;
    case LogLevel::eWarning:
        oss << s_warning;
        break;
    case LogLevel::eError:
        oss << s_error;
        break;
    }

    // If multi-line is enabled then append the begin token and a newline. After that the message gets dumped
    // as-is, then we can close out the multi line token.
    if (multiline)
    {
        oss << "|" << category << "|" << s_multilineBegin << "\n";
        oss << message << "\n" << s_multilineEnd << "\n";
    }
    else
    {
        oss << "|" << category << "|" << message << "\n";
    }

    // Append to queue and then notify
    {
        std::lock_guard<std::mutex> lock(m_queueMutex);
        m_queue.push(oss.str());
    }
    m_queueCv.notify_one();
}


} // namespace omni::scene::optimizer
