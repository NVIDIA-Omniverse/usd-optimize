// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"

// C++
#include <chrono>
#include <condition_variable>
#include <functional>
#include <memory>
#include <mutex>


namespace omni::scene::optimizer
{

/// Class to coalesce events
///
/// An instance of the class is created with a callback function and a delay
/// in milliseconds. The instance can then be triggered, which will schedule
/// the callback to run after the delay.
///
/// If the instance is subsequently triggered again before the delay has
/// expired, then the callback is rescheduled. This continues until no
/// more triggers have been made within the delay, at which point the
/// callback will be executed.
///
/// It can be used to respond to events where you may have a high frequency
/// of similar events and only want to respond when the last one completes,
/// for example responding after a user has been interactively manipulating
/// the transform of an object which fires a lot of interim events, and you
/// don't necessarily want to respond to all of them.
///
/// The callback function, when executed, will be called on an arbitrary
/// thread. The intention is to allow triggering an instance of this class
/// from the main thread and not blocking.
class OMNI_SO_EXPORT Coalescer
{

public:
    /// Callback typedef
    using CallbackFn = std::function<void()>;

    /// Constructor
    ///
    /// \param callback The callback function to execute
    /// \param delay The delay to wait before executing the callback function
    Coalescer(const CallbackFn& callback, const std::chrono::milliseconds& delay);

    /// Destructor
    ///
    /// When this object is destroyed it will Coalescer::wait on any active thread
    /// to ensure that the object is still alive if a callback was scheduled to
    /// trigger. This means that destruction may block for a brief delay. If you
    /// prefer to avoid this you can manually wait.
    virtual ~Coalescer();

    /// Trigger the callback to be scheduled.
    ///
    /// This function can be called repeatedly, and the callback timer will be reset until it
    /// stops being called. Once the configured delay passes without this function being
    /// called again, the callback will be executed.
    void trigger();

    /// Return whether there is a callback currently scheduled.
    ///
    /// If a callback has been scheduled but has not yet executed this function will
    /// return true. If there is no scheduled callback, it will return false.
    ///
    /// \return Whether a callback is scheduled
    bool isActive();

    /// Cancel any active callback and invalidate.
    ///
    /// This function will mark this object cancelled. No new triggers will be accepted, and
    /// any scheduled callback will not execute. There may however still be an active thread
    /// running that requires this object to exist, so it is not safe to delete until either
    /// Coalescer::isActive returns false or Coalescer::wait has been called.
    void cancel();

    /// Wait for any internal thread to finish.
    ///
    /// Blocks and waits for any scheduled internal thread to complete. This function
    /// can be used prior to deleting the object in order to safely destroy it.
    void wait();

private:
    void executeCallback();

    CallbackFn m_callback;
    std::chrono::milliseconds m_delay;
    std::chrono::time_point<std::chrono::steady_clock> m_timestamp;
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_timerActive = false;
    bool m_cancelled = false;
};

/// Typedef
using CoalescerUPtr = std::unique_ptr<Coalescer>;


} // namespace omni::scene::optimizer
