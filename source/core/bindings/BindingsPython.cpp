// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Scene Optimizer Core
#include "omni/scene.optimizer/core/UsdIncludes.h"

#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/PybindUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// Usd
#include <pxr/base/js/json.h>
#include <pxr/base/js/value.h>

// Pybind
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

// C++
#include <mutex>


using namespace omni::scene::optimizer;


// Simple helper that can be used in a unique_ptr that will do nothing when its deleted
template <typename T>
struct BlankDeleter
{
    void operator()(T* inst) const
    {
    }
};


// C-callable trampoline for Py_AtExit. Drains the SceneOptimizerCore's
// shutdown-callback queue (registered by ops via registerShutdownCallback)
// during Py_Finalize, *before* C++ static destructors run — so callbacks
// can safely free CUDA buffers etc. while their host runtimes are still
// alive. std::atexit is unsuitable here because it interleaves LIFO with
// the CUDA driver's own atexit, which can leave us running after the
// driver has torn down.
static void _runSceneOptimizerCoreShutdownCallbacks()
{
    SceneOptimizerCore::getInstance().runShutdownCallbacks();
}


// wrapper function for SceneOptimizerCore::getInstance that ensures the singleton is initialized before we try to use
// it in Python.
static SceneOptimizerCore& _getInitializedSceneOptimizerCore()
{
    // Ensure the core is initialized before we return it
    static std::once_flag initFlag;
    std::call_once(initFlag,
                   []()
                   {
                       auto& core = SceneOptimizerCore::getInstance();
                       if (!core.isInitialized())
                       {
                           core.loadPlugins();
                       }
                       // Hook the shutdown-callback queue to Python's exit
                       // path. Operations that need cleanup before driver
                       // teardown (CUDA, etc.) register via
                       // SceneOptimizerCore::registerShutdownCallback; this
                       // single Py_AtExit drives them all.
                       Py_AtExit(_runSceneOptimizerCoreShutdownCallbacks);
                   });
    return SceneOptimizerCore::getInstance();
}


static const char* _SceneOptimizerCore_getOperationDisplayName_docString =
    "Returns the display name of the given operation, or an empty string if the operation doesn't exist.";
static std::string _SceneOptimizerCore_getOperationDisplayName(SceneOptimizerCore& core, const std::string& operationName)
{
    const OperationUPtr operation = core.getOperation(operationName);
    if (operation == nullptr)
    {
        return "";
    }

    return operation->getDisplayName();
}


static const char* _SceneOptimizerCore_getOperationDisplayGroup_docString =
    "Returns the display group of the given operation, or an empty string if the operation doesn't exist.";
static std::string _SceneOptimizerCore_getOperationDisplayGroup(SceneOptimizerCore& core, const std::string& operationName)
{
    const OperationUPtr operation = core.getOperation(operationName);
    if (operation == nullptr)
    {
        return "";
    }

    return operation->getDisplayGroup();
}


static const char* _SceneOptimizerCore_getOperationDescription_docString =
    "Returns the description of the given operation, or an empty string if the operation doesn't exist.";
static std::string _SceneOptimizerCore_getOperationDescription(SceneOptimizerCore& core, const std::string& operationName)
{
    const OperationUPtr operation = core.getOperation(operationName);
    if (operation == nullptr)
    {
        return "";
    }

    return operation->getDescription();
}


static const char* _SceneOptimizerCore_getOperationArguments_docString =
    "Returns the arguments of the given operation as JSON list, or an empty list if the operation doesn't exist.";
static pybind11::object _SceneOptimizerCore_getOperationArguments(SceneOptimizerCore& core,
                                                                  const std::string& operationName)
{
    PXR_NS::JsArray args;

    const OperationUPtr operation = core.getOperation(operationName);
    if (operation != nullptr)
    {
        // Create a JSON array and append the JSON object for each argument
        for (const Argument* argument : operation->getArgs())
        {
            args.push_back(argument->toJson());
        }
    }

    return _jsonValueToPybindObject(args);
}


static const char* _SceneOptimizerCore_getOperationAuthor_docString =
    "Returns the author of the given operation, or an empty string if the operation doesn't exist.";
static std::string _SceneOptimizerCore_getOperationAuthor(SceneOptimizerCore& core, const std::string& operationName)
{
    const OperationUPtr operation = core.getOperation(operationName);
    if (operation == nullptr)
    {
        return "";
    }

    return operation->getAuthor();
}

/// Returns the version of the given operation, or (-1, -1, -1) if the operation doesn't exist.
static const char* _SceneOptimizerCore_getOperationVersion_docString =
    "Returns the version of the given operation, or (-1, -1, -1) if the operation doesn't exist.";
static SOPluginVersion _SceneOptimizerCore_getOperationVersion(SceneOptimizerCore& core, const std::string& operationName)
{
    const OperationUPtr operation = core.getOperation(operationName);
    if (operation == nullptr)
    {
        return SOPluginVersion{ -1, -1, -1 };
    }

    return operation->getVersion();
}


