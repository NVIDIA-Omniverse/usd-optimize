// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/Operation.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"
#include "omni/scene.optimizer/core/Log.h"
#include "omni/scene.optimizer/core/Utils.h"

// carb
#include <carb/Framework.h>
#include <carb/extras/Library.h>
#include <carb/logging/ILogging.h>
#include <carb/logging/Log.h>
#include <carb/logging/Logger.h>

// omni
#include <omni/core/Omni.h>

// USD
#include <pxr/usd/usdUtils/stageCache.h>

// C++
#include <chrono>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{

struct OperationLogger : public carb::logging::Logger2
{
    OperationLogger(const std::string& opname, const std::string& category, bool verbose)
        : Logger2()
    {
        m_operationName = opname;
        m_category = category;
        m_verbose = verbose;
    }

    void setReport(Report* report)
    {
        m_report = report;
    }

    void log(int32_t level, const char* message)
    {
        // Create a LogMessage and call ourselves
        carb::logging::LogMessage _message;
        _message.level = level;
        _message.message = message;

        handleMessage(_message);
    }

    void handleMessage(const carb::logging::LogMessage& message) override
    {
        std::string m(message.message);
        bool multiline = m.find('\n') != std::string::npos;

        // Logging to the report only happens when the call originates from
        // Operation::log, and not when from any other place. This avoids "pollution"
        // from other plugins/carbonite/omni being intermingled with the report.
        // Note that if we are logging to the report, nothing is printed to the console.
        if (m_report && m_fromOperation)
        {
            // Map carb level to SO level, for the report
            auto reportLevel = LogLevel::eInfo;

            switch (message.level)
            {
            case carb::logging::kLevelVerbose:
                reportLevel = LogLevel::eDebug;
                break;
            case carb::logging::kLevelInfo:
                reportLevel = LogLevel::eInfo;
                break;
            case carb::logging::kLevelWarn:
                reportLevel = LogLevel::eWarning;
                break;
            case carb::logging::kLevelError:
            case carb::logging::kLevelFatal:
                reportLevel = LogLevel::eError;
                break;
            default:
                break;
            }

            m_report->log(reportLevel, m_category, message.message, multiline);
            return;
        }

        int32_t _level = message.level;

        // Adjust level to index into array
        if (_level < carb::logging::kLevelVerbose)
        {
            // If it's out of bounds, push it to the end to use "unknown"
            _level = carb::logging::kLevelFatal + 1;
        }

        // Clamp to the highest level + 1 (which will become unknown for anything out of bounds)
        _level = std::min(_level, carb::logging::kLevelFatal + 1);

        // In the case there is no carb, therefore messages were not filtered prior to this function,
        // avoid printing verbose logs unless the context verbose flag was set.
        if (_level < carb::logging::kLevelInfo && m_verbose)
        {
            return;
        }

        static const std::array<std::string, 6> s_prefix = { "[DEBUG]", "[INFO]",  "[WARNING]",
                                                             "[ERROR]", "[FATAL]", "[UNKNOWN VERBOSITY]" };

        // Output prefix, offset by +2 to adjust carb level values to zero-based
        std::cout << s_prefix[message.level + 2];

        // Operation name - available if thisLogger exists (i.e. inside an operation)
        std::cout << " " << m_operationName;

        std::cout << ": ";

        // Message
        if (multiline)
        {
            std::cout << std::endl;
        }

        std::cout << message.message << std::endl;
    }

    std::string m_operationName;
    std::string m_category;
    Report* m_report = nullptr;
    bool m_fromOperation = false;
    bool m_verbose = false;
};


