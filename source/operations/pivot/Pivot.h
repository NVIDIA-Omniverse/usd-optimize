// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>


namespace omni::scene::optimizer
{

/// This operation computes and sets the centroid of a prim as the pivot, unless a pivot has already been
/// authored.
///
/// It can do this for meshes, or meshes and xforms.
class PivotOperation : public Operation
{
    /// Enum describing what type of prims to apply a pivot to
    enum class ApplyTo
    {
        eMeshes = 0, ///< Only meshes
        eMeshesAndXforms = 1, ///< Meshes and Xforms
    };

    enum class Method
    {
        eWeighted = 0, ///< Average of points
        eCenter = 1, ///< Center of bounding box
    };

public:
    /// Constructor
    explicit PivotOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

private:
    /// Calculate and set the pivot of the specified prims.
    void pivot(const std::vector<PXR_NS::UsdPrim>& prims) const;

    std::vector<std::string> m_primPaths;
    bool m_overwrite = false;
    ApplyTo m_applyTo = ApplyTo::eMeshes;
    Method m_method = Method::eWeighted;
};


} // namespace omni::scene::optimizer
