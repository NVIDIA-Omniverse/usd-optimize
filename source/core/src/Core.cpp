// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/Core.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Log.h"
#include "omni/scene.optimizer/core/PythonOperation.h"

#include <omni/scene.optimizer/core/Utils.h>

// Carb
#include <carb/extras/EnvironmentVariable.h>
#include <carb/extras/Library.h>

// USD
#include <pxr/base/js/value.h>
#include <pxr/base/tf/fileUtils.h>
#include <pxr/base/tf/pathUtils.h>

// C++
#include <fstream>
#include <mutex>
#include <sstream>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


using InitFunc = bool (*)();
constexpr const char* kInitFuncName = "sceneOptimizerPluginInit";

// Custom plugin path env var
constexpr const char* kEnvPluginPath = "SCENE_OPTIMIZER_PLUGIN_PATH";

// Internal debug logging based on environment variable
constexpr const char* kEnvVarDebug = "SO_DEBUG_CORE";

// Operation/Argument remapping filename
constexpr const char* kMappingFilename = "operation_mapping.json";

#define SO_DEBUG_CORE(FMT, ...)                                                                                        \
    if (m_debug)                                                                                                       \
    {                                                                                                                  \
        printf("[DEBUG] " FMT "\n", __VA_ARGS__);                                                                      \
    }

// Get the scene optimizer shared library dir from a known symbol within this library
static std::string _getLibraryDir()
{
    return carb::extras::getLibraryDirectory(reinterpret_cast<void*>(_getLibraryDir));
}


class SceneOptimizerCore::Impl
{
public:
    struct OperationData
    {
        SceneOptimizerOperationCreate creator;
        SceneOptimizerOperationDestroy destructor;
    };

    bool m_initialized = false;
    std::map<std::string, OperationData> m_operations;
    std::map<std::string, std::shared_ptr<PythonOperation>> m_pythonOperations;
    bool m_debug = false;

    // Callbacks invoked at process shutdown via the Python binding's
    // Py_AtExit hook (or by an embedder calling runShutdownCallbacks
    // directly). Guarded by m_shutdownCallbacksMutex — callers may
    // register from any thread (e.g. parallel plugin loading). Once
    // m_shutdownStarted is set, registerShutdownCallback drops new
    // callbacks and runShutdownCallbacks is a no-op on subsequent calls.
    std::vector<std::function<void()>> m_shutdownCallbacks;
    std::mutex m_shutdownCallbacksMutex;
    bool m_shutdownStarted = false;

    // Storage for supporting renamed operations/args
    struct OperationMapping
    {
        std::string name;
        std::map<std::string, std::string> attributes;
    };

    std::map<std::string, OperationMapping> m_operationMappings;

    carb::logging::ILogging* m_ilogger = nullptr;

    Impl() = default;

    ~Impl()
    {
        m_operations.clear();
        m_pythonOperations.clear();
    }

    void registerOperation(SceneOptimizerOperationCreate creation, SceneOptimizerOperationDestroy deletion)
    {
        // Create an operation.
        OperationUPtr temporary(creation(), deletion);

        auto findIt = m_operations.find(temporary->getName());
        if (findIt != m_operations.end())
        {
            std::cerr << "[SceneOptimizer] Skip registering " << temporary->getName() << ", already registered"
                      << std::endl;
            return;
        }

        OperationData _operation{ creation, deletion };

        SO_DEBUG_CORE("Registering operation %s", temporary->getName().c_str());

        // Insert, but don't replace if it already exists
        m_operations.insert(std::make_pair(temporary->getName(), _operation));
    }


    void deregisterOperation(const std::string& name)
    {
        auto findIt = m_operations.find(name);
        if (findIt == m_operations.end())
        {
            return;
        }

        SO_DEBUG_CORE("Deregistering %s", name.c_str());

        auto findPyIt = m_pythonOperations.find(name);
        if (findPyIt != m_pythonOperations.end())
        {
            m_pythonOperations.erase(findPyIt);
        }

        // Erase from the list of operations
        m_operations.erase(findIt);

        // TODO: Should we attempt to unload the plugin library also.. ?
    }