// RAII object for execution.
struct ScopedOperationLogger
{
    ScopedOperationLogger(const std::string& operationName, const std::string& category, bool verbose)
        : m_logger(operationName, category, verbose)
    {
        carb::logging::ILogging* ilogging = SceneOptimizerCore::getInstance().getLoggingInterface();
        if (ilogging != nullptr)
        {
            // If reporting is enabled we might overwrite the log level verbosity. In case we do,
            // we save the old verbosity here so we can restore it on finishing the operation.
            m_oldVerbosity = ilogging->getLevelThreshold();

            m_defaultLogger = ilogging->getDefaultLogger();
            ilogging->removeLogger(m_defaultLogger->getLogger());

            ilogging->addLogger(&m_logger);
        }
        s_logger = &m_logger;
    }

    ~ScopedOperationLogger()
    {
        s_logger = nullptr;

        // Remove the custom logger and restore what we had previous.
        carb::logging::ILogging* ilogging = SceneOptimizerCore::getInstance().getLoggingInterface();
        if (ilogging != nullptr)
        {
            ilogging->removeLogger(&m_logger);
            ilogging->addLogger(m_defaultLogger->getLogger());
            ilogging->setLevelThreshold(m_oldVerbosity);
        }
    }

    void enableReporting(Report* report)
    {
        m_logger.setReport(report);

        carb::logging::ILogging* ilogging = SceneOptimizerCore::getInstance().getLoggingInterface();
        if (ilogging != nullptr)
        {
            // Override the log level to ensure that we receive info messages (that we want in a report)
            // even if the real log level is set to something less verbose.
            ilogging->setLevelThreshold(carb::logging::kLevelInfo);
        }
    }

    OperationLogger m_logger;

    carb::logging::StandardLogger2* m_defaultLogger = nullptr;
    int32_t m_oldVerbosity = carb::logging::kLevelInfo;

    // The thread-local-storage pointer to a logger is valid for any thread while there is a ScopedOperationLogger
    // on the stack. Currently, this is only during Operation::execute(). It allows for situations where no
    // carb logging is available to by-pass carbonite framework and access the OperationLogger directly.
    static OperationLogger* s_logger;
};

OperationLogger* ScopedOperationLogger::s_logger = nullptr;


class Operation::Impl
{
public:
    std::string m_name;
    std::string m_displayName;
    std::string m_description;
    ExecutionContext* m_context;
    PXR_NS::UsdStageWeakPtr m_usdStage;
    std::shared_ptr<Report> m_report;
    std::vector<Argument*> m_args;

    Impl(const std::string& name, const std::string& displayName, const std::string& description)
        : m_name(name)
        , m_displayName(displayName)
        , m_description(description)
        , m_context(nullptr)
        , m_report(nullptr)
    {
    }


    Argument* invalidArgument()
    {
        static Argument s_invalid;
        return &s_invalid;
    }


    Argument* checkArgument(const std::string& name)
    {
        // Ensure arguments are unique, by name
        auto findArg = std::find_if(m_args.begin(),
                                    m_args.end(),
                                    [&name](const Argument* argument) { return argument->getName() == name; });

        if (findArg != m_args.end())
        {
            // Return invalid argument
            return invalidArgument();
        }

        return nullptr;
    }


    const Argument* getArgument(const std::string& name) const
    {
        for (const auto arg : m_args)
        {
            if (arg->getName() == name)
            {
                return arg;
            }
        }

        // Return invalid arg
        return nullptr;
    }


    Argument& addArgument(const std::string& name,
                          const std::string& displayName,
                          const std::string& displayType,
                          const std::string& description,
                          const PXR_NS::JsValue& defaultValue,
                          void* target)
    {
        auto arg = new Argument(name, displayName, displayType, description, defaultValue, target);
        return *m_args.emplace_back(arg);
    }

    Argument& addArgument(Argument* arg)
    {
        m_args.push_back(arg);
        return *m_args.back();
    }
};


Operation::Operation(const std::string& name, const std::string& displayName, const std::string& description)
    : pImpl(new Impl(name, displayName, description))
{
}


Operation::~Operation()
{
    // Clean up arguments
    for (auto argument : pImpl->m_args)
    {
        delete argument;
    }

    delete pImpl;
}


