// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/OmniOperation.h>

// OmniMeshOps
#include <OmniMeshOps/Subdivide.h>


namespace omni::scene::optimizer
{

/// Subdivide meshes using OmniMesh.
class SubdivideOperation : public OmniOperation
{
public:
    enum class Method
    {
        eCatmullClark = 0, // Use the Catmull-Clark method
        eLoop = 1, // Use the Loop method
    };

    SubdivideOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    /// Process
    ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context&) override;

    /// Pre
    OperationResult executePre() override;

private:
    uint32_t m_gpu_face_count_threshold;
    uint32_t m_face_count_limit;
    Method m_method;
    uint32_t m_iteration_count;
    omo::SubdivisionMethod m_subdivisionMethod = omo::SubdivisionMethod::CatmullClark;
};

} // namespace omni::scene::optimizer