    void loadPlugin(const std::string& libraryPath)
    {
        SO_DEBUG_CORE("Trying to load plugin from %s", libraryPath.c_str());

        auto handle = carb::extras::loadLibrary(libraryPath.c_str());
        if (!handle)
        {
            std::cerr << "[WARNING] Failed to load plugin " << libraryPath << ": "
                      << carb::extras::getLastLoadLibraryError() << std::endl;
            return;
        }

        // Check for init symbol
        auto initFunc = carb::extras::getLibrarySymbol<InitFunc>(handle, kInitFuncName);
        if (!initFunc)
        {
            // Maybe not a SO plugin
            SO_DEBUG_CORE("library does not contain expected init function %s", kInitFuncName); // LCOV_EXCL_LINE
            return;
        }

        // Call init func
        initFunc();
    }


    void loadPluginsFromPath(const std::string& path)
    {
        const std::vector<std::string>& files = TfListDir(path);
        std::vector<std::string> pythonDirs;

        for (const auto& filename : files)
        {
            if (TfIsFile(filename))
            {
                // Prepend the "." which the carb macro includes
                std::string ext = "." + TfGetExtension(filename);

                if (ext == CARB_LIBRARY_EXTENSION)
                {
                    loadPlugin(filename);
                }
            }
            // if this is a directory check if it is a python plugin
            else if (TfIsDir(filename, true /*resolveSymlinks*/))
            {
                std::string initPath = filename + "/__init__.py";
                initPath = TfNormPath(initPath);

                if (TfIsFile(initPath))
                {
                    SO_DEBUG_CORE("Appending possible python plugin path: %s", filename.c_str());
                    pythonDirs.push_back(filename);
                }
            }
        }

        // are there python plugins to load?
        if (Py_IsInitialized() && !pythonDirs.empty())
        {
            // add the root directory to the Python path
            PyObject* pySys = PyImport_ImportModule("sys");
            PyObject* pyPath = PyObject_GetAttrString(pySys, "path");
            PyList_Append(pyPath, PyUnicode_FromString(path.c_str()));

            // iterate the python directories and attempt to load them as plugins
            for (const auto& dir : pythonDirs)
            {
                loadPythonPluginFromDirectory(dir);
            }
        }
    }