template <typename T>
static void _extractArg(const T& value, void* target)
{
    *(reinterpret_cast<T*>(target)) = value;
}


template <typename T>
static void _clampArg(const Argument* argument, T& value)
{
    if (argument->hasMin())
    {
        value = std::max(value, static_cast<T>(argument->getMin()));
    }

    if (argument->hasMax())
    {
        value = std::min(value, static_cast<T>(argument->getMax()));
    }
}


static bool setArg(const JsValue& value, const Argument* argument)
{

    void* target = argument->getTarget();

    if (value.IsString())
    {
        _extractArg(value.GetString(), target);
    }
    else if (value.IsBool())
    {
        _extractArg(value.GetBool(), target);
    }
    else if (value.IsInt())
    {
        _extractArg(value.GetInt(), target);
        _clampArg(argument, *(reinterpret_cast<int*>(target)));
    }
    else if (value.IsReal())
    {
        // reinterpret_cast and setting a float from a double is no good
        if (argument->getIsFloat())
        {
            _extractArg(static_cast<float>(value.GetReal()), target);
            _clampArg(argument, *(reinterpret_cast<float*>(target)));
        }
        else
        {
            _extractArg(value.GetReal(), target);
            _clampArg(argument, *(reinterpret_cast<double*>(target)));
        }
    }
    else if (value.IsArray())
    {
        switch (argument->getArrayType())
        {
        case ArgumentArrayType::eIntArray:
            _extractArg(value.GetArrayOf<int>(), target);
            break;
        case ArgumentArrayType::eDoubleArray:
            _extractArg(value.GetArrayOf<double>(), target);
            break;
        case ArgumentArrayType::eFloatArray:
        {
            // Explicit conversion from double to float, to ensure we set the correct
            // data on the target
            std::vector<double> _doubles = value.GetArrayOf<double>();
            std::vector<float> _floats(_doubles.begin(), _doubles.end());
            _extractArg(_floats, target);
            break;
        }
        case ArgumentArrayType::eStringArray:
            _extractArg(value.GetArrayOf<std::string>(), target);
            break;
        default:
            return false;
        }
    }
    else
    {
        return false;
    }

    return true;
}


void Operation::resetArgs()
{
    // As the operation may be executed multiple times, and we support sparsely populating the args,
    // take care to reset them to default after execution. Note that we reset ALL args as we can't
    // be sure whether any changed during execution, whether userData modified any, etc.
    for (const auto argument : getArgs())
    {
        // Groups shouldn't be reset - they have no value or target.
        if (!argument->getIsGroup())
        {
            setArg(argument->getDefaultValue(), argument);
        }
    }
}


bool Operation::populateExecutionArguments(const JsObject& args)
{

    for (const auto& it : args)
    {
        // Look up the argument description. This validates it is a supported argument,
        // and we also need the address of the member variable to populate.
        const Argument* argDescription = getArgument(it.first);
        if (argDescription == nullptr)
        {
            // Skip unknown args.
            // This could be the operation name or "notes" or random stuff. For now this is not a warning,
            // just skip.
            continue;
        }

        const JsValue& value = it.second;

        const auto& defaultValue = argDescription->getDefaultValue();
        if (value.GetType() != defaultValue.GetType())
        {
            CARB_LOG_WARN("Argument %s value type does not match expected: %s != %s",
                          it.first.c_str(),
                          value.GetTypeName().c_str(),
                          defaultValue.GetTypeName().c_str());

            return false;
        }

        if (!setArg(value, argDescription))
        {
            // LCOV_EXCL_START
            CARB_LOG_WARN("Unsupported argument type for %s : %s", it.first.c_str(), it.second.GetTypeName().c_str());
            return false;
            // LCOV_EXCL_STOP
        }
    }

    return true;
}


