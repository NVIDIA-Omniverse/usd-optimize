// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/Coalescer.h"

// C++
#include <thread>


namespace omni::scene::optimizer
{


Coalescer::Coalescer(const CallbackFn& callback, const std::chrono::milliseconds& delay)
    : m_callback(callback)
    , m_delay(delay)
{
}


Coalescer::~Coalescer()
{
    // Cancel so no new triggers are accepted
    cancel();

    // Wait for any outstanding threads
    wait();
}


void Coalescer::trigger()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // If cancelled, do not schedule the callback
    if (m_cancelled)
    {
        return;
    }

    // Update last timestamp
    m_timestamp = std::chrono::steady_clock::now();

    // If there isn't a callback already scheduled then go ahead and queue one.
    if (!m_timerActive)
    {
        m_timerActive = true;
        std::thread(
            [this]()
            {
                // Execute the callback after the delay
                std::this_thread::sleep_for(m_delay);
                executeCallback();
            })
            .detach();
    }
}


bool Coalescer::isActive()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_timerActive;
}


void Coalescer::cancel()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cancelled = true;
}


void Coalescer::wait()
{
    std::unique_lock<std::mutex> lock(m_mutex);

    // If not active, we're done
    if (!m_timerActive)
    {
        return;
    }

    // Wait for the active thread to complete and invalidate the timer.
    m_cv.wait(lock, [this] { return !m_timerActive; });
}


void Coalescer::executeCallback()
{
    std::lock_guard<std::mutex> lock(m_mutex);

    // Cancelled, so abort.
    if (m_cancelled)
    {
        // Mark the timer inactive as we will not run or reschedule.
        // Then notify, in case we are being waited.
        m_timerActive = false;
        m_cv.notify_one();
        return;
    }

    auto delta = std::chrono::steady_clock::now() - m_timestamp;

    // First check whether we received another trigger() while we were sleeping.
    // If not, we're good to run the actual function.
    if (delta >= m_delay)
    {
        // Detach a new thread to execute the callback.
        std::thread([this]() { m_callback(); }).detach();

        // Reset timer
        m_timerActive = false;
        m_cv.notify_one();
    }
    else
    {
        // This means we received another trigger while we were waiting, so reset
        // the timer by rescheduling this thread for the delay minus the delta -
        // essentially letting us wait for m_delay since the last trigger.
        std::thread(
            [this](const std::chrono::milliseconds& _delta)
            {
                std::this_thread::sleep_for(m_delay - _delta);
                executeCallback();
            },
            std::chrono::duration_cast<std::chrono::milliseconds>(delta))
            .detach();
    }
}


} // namespace omni::scene::optimizer