    void loadPythonPluginFromDirectory(const std::string& path)
    {
        SO_DEBUG_CORE("Trying to load python plugin from %s", path.c_str());
        // resolve the module name
        const std::string moduleName = TfGetBaseName(path);
        ScopedPyObject pyModuleName = PyUnicode_FromString(moduleName.c_str());
        ScopedPyObject pyModule = PyImport_Import(pyModuleName.obj);

        std::string pyErrorMessage;
        if (_pyCheckAndGetExceptionMessage(pyErrorMessage))
        {
            std::cerr << "[WARNING] Potential python plugin at \"" << path << "\" will not be registered. Import "
                      << "failed with message: " << pyErrorMessage << std::endl;
            return;
        }
        if (pyModule.obj == nullptr)
        {
            // LCOV_EXCL_START
            std::cerr << "[WARNING] Potential python plugin at \"" << path << "\" will not be registered. Import "
                      << "failed with unknown error." << std::endl;
            return;
            // LCOV_EXCL_STOP
        }

        static const std::string kPythonInitFuncName = "sceneOptimizerPluginInit";
        ScopedPyObject pyRegisterFunc = PyObject_GetAttrString(pyModule.obj, kPythonInitFuncName.c_str());
        if (_pyCheckAndGetExceptionMessage(pyErrorMessage))
        {
            std::cerr << "[WARNING] Potential python plugin at \"" << path << "\" will not be registered. Failed to "
                      << "acquire " << kPythonInitFuncName << " function: " << pyErrorMessage << std::endl;
            return;
        }
        if (pyRegisterFunc.obj == nullptr)
        {
            // LCOV_EXCL_START
            std::cerr << "[WARNING] Potential python plugin at \"" << path << "\" will not be registered as does not "
                      << "have the required " << kPythonInitFuncName << " function. Plugin will not be registered."
                      << std::endl;
            return;
            // LCOV_EXCL_STOP
        }

        ScopedPyObject pyOperation = PyObject_CallNoArgs(pyRegisterFunc.obj);
        if (_pyCheckAndGetExceptionMessage(pyErrorMessage))
        {
            std::cerr << "[WARNING] Python plugin at \"" << path << "\" will not be registered. Exception when calling "
                      << kPythonInitFuncName << ": " << pyErrorMessage << std::endl;
            return;
        }
        if (pyOperation.obj == nullptr)
        {
            // LCOV_EXCL_START
            std::cerr << "[WARNING] Python plugin at \"" << path << "\" will not be registered. Function "
                      << kPythonInitFuncName << "did not return an Operation instance." << std::endl;
            return;
            // LCOV_EXCL_STOP
        }

        // get the operation name from python
        static const std::string kNameAttribute = "name";
        std::string opName;
        try
        {
            opName = _pyAsString(_pyGetObjectAttribute(pyOperation.obj, kNameAttribute).obj);
        }
        catch (const std::exception& exc)
        {
            std::cerr << "[WARNING] Python plugin at \"" << path << "\" will not be registered. Error getting \""
                      << kNameAttribute << "\" attribute from Operation object: " << exc.what() << std::endl;
            return;
        }

        // check if the operation is already registered before going further
        if (m_operations.find(opName) != m_operations.end())
        {
            // LCOV_EXCL_START
            std::cerr << "[SceneOptimizer] Skip registering " << opName << ", already registered" << std::endl;
            return;
            // LCOV_EXCL_STOP
        }

        // get the operation display name
        static const std::string kDisplayNameAttribute = "display_name";
        std::string opDisplayName;
        try
        {
            opDisplayName = _pyAsString(_pyGetObjectAttribute(pyOperation.obj, kDisplayNameAttribute).obj);
        }
        catch (const std::exception& exc)
        {
            std::cerr << "[WARNING] Python plugin at \"" << path << "\" will not be registered. Error getting \""
                      << kDisplayNameAttribute << "\" attribute from Operation object: " << exc.what() << std::endl;
            return;
        }

        // get the operation description
        static const std::string kDescriptionAttribute = "description";
        std::string opDescription;
        try
        {
            opDescription = _pyAsString(_pyGetObjectAttribute(pyOperation.obj, kDescriptionAttribute).obj);
        }
        catch (const std::exception& exc)
        {
            std::cerr << "[WARNING] Python plugin at \"" << path << "\" will not be registered. Error getting \""
                      << kDescriptionAttribute << "\" attribute from Operation object: " << exc.what() << std::endl;
            return;
        }

        // Construct a new wrapper PythonOperation object around the actual python object
        std::shared_ptr<PythonOperation> operation = nullptr;
        try
        {
            operation = std::make_shared<PythonOperation>(opName, opDisplayName, opDescription, pyOperation.obj);
        }
        catch (const std::exception& exc)
        {
            std::cerr << "[WARNING] Python operation \"" << opName
                      << "\" failed to register with message: " << exc.what() << std::endl;
            return;
        }

        m_pythonOperations.insert(std::make_pair(opName, operation));


        // A python operation maintains "owned" by the python-side. The registration merely
        // has a lambda to get that pointer back. We provide a deleter that is a no-op. That way
        // we can safely construct OperationUPtr's from the provided Python operation and not
        // delete the data from underneath Python's feet.
        registerOperation([op = operation.get()]() { return op; }, [](Operation*) {});
    }