static const char* _SceneOptimizerCore_getOperationVisible_docString =
    "Returns if the given operation is visible, or False if the operation doesn't exist.";
static bool _SceneOptimizerCore_getOperationVisible(SceneOptimizerCore& core, const std::string& operationName)
{
    const OperationUPtr operation = core.getOperation(operationName);
    if (operation == nullptr)
    {
        return false;
    }

    return operation->getVisible();
}


static const char* _SceneOptimizerCore_getOperationSupportsAnalysis_docString =
    "Returns if the given operation supports analysis, or False if the operation doesn't exist.";
static bool _SceneOptimizerCore_getOperationSupportsAnalysis(SceneOptimizerCore& core, const std::string& operationName)
{
    const OperationUPtr operation = core.getOperation(operationName);
    if (operation == nullptr)
    {
        return false;
    }

    return operation->getSupportsAnalysis();
}


// wrapper function for SceneOptimizerCore::ExecuteOperation that handles the ExecutionContext in its Python wrapper
// form, converts the args from a Python dict to a JSON string, and converts the OperationResult to a Python tuple.
static pybind11::tuple _SceneOptimizerCore_executeOperation(SceneOptimizerCore& core,
                                                            const std::string& operationName,
                                                            pybind11::object context,
                                                            pybind11::object args)
{
    // get the python args as a json string
    pybind11::module_ pyJson = pybind11::module_::import("json");
    pybind11::object pyJsonArgs = pyJson.attr("dumps")(args);

    // execute the operation
    OperationResult result =
        core.executeOperation(operationName, _getExecutionContextFromPyWrapper(context), pyJsonArgs.cast<std::string>());

    // convert result to a python tuple
    pybind11::tuple _result = _operationResultToPybindTuple(result);

    // Free the operation result
    so_operation_result_free(&result);

    return _result;
}


// wrapper function for SceneOptimizerCore::executeConfig that accepts a Python list of dicts,
// serializes to a JSON string, executes the config, and returns a list of (success, error, output) tuples.
static pybind11::list _SceneOptimizerCore_executeConfig(SceneOptimizerCore& core,
                                                        pybind11::object context,
                                                        pybind11::object config)
{
    pybind11::module_ pyJson = pybind11::module_::import("json");
    pybind11::object pyJsonConfig = pyJson.attr("dumps")(config);

    std::vector<OperationResult> results =
        core.executeConfig(_getExecutionContextFromPyWrapper(context), pyJsonConfig.cast<std::string>());

    pybind11::list pyResults;
    for (auto& result : results)
    {
        pyResults.append(_operationResultToPybindTuple(result));
        so_operation_result_free(&result);
    }

    return pyResults;
}


static pybind11::object _SceneOptimizerCore_mapConfig(SceneOptimizerCore& core, const std::string& config)
{
    PXR_NS::JsValue document = PXR_NS::JsParseString(config);
    if (document.IsNull())
    {
        throw pybind11::value_error("mapConfig: failed to parse JSON string");
    }
    if (!document.IsArray())
    {
        throw pybind11::type_error("mapConfig: expected a JSON array");
    }

    PXR_NS::JsArray mapped = core.mapConfig(document.GetJsArray());
    std::string result = PXR_NS::JsWriteToString(PXR_NS::JsValue(mapped));
    return pybind11::str(result);
}


