// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "Manifold.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMeshOps
#include <OmniMeshOps/Manifold.h>
#include <OmniMeshOps/usd/Mesh.h>


PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::ManifoldOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categoryManifold = "MANIFOLD";

ManifoldOperation::ManifoldOperation()
    : OmniOperation("manifoldMeshes", "Manifold Meshes", "This operation makes meshes into manifold meshes.")
{

    addArgument("paths", "Meshes To Process", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");
}


std::string ManifoldOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion ManifoldOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string ManifoldOperation::getCategory() const
{
    return s_categoryManifold;
}


std::string ManifoldOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


ProcessedData* ManifoldOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{
    using namespace omo::usd;

    ProcessedData* result = nullptr;

    try
    {
        UsdGeomMesh usdMesh(prim);
        HostMesh mesh(usdMesh, { omo::Defect::None });

        size_t srcVertexVount = mesh.vertexCount();

        mesh = manifold(mesh);

        // The GPU manifold code here was removed due to being slower than running it on CPU.
        // The host<->device mesh copying took longer than the manifold operation itself.
        result = new ProcessedHostMesh(mesh, prim);

        SO_LOG_VERBOSE("%s: %u -> %u vertices", prim.GetName().GetText(), srcVertexVount, mesh.vertexCount());
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        SO_LOG_ERROR(errorMsg.c_str());
        if (result)
        {
            delete result;
            result = nullptr;
        }
    }

    return result;
}

} // namespace omni::scene::optimizer
