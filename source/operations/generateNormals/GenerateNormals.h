// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/OmniOperation.h>


namespace omni::scene::optimizer
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
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Support Analysis
    bool getSupportsAnalysis() const override;

protected:
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
        }

        std::atomic<size_t> totalNonUnitLengthStrict;
    };

    ExistingNormals m_existingNormals;
    float m_sharpnessAngle;
    Binding m_binding;
    WeightMode m_weightMode;
    unsigned int m_gpuThreshold;

    Report m_report;
};

} // namespace omni::scene::optimizer
