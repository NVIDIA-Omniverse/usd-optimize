// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>


namespace omni::scene::optimizer
{


enum class DetectionMethod
{
    eWorldSpace = 0, // Detect small geometry by checking its maximum extent size against an absolute worldspace value
    ePercentage = 1, // Detect small geometry by checking its extent size against a percentage of the scene's median
                     // extent size
};


/// Operation for identifying and removing small and/or degenerate geometry from a USD stage.
class RemoveSmallGeometryOperation : public Operation
{

public:
    /// Constructor
    explicit RemoveSmallGeometryOperation();

    /// Destructor
    ~RemoveSmallGeometryOperation() override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    // Returns whether or not this operation supports analysis mode
    bool getSupportsAnalysis() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    std::vector<std::string> m_paths;
    RemoveMethod m_removeMethod = RemoveMethod::eDelete;
    DetectionMethod m_detectionMethod = DetectionMethod::eWorldSpace;
    float m_threshold = 0.0;

    /// finds and returns a list of the prims that are small or degenerate
    std::vector<PXR_NS::UsdPrim> findSmallGeometry();
};

} // namespace omni::scene::optimizer
