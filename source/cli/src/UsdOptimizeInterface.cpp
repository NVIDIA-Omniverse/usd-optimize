// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/Defs.h>
#include <usd_optimize/core/Operation.h>
#include <usd_optimize/core/ParseJson.h>
#include <usd_optimize/core/Utils.h>

// Carb
#include <carb/extras/Path.h>

// USD
#include <pxr/base/js/utils.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdUtils/stageCache.h>

// C++
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE
using namespace usd_optimize;


// Basic arg struct.
// I'm just trying to avoid boost_program_options really.
struct Args
{
    std::string config;
    std::string input;
    std::string output;
    std::string json;

    bool flatten = false;
    bool relative = false;
    bool stats = false;

    ExecutionContext context;
    std::vector<JsObject> commands;
};


template <typename F>
static JsArray transformValue(const std::string& value, F&& func)
{
    std::vector<std::string> values = TfStringTokenize(value, ",");
    JsArray _values(values.size());
    std::transform(values.begin(), values.end(), _values.begin(), func);
    return _values;
}


// Accept the spelling that `usdOptimize -h` prints (true/false, in any case) as well as 1 and 0.
static bool parseBool(const std::string& arg, const std::string& value)
{
    const std::string lowerValue = TfStringToLower(value);
    if (lowerValue == "true" || lowerValue == "false")
    {
        return lowerValue == "true";
    }

    if (value == "1" || value == "0")
    {
        return value == "1";
    }
    throw std::invalid_argument(arg + " takes true, false, 1 or 0, not \"" + value + "\"");
}


static void setArg(const Argument* argument, JsObject& command, const std::string& arg, const std::string& value)
{
    // Get the default value from the argument definition.
    // We can use this to figure out the correct type.
    const JsValue& defaultValue = argument->getDefaultValue();

    if (defaultValue.IsString())
    {
        command[arg] = JsValue(value);
    }
    else if (defaultValue.IsBool())
    {
        command[arg] = JsValue(parseBool(arg, value));
    }
    else if (defaultValue.IsInt())
    {
        command[arg] = JsValue(std::stoi(value));
    }
    else if (defaultValue.IsReal())
    {
        command[arg] = JsValue(std::stod(value));
    }
    else if (defaultValue.IsArray())
    {
        switch (argument->getArrayType())
        {
        case ArgumentArrayType::eIntArray:
            command[arg] = transformValue(value, [](const std::string& v) { return JsValue(std::stoi(v)); });
            break;
        case ArgumentArrayType::eDoubleArray:
        case ArgumentArrayType::eFloatArray:
            command[arg] = transformValue(value, [](const std::string& v) { return JsValue(std::stod(v)); });
            break;
        case ArgumentArrayType::eStringArray:
            command[arg] = transformValue(value, [](const std::string& v) { return JsValue(v); });
            break;
        default:
            std::cerr << "Error: unsupported argument array type: " << value << std::endl;
            exit(1);
        }
    }
    else
    {
        std::cerr << "Error: unsupported argument value type: " << value << std::endl;
        exit(1);
    }
}


std::string getArg(int argc, char** argv, int& i)
{
    if (i + 1 > argc)
    {
        std::cerr << "Error: missing argument value" << std::endl;
        exit(1);
    }

    return argv[++i];
}