OperationResult Operation::execute(ExecutionContext* context, const JsObject& args)
{
    // create a scoped logger as long as there's not one operating already - if there is this means this is a nested
    // operation call and we want to keep using the parent level logger
    std::unique_ptr<ScopedOperationLogger> scopedLoggerPtr(nullptr);
    if (ScopedOperationLogger::s_logger == nullptr)
    {
        bool verbose = context ? context->verbose : false;
        scopedLoggerPtr = std::make_unique<ScopedOperationLogger>(pImpl->m_name, getCategory(), verbose);
    }

    // Set context then validate
    pImpl->m_context = context;
    if (!pImpl->m_context)
    {
        SO_LOG_ERROR("invalid context");
        return { false };
    }

    // If the context is in analysis mode, ensure the operation supports it.
    if (pImpl->m_context->analysisMode && !getSupportsAnalysis())
    {
        SO_LOG_ERROR("Operation %s does not support analysis mode", pImpl->m_name.c_str());
        return { false };
    }

    // Find and cache the stage for convenient access
    // Note that it is not a requirement to provide a stage ID. Some operations may not need one, or may want to
    // find or create their own.
    auto stageId = pImpl->m_context->usdStageId;
    if (stageId)
    {
        pImpl->m_usdStage = (UsdStageWeakPtr)UsdUtilsStageCache::Get().Find(UsdStageCache::Id::FromLongInt(stageId));

        // Verify there is now a stage. A common cause of a missing stage when one was inserted by Python is two
        // copies of libusd loaded in the process (e.g. the pxr Python bindings resolve to one libusd and our C++
        // core links against another). UsdUtilsStageCache::Get() is a function-local static, so each libusd has
        // its own; an Insert on one is invisible to the Find on the other. Surface that hypothesis in the error
        // so callers don't have to chase an "unknown error".
        if (!pImpl->m_usdStage)
        {
            return { false,
                     getCStr("Stage id " + std::to_string(stageId) +
                             " not found in UsdUtilsStageCache. The Python pxr bindings and the C++ core may be "
                             "linked against different libusd builds — check that the pxr you import resolves to "
                             "the same libusd_*.so the Scene Optimizer core was built against."),
                     nullptr };
        }
    }
    else
    {
        // No stage ID, ensure the stage is empty.
        pImpl->m_usdStage = nullptr;
    }

    // If reporting is enabled, make sure a report object is configured.
    if (pImpl->m_context->generateReport && !pImpl->m_report)
    {

        // Figure out the path. We may have already processed operations that recorded the report path, or been
        // given an explicit report path. Either way, reuse that if found. Otherwise create a temporary filename.
        std::string reportPath;

        // If a path has already been assigned, reopen the same file
        if (pImpl->m_context->reportPath != nullptr)
        {
            reportPath = pImpl->m_context->reportPath;
        }
        else
        {
            reportPath = _getTempFile("sceneOptimizer", "");
        }

        // Create Report object
        pImpl->m_report = std::make_shared<Report>(reportPath);

        // Verify the report can open, otherwise reset to null so we don't try and log to an invalid
        // file handle.
        if (!pImpl->m_report->initialize())
        {
            pImpl->m_report = nullptr;
        }
        else
        {
            // Store the temp file path on the context.
            // This will allow it to be reused and appended to for subsequent operations.
            // Free any existing path first — context->reportPath is C-malloc'd memory.
            if (pImpl->m_context->reportPath != nullptr)
            {
                free(pImpl->m_context->reportPath);
                pImpl->m_context->reportPath = nullptr;
            }
            pImpl->m_context->reportPath = (char*)malloc(sizeof(char) * (reportPath.length() + 1));
            reportPath.copy(pImpl->m_context->reportPath, reportPath.length());
            pImpl->m_context->reportPath[reportPath.length()] = '\0';
        }

        if (scopedLoggerPtr)
        {
            scopedLoggerPtr->enableReporting(pImpl->m_report.get());
        }
    }

    // Wrap the operation around a BEGIN/END marker for filtering the output later.
    if (pImpl->m_report)
    {
        SO_LOG_INFO("BEGIN %s", pImpl->m_displayName.c_str());
    }

    OperationResult result{ true, nullptr, nullptr };

    // Ensure config is up to date
    JsObject mappedArgs = SceneOptimizerCore::getInstance().mapOperation(getName(), args);

    // Set execution arguments.
    // It is an error if anything goes wrong here.
    result.success = populateExecutionArguments(mappedArgs);
    if (!result.success)
    {
        // Warn, but don't abort: let the rest of the cleanup code later happen.
        // The execution will *not* happen, but we do want to reset arguments etc.
        SO_LOG_ERROR("Failed to set arguments, cannot execute operation.");
    }

    // Also record how long the operation takes
    auto timeStart = std::chrono::system_clock::now();

    // Call the internal execute function, as long as there was no error to this point.
    if (result.success)
    {
        if (pImpl->m_context->analysisMode)
        {
            result = executeAnalysisImpl();
        }
        else
        {
            result = executeImpl();
        }
    }

    auto timeEnd = std::chrono::system_clock::now();

    if (pImpl->m_report)
    {
        double duration =
            double(std::chrono::duration_cast<std::chrono::milliseconds>(timeEnd - timeStart).count()) / 1000.0;
        SO_LOG_INFO("END %s (%ss)", pImpl->m_displayName.c_str(), std::to_string(duration).c_str());

        // Nil out the report. This will destroy it and flush it to disk.
        pImpl->m_report = nullptr;
    }

    // Reset the operation to default values.
    // Even if we failed to set SOME of the args and didn't execute, reset everything to default.
    resetArgs();

    return result;
}


