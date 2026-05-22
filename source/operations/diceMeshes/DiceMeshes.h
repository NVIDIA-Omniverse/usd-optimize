// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/OmniOperation.h>


namespace omni::scene::optimizer
{

enum class GridType
{
    eRegular = 0,
    eIrregular = 1
};

/// Generate normals using OmniMesh.
class DiceMeshesOperation : public OmniOperation
{
public:
    DiceMeshesOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context&);

    // Pre
    OperationResult executePre() override;

    // Post
    void executePost(const TotalStats& totalStats) override;

private:
    bool m_splitDices;
    GridType m_gridType;
    std::array<double, 3> m_gridCellSize;
    std::array<double, 3> m_gridOrigin;
    std::array<double, 3> m_upX;
    std::array<double, 3> m_upY;
    std::array<double, 3> m_upZ;
    std::string m_cutHeightsX;
    std::string m_cutHeightsY;
    std::string m_cutHeightsZ;
    bool m_advancedSettings;

    // parsed result from the strings above
    std::vector<double> m_parsedCutHeightsX;
    std::vector<double> m_parsedCutHeightsY;
    std::vector<double> m_parsedCutHeightsZ;
};

} // namespace omni::scene::optimizer
