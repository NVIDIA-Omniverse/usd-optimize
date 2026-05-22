// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>

// USD
#include <pxr/usd/usdGeom/metrics.h>


namespace omni::scene::optimizer
{


/// Possible transformations to a stage's up axis
enum class UpAxis
{
    eNone = 0, // No change
    eY = 1, // Y-up
    eZ = 2, // Z-up
};


/// Operation for editing a stage's metrics (meters per unit or axis) and baking the changes into the primitives
/// transforms and geometry data
class EditStageMetricsOperation : public Operation
{

public:
    /// Constructor
    explicit EditStageMetricsOperation();

    /// Destructor
    ~EditStageMetricsOperation() override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    /// Entry point
    OperationResult executeImpl() override;

private:
    double m_metersPerUnit = PXR_NS::UsdGeomLinearUnits::centimeters;
    UpAxis m_upAxis = UpAxis::eNone;
    bool m_collapseXforms = false;
    bool m_ignoreKitCameras = true;
};

} // namespace omni::scene::optimizer