PYBIND11_MODULE(_omni_scene_optimizer_impl_core, m)
{
    // Global execution context/options
    pybind11::class_<ExecutionContext>(m,
                                       "_ExecutionContextImpl",
                                       R"(
A struct describing the context in which a Scene Optimization should be performed.

This is accepted by all Scene Optimizer Operation Commands.

:param int usdStageId: The stage on which to perform the operation
:param int generateReport: If true, a report will be generated that can be viewed via the Scene Optimizer UI
:param int verbose: If true, log extended information (may result in slower performance)
:param int singleThreaded: If true, run operation single threaded
:param int captureStats: If true, capture and report on the contents of the stage before and after the operations run
:param str reportPath: File path where the report will be written, if undefined a path will be generated on execute
        )")
        .def(pybind11::init<>())
        .def_readwrite("usdStageId", &ExecutionContext::usdStageId)
        .def_readwrite("generateReport", &ExecutionContext::generateReport)
        .def_readwrite("verbose", &ExecutionContext::verbose)
        .def_readwrite("singleThreaded", &ExecutionContext::singleThreaded)
        .def_readwrite("debug", &ExecutionContext::debug)
        .def_readwrite("captureStats", &ExecutionContext::captureStats)
        .def_readwrite("analysisMode", &ExecutionContext::analysisMode)
        // reportPath is a char* owned by C++ (malloc/free). def_readwrite would let pybind11's
        // char* caster store a pointer into a temporary std::string, which dangles after the
        // setter returns. Use def_property so we copy on assign and own the buffer.
        .def_property(
            "reportPath",
            [](const ExecutionContext& self) -> pybind11::object
            {
                if (self.reportPath == nullptr)
                {
                    return pybind11::none();
                }
                return pybind11::str(self.reportPath);
            },
            [](ExecutionContext& self, pybind11::object value)
            {
                if (value.is_none())
                {
                    if (self.reportPath != nullptr)
                    {
                        free(self.reportPath);
                        self.reportPath = nullptr;
                    }
                    return;
                }
                // Cast first so a bad value throws before we free the existing buffer.
                std::string newPath = value.cast<std::string>();
                if (self.reportPath != nullptr)
                {
                    free(self.reportPath);
                    self.reportPath = nullptr;
                }
                self.reportPath = getCStr(newPath);
            })
        // ExecutionContext is a POD-ish C struct (no destructor — kept that way for Carbonite).
        // Without a finalizer here, reportPath leaks every time Python GCs the wrapper.
        .def("__del__", [](ExecutionContext& self) { so_execution_context_free(&self); });

    pybind11::class_<SOPluginVersion>(m,
                                      "SOPluginVersion",
                                      R"(
Semantic version for plugins

:param int major: The major version number
:param int minor: The minor version number
:param int rev: The revision number
    )")
        .def(pybind11::init<>())
        .def_readwrite("major", &SOPluginVersion::major)
        .def_readwrite("minor", &SOPluginVersion::minor)
        .def_readwrite("rev", &SOPluginVersion::rev);

    // Scene Optimizer Core is not publicly constructible or destructible - so we wrap it in a unique_ptr with a blank
    // deleter to prevent pybind from trying to manage its lifetime
    pybind11::class_<SceneOptimizerCore, std::unique_ptr<SceneOptimizerCore, BlankDeleter<SceneOptimizerCore>>>(
        m,
        "SceneOptimizerCore",
        R"(
Singleton object that manages loading of Scene Optimizer plugins and execution of operations.
        )")
        .def_static("getInstance", &_getInitializedSceneOptimizerCore, pybind11::return_value_policy::reference)
        .def("isInitialized", &SceneOptimizerCore::isInitialized)
        .def("getOperations", &SceneOptimizerCore::getOperations)
        .def("getOperationDisplayName",
             &_SceneOptimizerCore_getOperationDisplayName,
             _SceneOptimizerCore_getOperationDisplayName_docString)
        .def("getOperationDisplayGroup",
             &_SceneOptimizerCore_getOperationDisplayGroup,
             _SceneOptimizerCore_getOperationDisplayGroup_docString)
        .def("getOperationDescription",
             &_SceneOptimizerCore_getOperationDescription,
             _SceneOptimizerCore_getOperationDescription_docString)
        .def("getOperationArguments",
             &_SceneOptimizerCore_getOperationArguments,
             _SceneOptimizerCore_getOperationArguments_docString)
        .def("getOperationAuthor", &_SceneOptimizerCore_getOperationAuthor, _SceneOptimizerCore_getOperationAuthor_docString)
        .def("getOperationVersion",
             &_SceneOptimizerCore_getOperationVersion,
             _SceneOptimizerCore_getOperationVersion_docString)
        .def("getOperationVisible",
             &_SceneOptimizerCore_getOperationVisible,
             _SceneOptimizerCore_getOperationVisible_docString)
        .def("getOperationSupportsAnalysis",
             &_SceneOptimizerCore_getOperationSupportsAnalysis,
             _SceneOptimizerCore_getOperationSupportsAnalysis_docString)
        .def("deregisterOperation", &SceneOptimizerCore::deregisterOperation)
        .def("loadPlugin", &SceneOptimizerCore::loadPlugin)
        .def("loadPluginsFromPath", &SceneOptimizerCore::loadPluginsFromPath)
        .def("loadPlugins", &SceneOptimizerCore::loadPlugins)
        .def("executeOperation", &_SceneOptimizerCore_executeOperation)
        .def("executeConfig",
             &_SceneOptimizerCore_executeConfig,
             R"(Execute a JSON configuration containing a sequence of operations.

The config is a list of dicts, where each dict has an "operation" key identifying the
operation to run, plus any operation-specific argument keys. An entry with
"operation": "executionContext" can be used to override context settings for subsequent
operations.

Operations are executed in order. Execution stops on the first failure.

:param context: The ExecutionContext to run the operations in.
:param config: A list of dicts, each describing an operation and its arguments.
:returns: A list of (success, error, output) tuples, one per executed operation
          (excluding executionContext entries).
)")
        .def("mapConfig",
             &_SceneOptimizerCore_mapConfig,
             R"(Map a JSON configuration to update renamed operations and arguments.

Takes a JSON string containing a Scene Optimizer config (a JSON array of
operation dicts) and returns a JSON string with operation and argument names
updated to their current equivalents.

:param config: A JSON string representing the array of operation dicts.
:returns: A JSON string with the mapped configuration.
)");
}
