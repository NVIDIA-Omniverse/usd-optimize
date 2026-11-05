// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Usd Optimize Core
#include <usd_optimize/core/Operation.h>


namespace usd_optimize
{

/// Shrinkwrap meshes using a level set approach via OpenVDB.
///
/// Converts meshes to a level set volume using polySoupToLevelSet and extracts
/// a watertight mesh back out using volumeToMesh. Resolution is controlled by
/// either a grid dimension or explicit voxel size. It can also combine sampled
/// geometry over a time range into one static swept envelope.
class ShrinkwrapOperation : public Operation
{
public:
    ShrinkwrapOperation();

    ~ShrinkwrapOperation() override;

    std::string getDocumentation() const override;

    std::string getAuthor() const override;

    UsdOptimizePluginVersion getVersion() const override;

    std::string getCategory() const override;

    std::string getDisplayGroup() const override;

protected:
    OperationResult executeImpl() override;

private:
    enum class InputMode
    {
        ePerMesh = 0,
        eTemporalCombined = 1,
    };

    std::vector<std::string> m_meshPrimPaths;

    // Input sampling
    InputMode m_inputMode;
    bool m_useStageTimeRange;
    double m_startTime;
    double m_endTime;
    double m_timeStep;
    bool m_connectTimeSamples;
    std::string m_outputPath;

    // Resolution: voxelSize is the primary control; dim is a max limit
    unsigned int m_dim;
    double m_voxelSize;

    // Level set controls
    double m_erode;
    double m_threshold;

    // Mesh output controls
    double m_adaptivity;

    // LOD
    bool m_extractLodPyramid;
};

} // namespace usd_optimize
