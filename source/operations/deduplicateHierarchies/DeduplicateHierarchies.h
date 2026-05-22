// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>

// std
#include <map>
#include <string>
#include <vector>


namespace omni::scene::optimizer
{

using HierarchyMap = std::map<PXR_NS::SdfPath, PXR_NS::SdfPathVector>;

/// Find duplicate prim hierarchies and convert duplicates into internal
/// instanceable references to the first instance (the "prototype").
///
/// This operates at the *hierarchy* level — it operates on whole subtrees,
/// not individual meshes. For per-mesh deduplication see
/// `deduplicateGeometry` (typically chained as a follow-up step).
///
/// Duplicates are identified by a structural hash of each subtree
/// (hierarchy shape, prim type names, sorted authored property names),
/// then refined by comparing all authored property values (excluding
/// xformOp values on the root prim, which are expected to differ between
/// instances).
///
/// Creates internal instanceable references from duplicate subtrees to the
/// first instance (the prototype).
class DeduplicateHierarchiesOperation : public Operation
{

public:
    /// Constructor
    explicit DeduplicateHierarchiesOperation();

    /// Destructor
    ~DeduplicateHierarchiesOperation() override;

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

    /// Entry-point for analysis (find duplicates without mutating stage)
    OperationResult executeAnalysisImpl() override;

private:
    /// Shared logic: find duplicate hierarchies and return the map.
    /// Does NOT mutate the stage.
    HierarchyMap _findDuplicates();
    /// Optional subtree restriction. Empty = walk children of the default prim.
    std::vector<std::string> m_paths;

    /// Tolerance for floating-point property comparison (stage units).
    double m_tolerance = 0.001;

    /// Skip shader output attributes (outputs:*) during value comparison.
    bool m_ignoreShaderOutputs = true;
};


} // namespace omni::scene::optimizer