void parseArgs(int argc, char** argv, Args& args)
{

    auto& core = UsdOptimizeCore::getInstance();

    // Caller should handle printing help if no args.
    if (argc == 1)
    {
        exit(1);
    }

    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i]);

        if (arg == "-a" || arg == "--argument")
        {
            // If there are no commands, there's nothing for the arg to apply to.
            if (args.commands.empty())
            {
                std::cerr << "Error: no operations to add argument to" << std::endl;
                exit(1);
            }

            std::string value = getArg(argc, argv, i);
            std::vector<std::string> parts = TfStringTokenize(value, "=");

            // Retrieve the last command
            // Note: this code assumes a *valid* operation was added as this is internal and
            // we should have already verified.
            auto& command = args.commands.back();
            const std::string& operationName = command["operation"].GetString();
            auto operation = core.getOperation(operationName);

            const auto argument = operation->getArgument(parts[0]);
            if (!argument)
            {
                std::cerr << "Error: invalid argument specified: " << parts[0] << std::endl;
                exit(1);
            }

            // As a (small) convenience allow adding a "true" bool argument by just specifying
            // the argument with no value
            if (value.find('=') == std::string::npos && argument->getIsBool())
            {
                // No try/catch here - it is a valid argument, it is a bool, and we're just
                // setting it to true. To set it to false, use arg=false (or arg=0).
                setArg(argument, command, parts[0], "1");
                continue;
            }

            // Otherwise arg values should be of the format name=value
            if (parts.size() != 2)
            {
                std::cerr << "Error: invalid argument value specified: " << value << std::endl;
                exit(1);
            }

            // Track the argument value against the last instance of this command
            try
            {
                setArg(argument, command, parts[0], parts[1]);
            }
            catch (const std::exception& ex)
            {
                std::cerr << "Error: failed to set argument: " << ex.what() << std::endl;
                exit(1);
            }
        }
        else if (arg == "-an" || arg == "--analysis")
        {
            args.context.analysisMode = 1;
        }
        else if (arg == "-c" || arg == "--config")
        {
            args.config = getArg(argc, argv, i);
        }
        else if (arg == "-fl" || arg == "--flatten")
        {
            args.flatten = true;
        }
        else if (arg == "-i" || arg == "--input")
        {
            args.input = getArg(argc, argv, i);
        }
        else if (arg == "-j" || arg == "--json")
        {
            args.json = getArg(argc, argv, i);
        }
        else if (arg == "-o" || arg == "--operation")
        {
            std::string operationName = getArg(argc, argv, i);
            auto operation = core.getOperation(operationName);
            if (!operation)
            {
                std::cerr << "Error: invalid operation specified: " << operationName << std::endl;
                exit(1);
            }

            // If analysis is enabled, warn and skip operations that don't support it
            if (args.context.analysisMode && !operation->getSupportsAnalysis())
            {
                std::cerr << "[UsdOptimize] warning: skipping operation " << operationName
                          << " which does not support analysis" << std::endl;
                continue;
            }

            // Append operation
            JsObject operationObject{};
            operationObject["operation"] = JsValue(operationName);
            args.commands.emplace_back(operationObject);
        }
        else if (arg == "-r" || arg == "--report")
        {
            args.context.generateReport = 1;
        }
        else if (arg == "-rp" || arg == "--relativePaths")
        {
            args.relative = true;
        }
        else if (arg == "-s" || arg == "--stats")
        {
            args.stats = true;
        }
        else if (arg == "-st" || arg == "--singleThreaded")
        {
            args.context.singleThreaded = true;
        }
        else if (arg == "-v" || arg == "--verbose")
        {
            args.context.verbose = true;
        }
        else if (arg == "-w" || arg == "--write")
        {
            args.output = getArg(argc, argv, i);
        }
        // Special case - assume final arg can also be input, unless it is flag-shaped:
        // a mistyped flag would otherwise be misreported as a failed layer open.
        else if (i == argc - 1 && arg.rfind('-', 0) != 0)
        {
            args.input = arg;
        }
        else
        {
            std::cerr << "Error: unknown argument: " << arg << std::endl;
            exit(1);
        }
    }
}


void checkForSaveInPlace(const Args& args, bool& saveInPlace)
{
    // If input/output match then we want to save this stage in place.
    if (args.input == args.output)
    {
        saveInPlace = true;
        return;
    }
}


void makePathsRelative(const std::string& filename)
{
    // Open the stage.
    // Exporting is what causes the assets to be made absolute in the first place. So we need to do this on
    // the output stage after we have done any other operation.
    UsdStageRefPtr stage = UsdStage::Open(filename);

    // Get the base path, to check what everything should be relative to.
    carb::extras::Path stagePath(filename);
    carb::extras::Path stageDir = stagePath.getAbsolute().getParent();

    std::map<std::string, std::string> pathCache;

    // Brute-force traversal to collect all asset references on the stage.
    for (const auto& prim : stage->TraverseAll())
    {
        for (const auto& attribute : prim.GetAttributes())
        {
            // Only care about Asset
            if (attribute.GetTypeName() != SdfValueTypeNames->Asset)
            {
                continue;
            }

            // Check we get a value
            SdfAssetPath assetPath;
            if (!attribute.Get(&assetPath))
            {
                continue;
            }

            const std::string& _assetPath = assetPath.GetAssetPath();
            if (!_assetPath.empty())
            {
                std::string newAssetPath;

                auto findIt = pathCache.find(_assetPath);
                if (findIt == pathCache.end())
                {
                    // Get the relative path from stageDir.
                    carb::extras::Path path(_assetPath);
                    carb::extras::Path relativePath = path.getRelative(stageDir);

                    if (!relativePath.empty())
                    {
                        newAssetPath = "./" + relativePath.getString();
                    }

                    // Cache either way
                    pathCache[_assetPath] = newAssetPath;
                }
                else
                {
                    newAssetPath = findIt->second;
                }

                // If we got a relative path then set it on the attribute
                if (!newAssetPath.empty())
                {
                    attribute.Set(SdfAssetPath(newAssetPath));
                }
            }
        }
    }

    // Save the stage in-place.
    stage->Save();
}


