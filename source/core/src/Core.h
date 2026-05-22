// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"

// carb
#include <carb/logging/ILogging.h>
#include <carb/logging/Log.h>

// USD
#include <pxr/base/js/value.h>

// C++
#include <functional>
#include <vector>


// Convenience macro to register/deregister a plugin
// Place inside your plugin .cpp!
#define SO_PLUGIN_INIT(OperationT)                                                                                      \
    extern "C" OMNI_SO_EXPORT bool sceneOptimizerPluginInit()                                                           \
    {                                                                                                                   \
        using namespace omni::scene::optimizer;                                                                         \
        auto& core = SceneOptimizerCore::getInstance();                                                                 \
        core.registerOperation(&sceneOptimizerOperationCreate<OperationT>, &sceneOptimizerOperationDelete<OperationT>); \
        return true;                                                                                                    \
    }


namespace omni::scene::optimizer
{

class Operation;

/// The sceneOptimizerOperationDelete template functions will be instantiated via
/// invocations of the SO_PLUGIN_INIT macro but can also be used for manual Operation registration
template <typename OperationT>
static void sceneOptimizerOperationDelete(Operation* operation)
{
    static_assert(std::is_base_of<Operation, OperationT>::value, "Must be used for operation construction");

    OperationT* op = static_cast<OperationT*>(operation);
    delete op;
}

/// The sceneOptimizerOperationCreate template functions will be instantiated via
/// invocations of the SO_PLUGIN_INIT macro but can also be used for manual Operation registration
template <typename OperationT>
static Operation* sceneOptimizerOperationCreate()
{
    static_assert(std::is_base_of<Operation, OperationT>::value, "Must be used for operation construction");

    return new OperationT();
}

/// Function objects for creating and destroying Operations inside the correct DLL.
using SceneOptimizerOperationCreate = std::function<Operation*()>;
using SceneOptimizerOperationDestroy = std::function<void(Operation*)>;

/// The OperationUPtr is a "managed" pointer, holding an Operation instance and a callback
/// for deleting it correctly
using OperationUPtr = std::unique_ptr<Operation, SceneOptimizerOperationDestroy>;

/// Scene Optimizer Plugin Manager
///
/// This class is a singleton that allows plugins to register themselves with the scene optimizer.
class OMNI_SO_EXPORT SceneOptimizerCore
{

public:
    /// Get the scene optimizer core object.
    static SceneOptimizerCore& getInstance();

    /// Returns whether the core has been initialised yet - aka loadPlugins has been called.
    bool isInitialized() const;

    /// Get all registered operations
    std::vector<std::string> getOperations() const;

    /// Find the specified operation.
    OperationUPtr getOperation(const std::string& name) const;

    /// Register an operation
    void registerOperation(SceneOptimizerOperationCreate creation, SceneOptimizerOperationDestroy destruction);

    /// Deregister an operation
    void deregisterOperation(const std::string& name);

    /// Load a plugin from a library path
    void loadPlugin(const std::string& pluginPath);

    /// Loads plugins contained in a directory
    void loadPluginsFromPath(const std::string& path);

    /// Load the plugins that ship with scene optimizer and from any paths defined in the SCENE_OPTIMIZER_PLUGIN_PATH
    /// environment variable
    void loadPlugins();

    /// Map the arguments of an individual operation to deal with renamed args.
    PXR_NS::JsObject mapOperation(const std::string& name, const PXR_NS::JsObject& config) const;

    /// Map a full JSON config - all operation names and arguments.
    PXR_NS::JsArray mapConfig(const PXR_NS::JsArray& config) const;

    /// Register a callback to be invoked at process shutdown, before C++
    /// static destructors run. Use this for cleanup that must occur while
    /// external runtimes (CUDA, etc.) are still alive — releasing GPU
    /// buffers from a static destructor risks the driver having torn down
    /// first, which throws from inside the destructor and terminates the
    /// process.
    ///
    /// The Python binding wires a single \p Py_AtExit to
    /// \ref runShutdownCallbacks(), so when the library is loaded as a
    /// Python module the queue drains automatically during \p Py_Finalize
    /// (before any C++ exit-time teardown begins). \p std::atexit is not
    /// a reliable hook for this — it interleaves LIFO with the CUDA
    /// driver's own \p atexit registration, which can cause our callback
    /// to run after CUDA has already torn down.
    ///
    /// \note Non-Python embedders must call \ref runShutdownCallbacks()
    ///       themselves before the process tears down external runtimes
    ///       — typically right before \p main returns or before
    ///       \p cuDeviceReset-style cleanup.
    /// \note Callbacks fire in registration order. Each callback should
    ///       be idempotent. After shutdown begins, additional calls to
    ///       this method are silently dropped.
    void registerShutdownCallback(std::function<void()> callback);

    /// Invoke every callback registered via \ref registerShutdownCallback(),
    /// in registration order, then clear the list. Safe to call multiple
    /// times; the second call is a no-op. Driven automatically by the
    /// Python binding's \p Py_AtExit; non-Python embedders should call
    /// this themselves before exit.
    void runShutdownCallbacks();

    /// Executes the operation with the given name
    ///
    /// \param operationName The name of the operation to execute.
    /// \param context The ExecutionContext to run the operation in.
    /// \param args A string of json containing the args to execute the operation with.
    ///
    /// \return A OperationResult struct which contains the results of the execution. Note: so_operation_result_free
    ///         must be used to clean up this struct.
    OperationResult executeOperation(const std::string& operationName, ExecutionContext* context, const std::string& args);

    /// Executes a JSON configuration containing a sequence of operations.
    ///
    /// The config is a JSON string representing an array of objects, where each object has
    /// an "operation" key and optional argument keys. An "executionContext" operation can be
    /// used to override context settings for subsequent operations.
    ///
    /// Operations are executed in order. Execution stops on the first failure.
    ///
    /// \param context The ExecutionContext to run the operations in.
    /// \param config A JSON string containing an array of operation configurations.
    ///
    /// \return A vector of OperationResult structs, one per executed operation (excluding
    ///         executionContext entries). The caller must call so_operation_result_free on
    ///         each result.
    std::vector<OperationResult> executeConfig(ExecutionContext* context, const std::string& config);

    // Disable copy/assign
    SceneOptimizerCore(const SceneOptimizerCore&) = delete;
    void operator=(const SceneOptimizerCore&) = delete;

    // Unit tests will have access to the carb framework but because of weak linkage of
    // the logging functions/interface, actually accessing those directly will fail from within core
    // when run as a unit test. Consequently, you can explicitly set the logging
    // interface from somewhere where you have access to it, such as unittest main.cpp.
    carb::logging::ILogging* getLoggingInterface() const;
    void setLoggingInterface(carb::logging::ILogging* ilogging);

private:
    class Impl;

    Impl* pImpl;

    SceneOptimizerCore();
    virtual ~SceneOptimizerCore();
};


} // namespace omni::scene::optimizer
