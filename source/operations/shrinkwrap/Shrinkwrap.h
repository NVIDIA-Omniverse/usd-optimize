// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>


namespace omni::scene::optimizer
{

/// Shrinkwrap meshes using a level set approach via OpenVDB.
///
/// Converts meshes to a level set volume using polySoupToLevelSet and extracts
/// a watertight mesh back out using volumeToMesh. Resolution is controlled by
/// either a grid dimension or explicit voxel size.
class ShrinkwrapOperation : public Operation
{
public:
    ShrinkwrapOperation();

    ~ShrinkwrapOperation() override;

    std::string getAuthor() const override;

    SOPluginVersion getVersion() const override;

    std::string getCategory() const override;

    std::string getDisplayGroup() const override;

protected:
    OperationResult executeImpl() override;

private:
    std::vector<std::string> m_meshPrimPaths;

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

} // namespace omni::scene::optimizer
