// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>

// C++
#include <vector>


namespace omni::scene::optimizer
{

/// Enum to define Optimize Material operations
///
/// \ref OptimizeMaterialMode::eDeduplicate will remove duplicate materials, rebinding affected prims.
/// \ref OptimizeMaterialMode::eConvertToColor will attempt to replace materials with a simple displayColor.
/// \ref OptimizeMaterialMode::eRemoveUnbound will remove any materials that are not bound.
/// \ref OptimizeMaterialMode::eConvertToPrimvar will deduplicate materials with the same attributes, but different
/// values, by creating new materials with primvar readers
enum class OptimizeMaterialMode
{
    eDeduplicate = 0, // Deduplicate materials
    eConvertToColor = 1, // Replace materials with a color on the mesh
    eRemoveUnbound = 2, // Remove any materials that are not bound
    eDeduplicateWithPrimvars = 3, // Replace duplicate materials with a single material, driven by primvars
};

/// Optimize Materials Operation
///
/// Optimize materials based on the specified operation \p mode, default: Deduplicate Materials.
class OptimizeMaterialsOperation : public Operation
{
public:
    /// Constructor
    explicit OptimizeMaterialsOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    /// Support Analysis
    bool getSupportsAnalysis() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    /// Deduplicate materials.
    bool optimizeMaterials(const std::vector<PXR_NS::UsdPrim>& prims);

    /// Remove any unassigned materials.
    bool removeUnboundMaterials(const std::vector<PXR_NS::UsdPrim>& prims);

    /// Deduplicate materials with primvar readers
    bool convertToPrimvars(const std::vector<PXR_NS::UsdPrim>& prims) const;

    std::vector<std::string> m_primPaths;
    OptimizeMaterialMode m_mode = OptimizeMaterialMode::eDeduplicate;
    std::string m_materialsPath = "/World/Looks";
    bool m_analysisCheckPrimvars = true;
};


} // namespace omni::scene::optimizer
