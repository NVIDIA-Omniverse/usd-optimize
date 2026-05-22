// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/OmniOperation.h>
#include <omni/scene.optimizer/core/Utils.h>

// OmniMeshOps
#include <OmniMeshOps/Primitive.h>


namespace omni::scene::optimizer
{

/// Fit primitives to meshes using OmniMesh.
class PrimitiveFitOperation : public OmniOperation
{
public:
    PrimitiveFitOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    /// Analysis mode
    bool getSupportsAnalysis() const override;

protected:
    PXR_NS::Usd_PrimFlagsPredicate primFlagsPredicate() const override
    {
        // Unlike the default predicate, this allows undefined and abstract prims
        return PXR_NS::UsdPrimIsActive && PXR_NS::UsdPrimIsLoaded;
    }

    /// Process
    ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context&) override;

    /// Pre
    OperationResult executePre() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

    /// Post
    void executePost(const TotalStats& totalStats) override;

private:
    /// Used for analysis mode
    struct MeshReport
    {
        MeshReport()
        {
            clear();
        }

        void clear()
        {
            totalMeshCount = 0;
            totalFaceCount = 0;
            totalVertexCount = 0;
            composedCount = 0;
            multimaterialCount = 0;
            for (auto& v : fitMeshCount)
            {
                v.store(0, std::memory_order_relaxed);
            }
            for (auto& v : fitFaceCount)
            {
                v.store(0, std::memory_order_relaxed);
            }
            for (auto& v : fitVertexCount)
            {
                v.store(0, std::memory_order_relaxed);
            }
            for (auto& v : fitNonConstPrimvarMeshCount)
            {
                v.store(0, std::memory_order_relaxed);
            }
            for (auto& v : fitNonConstPrimvarFaceCount)
            {
                v.store(0, std::memory_order_relaxed);
            }
            for (auto& v : fitNonConstPrimvarVertexCount)
            {
                v.store(0, std::memory_order_relaxed);
            }
        }

        std::atomic<size_t> totalMeshCount;
        std::atomic<size_t> totalFaceCount;
        std::atomic<size_t> totalVertexCount;
        std::atomic<size_t> composedCount;
        std::atomic<size_t> multimaterialCount;
        std::atomic<size_t> fitMeshCount[omo::PrimitiveType::Count];
        std::atomic<size_t> fitFaceCount[omo::PrimitiveType::Count];
        std::atomic<size_t> fitVertexCount[omo::PrimitiveType::Count];
        std::atomic<size_t> fitNonConstPrimvarMeshCount[omo::PrimitiveType::Count];
        std::atomic<size_t> fitNonConstPrimvarFaceCount[omo::PrimitiveType::Count];
        std::atomic<size_t> fitNonConstPrimvarVertexCount[omo::PrimitiveType::Count];
    };

    /// Process
    void removeUnusedAttributes();

    /// Analyze the stage
    OperationResult recordAnalysis();

    /// Parameters to use for mesh generation
    struct MeshParameters
    {
        omo::SphereMeshParameters sphereParameters;
        omo::CylinderMeshParameters cylinderParameters;
        omo::ConeMeshParameters coneParameters;
        omo::CubeMeshParameters cubeParameters;

        const omo::PrimitiveMeshParameters& forType(omo::PrimitiveType::Enum type) const
        {
            const static omo::PrimitiveMeshParameters zeroParams = { 0, 0, false };

            switch (type)
            {
            case omo::PrimitiveType::Sphere:
                return sphereParameters;
            case omo::PrimitiveType::Cylinder:
                return cylinderParameters;
            case omo::PrimitiveType::Cone:
                return coneParameters;
            case omo::PrimitiveType::Cube:
                return cubeParameters;
            default:
                SO_LOG_ERROR("MeshParameters::forType: Bad primitive type given.");
                return zeroParams;
            }
        }
    };

    /// Parameters
    uint32_t m_gpuFaceCountThreshold;
    bool m_showFittingParameters;
    float m_vertexTolerance;
    float m_volumeTolerance;
    bool m_ignoreNonConstPrimvars;
    bool m_ignoreSubsets;
    bool m_allowNegativeVolume;
    bool m_allowMissingEndcaps;
    bool m_fitSphere;
    bool m_fitCylinder;
    bool m_fitCone;
    bool m_fitCube;
    bool m_generateMeshes;
    MeshParameters m_meshParameters;

    /// Derived data
    int m_shapeMask = int(omo::PrimitiveTypeMask::All);

    /// Internal state
    HashCache m_hashCache;
    MeshReport m_meshReport;
};

} // namespace omni::scene::optimizer