std::string Operation::getName() const
{
    return pImpl->m_name;
}


std::string Operation::getDisplayName() const
{
    return pImpl->m_displayName;
}


std::string Operation::getDisplayGroup() const
{
    // Default grouping for any operation that doesn't override this function
    return s_displayGroupUtilities;
}


std::string Operation::getDescription() const
{
    return pImpl->m_description;
}


bool Operation::getVisible() const
{
    return true;
}


bool Operation::getSupportsAnalysis() const
{
    return false;
}


ExecutionContext* Operation::getContext() const
{
    return pImpl->m_context;
}


PXR_NS::UsdStageWeakPtr Operation::getUsdStage() const
{
    return pImpl->m_usdStage;
}


std::shared_ptr<Report> Operation::getReport() const
{
    return pImpl->m_report;
}


const Argument* Operation::getArgument(const std::string& name) const
{
    return pImpl->getArgument(name);
}


Argument* Operation::invalidArgument()
{
    return pImpl->invalidArgument();
}


std::vector<const Argument*> Operation::getArgs() const
{
    // Convert to const
    // They are stored non-const internally in order to allow manipulating them during construction.
    // This kinda sucks but the idea is for them to be read only after creation.
    std::vector<const Argument*> result;
    result.insert(result.end(), pImpl->m_args.begin(), pImpl->m_args.end());
    return result;
}


void Operation::setUserData(void* userData)
{
    // for plugins to do with as they wish...
}


Argument* Operation::checkArgument(const std::string& name)
{
    return pImpl->checkArgument(name);
}


Argument& Operation::addArgument(const std::string& name,
                                 const std::string& displayName,
                                 const std::string& displayType,
                                 const std::string& description,
                                 const PXR_NS::JsValue& defaultValue,
                                 void* target)
{
    return pImpl->addArgument(name, displayName, displayType, description, defaultValue, target);
}


Argument& Operation::addArgument(Argument* arg)
{
    return pImpl->addArgument(arg);
}


void Operation::positionArgument(const Argument* argument, const Argument* after)
{

    auto findArgIt = std::find(pImpl->m_args.begin(), pImpl->m_args.end(), argument);
    auto findAfterIt = std::find(pImpl->m_args.begin(), pImpl->m_args.end(), after);

    if (findArgIt != pImpl->m_args.end() && findAfterIt != pImpl->m_args.end())
    {
        if (findArgIt < findAfterIt)
        {
            std::rotate(findArgIt, findArgIt + 1, findAfterIt + 1);
        }
        else
        {
            std::rotate(findAfterIt, findArgIt, findArgIt + 1);
        }
    }
}