    void loadMappings(const std::string& libraryDir)
    {

        std::string mappingFile = TfNormPath(libraryDir + "/" + kMappingFilename);

        // Attempt to read the file and parse it
        std::ifstream inputStream;
        inputStream.open(mappingFile, std::ifstream::in);

        JsValue document;
        JsParseError error;

        if (inputStream)
        {
            // Parse it into a Document then release the stream.
            document = JsParseStream(inputStream, &error);
            inputStream.close();
        }

        // Check for any useful parsing error that could be reported back
        if (!error.reason.empty())
        {
            // LCOV_EXCL_START
            SO_LOG_WARN("Error parsing JSON: %s at line %d, col %d", error.reason.c_str(), error.line, error.column);
            return;
            // LCOV_EXCL_STOP
        }

        // LCOV_EXCL_START
        // Dev error - we expect this file is shipped and correct
        if (!document.IsObject())
        {
            SO_LOG_WARN("Invalid mapping file - expected Object");
            return;
        }
        // LCOV_EXCL_STOP

        // The root of the mapping file is expected to be an Object with two optional keys.
        const JsObject& root = document.GetJsObject();

        // Check for operation renames
        // { old_operation: new_operation, ... }
        const auto& operationsIt = root.find("operations");
        if (operationsIt != root.end() && operationsIt->second.IsObject())
        {
            for (const auto& operationIt : operationsIt->second.GetJsObject())
            {
                if (operationIt.second.IsString())
                {
                    SO_DEBUG_CORE("Mapping operation %s to %s",
                                  operationIt.first.c_str(),
                                  operationIt.second.GetString().c_str());
                    auto& [name, attributes] = m_operationMappings[operationIt.first];
                    name = operationIt.second.GetString();
                }
            }
        }

        // Check for attribute renames
        // { current_operation: { old_attr: new_attr, ... }, ... }
        const auto& attributesIt = root.find("attributes");
        if (attributesIt != root.end() && attributesIt->second.IsObject())
        {
            for (const auto& [operation, attributeMap] : attributesIt->second.GetJsObject())
            {
                if (attributeMap.IsObject())
                {
                    const JsObject& attributes = attributeMap.GetJsObject();
                    for (const auto& [oldAttr, newAttrJs] : attributes)
                    {
                        if (newAttrJs.IsString())
                        {
                            SO_DEBUG_CORE("Mapping argument %s.%s to %s",
                                          operation.c_str(),
                                          oldAttr.c_str(),
                                          newAttrJs.GetString().c_str());
                            m_operationMappings[operation].attributes[oldAttr] = newAttrJs.GetString();
                        }
                    }
                }
            }
        }
    }


    void loadPlugins()
    {
        // Find the default plugin path.
        std::string libraryDir = _getLibraryDir();
        if (libraryDir.empty())
        {
            std::cerr << "[WARNING] Failed to query library path, default plugin load aborted" << std::endl;
            return;
        }

        m_initialized = true;

        // Make a Linux-y path, but then use TfNormPath to fix it for windows if
        // necessary.
        std::string pluginPath = libraryDir + "/" + "operations";
        pluginPath = TfNormPath(pluginPath);

        SO_DEBUG_CORE("Internal operations path: %s", pluginPath.c_str());

        if (TfIsDir(pluginPath, true))
        {
            loadPluginsFromPath(pluginPath);
        }

        // next process the plugin path environment variable and load plugins from paths contained within that
        char* envPluginPaths = getenv(kEnvPluginPath);

        if (envPluginPaths != nullptr)
        {
#if defined(WIN32) || defined(_WIN32) || defined(__WIN32) && !defined(__CYGWIN__)
            static const std::string delimiter = ";";
#else
            static const std::string delimiter = ":";
#endif
            std::vector<std::string> paths = TfStringTokenize(envPluginPaths, delimiter.c_str());
            for (const std::string& path : paths)
            {
                if (TfIsDir(path))
                {
                    SO_DEBUG_CORE("Loading plugins from environment environment variable path: %s", path.c_str());
                    loadPluginsFromPath(path);
                }
            }
        }

        // Load operation/argument mappings.
        loadMappings(libraryDir);
    }


    OperationUPtr getOperation(const std::string& name)
    {
        auto findIt = m_operations.find(name);

        // If not found, check if it has been renamed (e.g. when loading an
        // older JSON config)
        if (findIt == m_operations.end())
        {
            auto mappedIt = m_operationMappings.find(name);

            // Found a mapping. Update the iterator and fall through to
            // return the renamed op.
            if (mappedIt != m_operationMappings.end())
            {
                findIt = m_operations.find(mappedIt->second.name);
            }

            // Not an operation!
            if (findIt == m_operations.end())
            {
                // No such operation
                return OperationUPtr(nullptr, [](Operation*) {});
            }
        }

        // Create a new OperationInstance and give ownership to the unique_ptr.
        return OperationUPtr(findIt->second.creator(), findIt->second.destructor);
    }


    void registerShutdownCallback(std::function<void()> callback)
    {
        if (!callback)
        {
            return;
        }
        std::lock_guard<std::mutex> lock(m_shutdownCallbacksMutex);
        if (m_shutdownStarted)
        {
            return;
        }
        m_shutdownCallbacks.push_back(std::move(callback));
    }

