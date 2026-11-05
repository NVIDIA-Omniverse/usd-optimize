// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Usd Optimize Core
#include <usd_optimize/core/OmniOperation.h>

// TBB
#include <tbb/concurrent_vector.h>


namespace usd_optimize
{

// Type of normals, how they are bound to the mesh
// NOTE: Integer values are part of the serialized config format.
// New values must be appended to preserve backward compatibility.
enum class Binding
{
    ePerCorner = 0, // One normal per corner (vertex index)
    ePerFace = 1, // One normal per face
    ePerVertex = 2, // One normal per vertex (point)
    eAuto = 3 // Automatically determine based on existing normals space-efficiency
};

// Type of weighting to use when computing normals
enum class WeightMode
{
    eAngle = 0,
    eArea = 1
};

// What to do with existing normals?
enum class ExistingNormals
{
    eFix = 0,
    eReplace = 1
};

/// Generate normals using OmniMesh.
class GenerateNormalsOperation : public OmniOperation
{
public:
    GenerateNormalsOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    UsdOptimizePluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Support Analysis
    bool getSupportsAnalysis() const override;

protected:
    PXR_NS::Usd_PrimFlagsPredicate primFlagsPredicate() const override
    {
        // Unlike the default predicate, this allows undefined and abstract prims
        return PXR_NS::UsdPrimIsActive && PXR_NS::UsdPrimIsLoaded;
    }

    ResolveFilter resolveFilter() override
    {
        // skip an `over` itself: an override, often incomplete by design, rather than a mesh to process
        return [filter = OmniOperation::resolveFilter()](const PXR_NS::UsdPrim& prim, PXR_NS::UsdPrimRange::iterator& it)
        { return prim.HasDefiningSpecifier() && filter(prim, it); };
    }

    /// Process
    ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context&) override;

    /// Post
    void executePost(const TotalStats& totalStats) override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

    /// Analyze the stage
    OperationResult recordAnalysis();

private:
    /// Used for analysis mode
    struct Report
    {
        Report()
        {
            clear();
        }

        void clear()
        {
            totalNonUnitLengthStrict = 0;
            nonUnitLengthStrictPaths.clear();
        }

        std::atomic<size_t> totalNonUnitLengthStrict;
        // Per-prim paths backing the count above (verbose reporting). Always
        // populated; consumers choose whether to surface them.
        tbb::concurrent_vector<PXR_NS::UsdPrim> nonUnitLengthStrictPaths;
    };

    ExistingNormals m_existingNormals;
    float m_sharpnessAngle;
    Binding m_binding;
    WeightMode m_weightMode;
    unsigned int m_gpuThreshold;

    Report m_report;
};

} // namespace usd_optimize