int usdOptimizeInterface(int argc, char** argv)
{
    // Load the default plugins.
    // This must happen once on startup to register the "built in" plugins.
    auto& core = UsdOptimizeCore::getInstance();
    core.loadPlugins();

    // Grab args. If there are no args, this will print help and exit.
    Args args;
    parseArgs(argc, argv, args);

    if (args.input.empty())
    {
        std::cout << "No stage specified" << std::endl;
        return 1;
    }

    // Overall timer
    ScopedTimer _mainTimer("UsdOptimize finished");

    // Work out whether we can save the file in place instead of exporting the result.
    bool saveInPlace = false;

    if (!args.output.empty())
    {
        checkForSaveInPlace(args, saveInPlace);
    }

    // Open stage
    UsdStageRefPtr stage;
    {
        ScopedTimer _timer(std::string("Open stage ") + args.input);
        stage = UsdStage::Open(args.input);
    }

    if (!stage)
    {
        std::cout << "Failed to open stage" << std::endl;
        return 1;
    }

    // Insert the stage so it has a valid stageID in the cache.
    auto stageId = UsdUtilsStageCache::Get().Insert(stage);

    // Populate stage id
    args.context.usdStageId = stageId.ToLongInt();

    // Copy the context and ensure analysis is off for stats.
    // Note: This is just for the -s before/after stats, it doesn't apply if you explicitly
    //       append a printStats operation.
    ExecutionContext statsContext;
    usd_optimize_execution_context_copy(&statsContext, &args.context);
    statsContext.analysisMode = 0;

    // Check whether to run stats
    OperationUPtr statsOp(nullptr, [](Operation*) {});
    if (args.stats)
    {
        statsOp = core.getOperation("printStats");

        if (statsOp != nullptr)
        {
            ScopedTimer _timer("Stats before");
            statsOp->execute(&statsContext, JsObject());
        }
    }

    // Check what to do
    bool execOk = true;
    if (!args.config.empty())
    {
        ScopedTimer _timer("Run commands");
        execOk = _parseJson(stage, args.config, &args.context);
    }
    else if (!args.commands.empty())
    {
        JsArray _commands(args.commands.size());
        std::transform(args.commands.begin(), args.commands.end(), _commands.begin(), [](const JsObject& j) { return j; });

        execOk = _parseJson(stage, _commands, &args.context);

        // Check whether to save JSON as a config
        if (!args.json.empty())
        {
            // This is for debug, no real error handling: a warning will be printed if the stream is
            // bogus.
            std::ofstream out(args.json);
            JsWriteToStream(_commands, out);
            out << "\n";
            out.close();
        }
    }
    else
    {
        std::cout << "Nothing to do :(" << std::endl;
        return 0;
    }

    // An operation failed and _parseJson aborted the run. Surface it as a non-zero exit and skip
    // the post-stats and output write below, but still fall through to the shared finalization
    // (report-path print + context cleanup) rather than reporting a phantom success or returning early.
    int exitCode = 0;
    if (!execOk)
    {
        std::cerr << "Operation execution failed; see the log above. No output written." << std::endl;
        exitCode = 1;
    }

    // Run after stats, unless in analysis mode (as nothing would have been done to the scene)
    if (execOk && statsOp && !args.context.analysisMode)
    {
        ScopedTimer _timer("Stats after");
        statsOp->execute(&statsContext, JsObject());
    }

    // Write output, if set.
    if (execOk && !args.output.empty())
    {
        ScopedTimer _timer(std::string("Wrote output ") + args.output);

        if (saveInPlace)
        {
            stage->Save();
        }
        else
        {
            // Export the root layer (we only author there) to avoid the cost and
            // quirks of flattening; flatten only when explicitly requested.
            const bool writeOk = args.flatten ? stage->Export(args.output) : stage->GetRootLayer()->Export(args.output);

            // Export forces absolute asset paths; rewrite them as relative if asked.
            if (writeOk && args.relative)
            {
                makePathsRelative(args.output);
            }

            if (!writeOk)
            {
                _timer.setLabel(std::string("Failed to write output ") + args.output);
                std::cerr << args.output
                          << " could not be written; see the USD diagnostic above "
                             "for the cause. Note: .usdz cannot be written via the export API -- "
                             "write .usd/.usdc and package it with usdzip."
                          << std::endl;
                exitCode = 1;
            }
        }
    }

    if (args.context.reportPath != nullptr)
    {
        std::cout << "UsdOptimize report: " << args.context.reportPath << std::endl;
    }

    usd_optimize_execution_context_free(&args.context);
    usd_optimize_execution_context_free(&statsContext);

    return exitCode;
}
