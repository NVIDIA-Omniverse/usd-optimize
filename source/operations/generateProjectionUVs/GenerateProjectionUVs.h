// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>


namespace omni::scene::optimizer
{

/// The type of UV projection to use
enum class ProjectionType
{
    ePlanar = 0, // Projection to ZX plane
    eSpherical = 1, // Spherical projection with poles on Y axis
    eCylindrical = 2, // Cylindrical projection onto cylinder along Y
    eTriplanar = 3, // Triplanar projection (nearest plane)
    eCube = 4, // Cube projection
};


/// Generate Projection UVs
class GenerateProjectionUVsOperation : public Operation
{
public:
    /// Constructor
    explicit GenerateProjectionUVsOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

private:
    std::vector<std::string> m_meshPrimPaths;
    ProjectionType m_projectionType = ProjectionType::eCube;
    bool m_useWorldSpaceScales = true;
    std::vector<float> m_xformMatrixEntries;
    float m_scaleFactor = 0.01f;
    float m_scaleUnits = 0.0f;
    bool m_overwriteExisting = true;
};


} // namespace omni::scene::optimizer