#if CARB_PLATFORM_LINUX || CARB_PLATFORM_MACOS
static int _vscprintf(const char* format, va_list pargs)
{
    return vsnprintf(nullptr, 0, format, pargs);
}
#endif


void soLog(int32_t level, const char* fmt, ...)
{
    va_list args;
    va_list argsCopy;
    va_start(args, fmt);
    va_copy(argsCopy, args);

    // Get string length from argsCopy
    int numChars = _vscprintf(fmt, argsCopy);
    va_end(argsCopy);

    // Verify
    if (numChars < 0)
    {
        CARB_LOG_ERROR("soLog got invalid string");
        return;
    }

    // Copy args in to buffer of string length + terminator
    auto message = new char[numChars + 1];
    vsnprintf(message, numChars + 1, fmt, args);
    va_end(args);

    struct SignalFromOperationLog
    {
        SignalFromOperationLog()
        {
            if (ScopedOperationLogger::s_logger != nullptr)
            {
                ScopedOperationLogger::s_logger->m_fromOperation = true;
                reporting = ScopedOperationLogger::s_logger->m_report;
            }
        }
        ~SignalFromOperationLog()
        {
            if (ScopedOperationLogger::s_logger != nullptr)
            {
                ScopedOperationLogger::s_logger->m_fromOperation = false;
            }
        }

        bool reporting = false;
    };

    // RAII object to signal to the logger that we are originating from here.
    SignalFromOperationLog scopedSignal;
    if (SceneOptimizerCore::getInstance().getLoggingInterface() != nullptr && !scopedSignal.reporting)
    {
        switch (level)
        {
        case carb::logging::kLevelVerbose:
            CARB_LOG_VERBOSE(message);
            break;
        case carb::logging::kLevelInfo:
            CARB_LOG_INFO(message);
            break;
        case carb::logging::kLevelWarn:
            CARB_LOG_WARN(message);
            break;
        case carb::logging::kLevelError:
            CARB_LOG_ERROR(message);
            break;
        case carb::logging::kLevelFatal:
            CARB_LOG_FATAL(message);
            break;
        default:
            std::cout << "[UNKNOWN VERBOSITY] " << message << std::endl;
            break;
        }
    }
    else if (ScopedOperationLogger::s_logger)
    {
        // Either no logging interface available, or reporting is enabled. Bypass the carb framework
        // and use the tls logger. This means we can either log to a console outside of carbonite, or
        // log ONLY to the report if it is enabled.
        CARB_ASSERT(ScopedOperationLogger::s_logger != nullptr);
        ScopedOperationLogger::s_logger->log(level, message);
    }
    else
    {
        // No interface, no scoped logger - most likely a generic message from e.g. the CLI tool
        // outside an operation being executed.
        std::cout << message << std::endl;
    }

    // Clean up
    delete[] message;
}


void Operation::log(int32_t level, const char* fmt, ...)
{
    va_list args;
    va_list argsCopy;
    va_start(args, fmt);
    va_copy(argsCopy, args);

    // Get string length
    int numChars = _vscprintf(fmt, argsCopy);
    va_end(argsCopy);

    if (numChars < 0)
    {
        CARB_LOG_ERROR("Operation::log got invalid string");
        return;
    }

    // Format into buffer
    auto message = new char[numChars + 1];
    vsnprintf(message, numChars + 1, fmt, args);
    va_end(args);

    // Delegate to soLog with the pre-formatted message
    soLog(level, "%s", message);

    delete[] message;
}


OperationResult Operation::executeAnalysisImpl()
{
    // if this function isn't overridden, we don't support analysis mode
    OperationResult result{ false, getCStr("Operation does not support Analysis Mode"), nullptr };
    return result;
}


} // namespace omni::scene::optimizer