    void runShutdownCallbacks()
    {
        std::lock_guard<std::mutex> lock(m_shutdownCallbacksMutex);
        if (m_shutdownStarted)
        {
            return;
        }
        m_shutdownStarted = true;
        for (auto& cb : m_shutdownCallbacks)
        {
            // Swallow exceptions: a single misbehaving callback must not
            // skip the others, since the rest are typically what prevents
            // the process from aborting on exit.
            try
            {
                cb();
            }
            catch (const std::exception& e)
            {
                SO_LOG_ERROR("Shutdown callback threw: %s", e.what());
            }
            catch (...)
            {
                SO_LOG_ERROR("Shutdown callback threw an unknown exception");
            }
        }
        m_shutdownCallbacks.clear();
    }

    JsObject mapOperation(const std::string& name, const JsObject& config)
    {
        // Quick check: if there is no mapping, the original arguments are up to date.
        auto operationMappingIt = m_operationMappings.find(name);
        if (operationMappingIt == m_operationMappings.end())
        {
            return config;
        }

        JsObject mappedConfig;

        for (const auto& it : config)
        {
            std::string key = it.first;
            JsValue val = it.second;

            // The operation key is special - first, this function should have
            // been called with the already mapped current name. Second, it
            // isn't an attribute we remap (since we rename the operation itself)
            // so there's no need to look it up anyway.
            if (it.first == "operation")
            {
                val = JsValue(name);
            }
            else
            {
                // Otherwise, check if the arg was renamed
                auto& attributes = operationMappingIt->second.attributes;
                auto findAttrIt = attributes.find(key);
                if (findAttrIt != attributes.end())
                {
                    key = findAttrIt->second;
                }
            }

            // Add to the remapped config
            mappedConfig[key] = val;
        }

        return mappedConfig;
    }


    JsArray mapConfig(const JsArray& config)
    {
        JsArray mapped;

        for (const auto& it : config)
        {
            if (!it.IsObject())
            {
                continue;
            }

            const JsObject& opConfig = it.GetJsObject();

            // For a full config, each object will have the operation name.
            // We can use getOperation below to ensure we have the up-to-date
            // name, then use mapOperation for the rest.
            auto findNameIt = opConfig.find("operation");
            if (findNameIt == opConfig.end())
            {
                SO_LOG_WARN("Object is missing 'operation' key, ignoring");
                continue;
            }

            const auto& op = getOperation(findNameIt->second.GetString());

            // getOperation handles mapping, but just in case it's totally bogus...
            if (!op)
            {
                SO_LOG_WARN("Unknown operation: %s", findNameIt->second.GetString().c_str());
                continue;
            }

            mapped.emplace_back(mapOperation(op->getName(), opConfig));
        }

        return mapped;
    }

    OperationResult executeOperation(const std::string& operationName, ExecutionContext* context, const std::string& args)
    {
        JsValue _args = JsParseString(args);

        // Empty string, no args, invalid object, etc.
        if (!_args.IsObject())
        {
            // Not an object, create an empty object as a placeholder.
            _args = JsObject();
        }

        OperationUPtr operation = getOperation(operationName);
        if (operation == nullptr)
        {
            return { false, getCStr("Operation not found"), nullptr };
        }

        // Execute
        return operation->execute(context, _args.GetJsObject());
    }


    std::vector<OperationResult> executeConfig(ExecutionContext* context, const std::string& config)
    {
        std::vector<OperationResult> results;

        JsValue document = JsParseString(config);
        if (!document.IsArray())
        {
            results.push_back({ false, getCStr("JSON config is not an array"), nullptr });
            return results;
        }

        ExecutionContext _context;
        so_execution_context_copy(&_context, context);

        const JsArray& commands = document.GetJsArray();

        for (const JsValue& command : commands)
        {
            if (!command.IsObject())
            {
                results.push_back({ false, getCStr("Config entry is not an object"), nullptr });
                break;
            }

            const JsObject& _command = command.GetJsObject();

            auto operationItr = _command.find("operation");
            if (operationItr == _command.end() || !operationItr->second.IsString())
            {
                results.push_back({ false, getCStr("Invalid or missing 'operation' key"), nullptr });
                break;
            }

            const std::string& operationName = operationItr->second.GetString();

            if (operationName == "executionContext")
            {
                auto getInt = [&](const std::string& key, int defaultValue)
                {
                    auto itr = _command.find(key);
                    return (itr != _command.end() && itr->second.IsInt()) ? itr->second.GetInt() : defaultValue;
                };

                _context.generateReport = getInt("generateReport", _context.generateReport);
                _context.verbose = getInt("verbose", _context.verbose);
                _context.singleThreaded = getInt("singleThreaded", _context.singleThreaded);
                _context.debug = getInt("debug", _context.debug);
                continue;
            }

            OperationUPtr operation = getOperation(operationName);
            if (operation == nullptr)
            {
                std::string msg = "Operation not found: " + operationName;
                results.push_back({ false, getCStr(msg), nullptr });
                break;
            }

            const JsObject mappedCommand = mapOperation(operation->getName(), _command);

            std::ostringstream oss;
            oss << "Executed " << operationName;
            if (_context.analysisMode)
            {
                oss << " analysis";
            }

            OperationResult result;
            {
                ScopedTimer _timer(oss.str());
                result = operation->execute(&_context, mappedCommand);
            }

            results.push_back(result);

            if (!result.success)
            {
                break;
            }
        }

        if (context != nullptr)
        {
            if (context->reportPath != nullptr)
            {
                free(context->reportPath);
            }
            context->reportPath = _context.reportPath;
            _context.reportPath = nullptr;
        }

        so_execution_context_free(&_context);

        return results;
    }
};


