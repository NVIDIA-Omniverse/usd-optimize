// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "DeletePrimsPlugin.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/RemovePrims.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::DeletePrimsOperation);


namespace omni::scene::optimizer
{

constexpr const char* s_category = "DELETE_PRIMS";

DeletePrimsOperation::DeletePrimsOperation()
    : Operation("deletePrims", "Delete Prims", "Deletes prims from a stage.")
{

    addArgument("primPaths", "Meshes To Process", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_primPaths)
        .setPlaceholder("Add meshes or all will be processed");
}


DeletePrimsOperation::~DeletePrimsOperation() = default;


bool DeletePrimsOperation::getVisible() const
{
    return false;
}


std::string DeletePrimsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


std::string DeletePrimsOperation::getCategory() const
{
    return s_category;
}


SOPluginVersion DeletePrimsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


OperationResult DeletePrimsOperation::executeImpl()
{
    _deletePrims(getUsdStage(), m_primPaths);
    return { true };
}


} // namespace omni::scene::optimizer
