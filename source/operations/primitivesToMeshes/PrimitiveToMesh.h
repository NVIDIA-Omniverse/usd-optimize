// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/OmniOperation.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMeshOps
#include <OmniMeshOps/Primitive.h>


namespace omni::scene::optimizer
{

/// Fit primitives to meshes using OmniMesh.
class PrimitiveToMeshOperation : public OmniOperation
{
public:
    PrimitiveToMeshOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    bool meshesOnly() override
    {
        return false;
    }

    PXR_NS::Usd_PrimFlagsPredicate primFlagsPredicate() const override
    {
        // Unlike the default predicate, this allows undefined and abstract prims
        return PXR_NS::UsdPrimIsActive && PXR_NS::UsdPrimIsLoaded;
    }

    ResolveFilter resolveFilter() override;

    /// Process prim, actually
    ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context&) override;

    /// Post
    void executePost(const TotalStats& totalStats) override;

private:
    /// Options to use for mesh generation
    struct PrimOptions
    {
        bool convertSpheres = true;
        omo::SphereMeshParameters sphereParameters;
        bool convertCylinders = true;
        omo::CylinderMeshParameters cylinderParameters;
        bool convertCones = true;
        omo::ConeMeshParameters coneParameters;
        bool convertCubes = true;
        omo::CubeMeshParameters cubeParameters;
    };

    /// Used for analysis mode
    struct PrimReport
    {
        PrimReport()
        {
            clear();
        }

        void clear()
        {
            sphereCount = 0;
            cylinderCount = 0;
            coneCount = 0;
            cubeCount = 0;
        }

        std::atomic<size_t> sphereCount;
        std::atomic<size_t> cylinderCount;
        std::atomic<size_t> coneCount;
        std::atomic<size_t> cubeCount;
    };

    /// Parameters
    PrimOptions m_options;

    /// Internal state
    HashCache m_hash_cache;
    PrimReport m_prim_report;
};

} // namespace omni::scene::optimizer
