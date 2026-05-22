// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "TestUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>

// carb
#include <carb/logging/ILogging.h>

#define DOCTEST_CONFIG_IMPLEMENT
#include <doctest/doctest.h>


namespace
{

const std::vector<const char*> kPlugins = { "omni.scene.optimizer.core.plugin" };
const std::vector<const char*> kPluginSearchPaths = { "exts/omni.scene.optimizer.core/bin" };

}
using namespace omni::scene::optimizer;

int main(int argc, char** argv)
{
    testutils::FrameworkScoped framework;
    framework.loadPlugins(kPlugins, kPluginSearchPaths.data(), kPluginSearchPaths.size());

    doctest::Context context(argc, argv);

    // Initialize the Optimizer with all plugins.
    SceneOptimizerCore::getInstance().loadPlugins();

    // Make sure we can get logging inside core.
    SceneOptimizerCore::getInstance().setLoggingInterface(carb::logging::getLogging());

    return context.run();
}
