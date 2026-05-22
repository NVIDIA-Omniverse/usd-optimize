// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "SceneOptimizerInterface.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Argument.h>
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Operation.h>

// USD
#include <pxr/base/js/utils.h>

// C++
#include <iomanip>
#include <iostream>

using namespace omni::scene::optimizer;


static constexpr int MAX_WIDTH = 100;

static void wrapString(std::ostringstream& oss, const std::string& str, size_t max, size_t indent)
{
    std::istringstream iss(str);
    std::string word;
    size_t current = 0;
    while (iss >> word)
    {
        if (current + word.length() > max)
        {
            current = 0;
            oss << "\n" << std::setw((int)indent) << "";
        }

        oss << word << " ";
        current += word.length();
    }
    oss << "\n";
}


std::string getArrayType(const Argument* argument)
{
    std::string result = "";

    switch (argument->getArrayType())
    {
    case ArgumentArrayType::eStringArray:
        result = "<str>";
        break;
    case ArgumentArrayType::eDoubleArray:
        result = "<double>";
        break;
    case ArgumentArrayType::eFloatArray:
        result = "<float>";
        break;
    case ArgumentArrayType::eIntArray:
        result = "<int>";
        break;
    case ArgumentArrayType::eNone:
    default:
        break;
    }

    return result;
}


void printHelpOperation(const Operation* operation)
{

    std::ostringstream oss;

    oss << "Usage: sceneOptimizer [OPTIONS] input-stage\n";
    oss << "\n";

    oss << operation->getDisplayName() << " (" << operation->getName() << ") Help:\n\n";
    wrapString(oss, operation->getDescription(), MAX_WIDTH, 0);
    oss << "\nArgs:\n";

    const auto& args = operation->getArgs();

    if (args.empty())
    {
        oss << "  No arguments available for this operation\n";
    }

    // Initial pass to work out the longest argument+typename
    size_t widthArgName = 0;
    for (const auto argument : args)
    {
        size_t thisArg = argument->getName().length();
        thisArg += 3; // space / equal / space
        thisArg += PXR_NS::JsWriteToString(argument->getDefaultValue()).length();

        std::string arrayType = getArrayType(argument);
        thisArg += arrayType.length();

        widthArgName = std::max(widthArgName, thisArg);
    }

    // Indent is the width, plus the 2 spaces at the start
    size_t indent = widthArgName + 2;

    // Max line length minus the indent
    size_t max = MAX_WIDTH - indent;

    for (const auto argument : args)
    {
        // Skip groups. We really just use them for visual organization in the UI.
        if (argument->getIsGroup())
        {
            continue;
        }

        // Write main arg / default value
        std::string arg = argument->getName() + " = ";

        std::string defaultVal = PXR_NS::JsWriteToString(argument->getDefaultValue());

        // Special case for default values that are empty arrays, to provide some more
        // context around the expected data type
        if (defaultVal == "[]")
        {
            defaultVal = "[" + getArrayType(argument) + "]";
        }

        arg += defaultVal;

        oss << std::left << std::setw((int)indent) << arg;

        // Wrap the description
        wrapString(oss, argument->getDescription(), max, indent);

        // For enum types, list the available options
        std::ostringstream ossEnum;
        const auto& enumValues = argument->getEnumValues();
        for (size_t i = 0; i < enumValues.size(); ++i)
        {
            if (i)
            {
                ossEnum << ", ";
            }
            ossEnum << enumValues[i].first << "=" << enumValues[i].second;
        }

        std::string enumString = ossEnum.str();
        if (!enumString.empty())
        {
            oss << std::left << std::setw((int)indent) << "";
            wrapString(oss, enumString, max, indent);
        }
    }

    SOPluginVersion version = operation->getVersion();
    oss << "\n";
    oss << "v" << version.major << "." << version.minor << "." << version.rev;
    oss << " - ";
    oss << operation->getAuthor();

    std::cout << oss.str() << std::endl;
}


void printHelp()
{

    std::ostringstream oss;
    oss << R"(Usage: sceneOptimizer [OPTIONS] input-stage

Required Args:
  -i [ --input ] arg    The input stage to read

Optional Args:
  -h [ --help ]                       Print this help
  -h [ --help ] operation             Print help specific to an operation

  -a [ --argument ] argument=value    Specify an argument for an operation, along with its value. Any arguments apply
                                      to the most recent operation that was specified. Array values can be specified by
                                      using a comma separated list.
  -an [ --analysis ]                  Run in analysis mode (unsupported operations will be skipped)
  -c [ --config ] arg                 JSON commands file
  -fl [ --flatten ]                   If enabled, flattens the stage before outputting, otherwise only exports the root
                                      layer.
  -j [ --json ] filename              Write any operation configuration to the specified JSON file
  -o [ --operation ] operation        Add an operation. Multiple operations can be specified, and will execute in the
                                      order they are provided. Adding arguments will apply to the most recently
                                      specified operation
  -r [ --report ]                     If enabled, generate a report describing what was done, warnings, etc
  -s [ --stats ]                      Capture stage stats before/after operations
  -rp [ --relativePaths ]             After exporting a stage, check for any asset paths that can be made relative
  -st [ --singleThreaded ]            Disable multi-threading in operations that support this option
  -v [ --verbose ]                    Enables verbose mode (extra stats, more logging, etc)
  -w [ --write ] filename             The output stage to write
)";

    oss << "\n";
    oss << "Available Operations:\n";

    auto& core = SceneOptimizerCore::getInstance();
    const auto& operations = core.getOperations();

    // Output operations in columns
    size_t i = 0;
    const size_t cols = 4;
    std::array<size_t, cols> colLengths{ 0 };

    // First pass to work out each column length
    for (const auto& name : operations)
    {
        colLengths[i] = std::max(colLengths[i], name.length());

        ++i;

        if (i % cols == 0)
        {
            i = 0;
        }
    }

    // Second pass to do the output
    i = 0;
    for (const auto& name : operations)
    {
        if (i == cols - 1)
        {
            oss << name << "\n";
        }
        else
        {
            oss << std::left << std::setw((int)colLengths[i]) << name << "    ";
        }

        ++i;

        if (i % cols == 0)
        {
            i = 0;
        }
    }
    oss << "\n";

    std::cout << oss.str() << std::endl;
}


int main(int argc, char** argv)
{
    // Init scene optimizer.
    auto& core = SceneOptimizerCore::getInstance();

    // If no args, print basic help.
    if (argc == 1)
    {
        core.loadPlugins();
        printHelp();
        exit(1);
    }

    for (int i = 1; i < argc; ++i)
    {
        std::string arg(argv[i]);

        if (arg == "-h" || arg == "--help")
        {
            core.loadPlugins();
            if (i + 1 < argc)
            {
                // If there is an arg following --help, check if it is an operation name.
                // If so, then we can print specific help for it.
                std::string value = argv[i + 1];

                const OperationUPtr operation = core.getOperation(value);
                if (operation != nullptr)
                {
                    printHelpOperation(operation.get());
                    return 1;
                }
            }

            printHelp();
            return 1;
        }
    }

    // Call main entrypoint in core library
    return sceneOptimizerInterface(argc, argv);
}