JsObject SceneOptimizerCore::mapOperation(const std::string& name, const JsObject& config) const
{
    return pImpl->mapOperation(name, config);
}


JsArray SceneOptimizerCore::mapConfig(const JsArray& config) const
{
    return pImpl->mapConfig(config);
}


SceneOptimizerCore::SceneOptimizerCore()
    : pImpl(new Impl())
{

    // One-time environment check on startup
    // LCOV_EXCL_START
    std::string envDebug;
    if (carb::extras::EnvironmentVariable::getValue(kEnvVarDebug, envDebug))
    {
        pImpl->m_debug = true;
    }
    // LCOV_EXCL_STOP
}


SceneOptimizerCore::~SceneOptimizerCore()
{
    delete pImpl;
}


SceneOptimizerCore& SceneOptimizerCore::getInstance()
{
    static SceneOptimizerCore instance;
    return instance;
}


bool SceneOptimizerCore::isInitialized() const
{
    return pImpl->m_initialized;
}


std::vector<std::string> SceneOptimizerCore::getOperations() const
{
    std::vector<std::string> operationNames;
    operationNames.reserve(pImpl->m_operations.size());
    for (const auto& it : pImpl->m_operations)
    {
        operationNames.push_back(it.first);
    }
    return operationNames;
}

OperationUPtr SceneOptimizerCore::getOperation(const std::string& name) const
{
    return pImpl->getOperation(name);
}


void SceneOptimizerCore::registerOperation(SceneOptimizerOperationCreate creationFn,
                                           SceneOptimizerOperationDestroy destructorFn)
{
    pImpl->registerOperation(creationFn, destructorFn);
}


void SceneOptimizerCore::deregisterOperation(const std::string& name)
{
    pImpl->deregisterOperation(name);
}


void SceneOptimizerCore::loadPlugin(const std::string& libraryPath)
{
    pImpl->loadPlugin(libraryPath);
}


void SceneOptimizerCore::loadPluginsFromPath(const std::string& path)
{
    pImpl->loadPluginsFromPath(path);
}


void SceneOptimizerCore::loadPlugins()
{
    pImpl->loadPlugins();
}


void SceneOptimizerCore::registerShutdownCallback(std::function<void()> callback)
{
    pImpl->registerShutdownCallback(std::move(callback));
}


void SceneOptimizerCore::runShutdownCallbacks()
{
    pImpl->runShutdownCallbacks();
}


OperationResult SceneOptimizerCore::executeOperation(const std::string& operationName,
                                                     ExecutionContext* context,
                                                     const std::string& args)
{
    return pImpl->executeOperation(operationName, context, args);
}


std::vector<OperationResult> SceneOptimizerCore::executeConfig(ExecutionContext* context, const std::string& config)
{
    return pImpl->executeConfig(context, config);
}


carb::logging::ILogging* SceneOptimizerCore::getLoggingInterface() const
{
    if (pImpl->m_ilogger == nullptr)
    {
        pImpl->m_ilogger = carb::logging::getLogging();
    }

    return pImpl->m_ilogger;
}


void SceneOptimizerCore::setLoggingInterface(carb::logging::ILogging* ilogging)
{
    pImpl->m_ilogger = ilogging;
}


} // namespace omni::scene::optimizer
