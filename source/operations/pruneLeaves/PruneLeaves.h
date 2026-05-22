// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>

// USD
#include <pxr/usd/usd/primFlags.h>


namespace omni::scene::optimizer
{


/// Prune all leaf grouping primitives found in a stage.
///
/// Prunes any leaf grouping primitives (Scope, Xform) that are encountered in a stage.
/// Optionally specify specific paths to search for leaves.
class PruneLeavesOperation : public Operation
{
public:
    /// Constructor
    explicit PruneLeavesOperation();

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
    /// Find any leaf grouping prims underneath the specified prim.
    ///
    /// This function recursively finds the shallowest leaves starting from \p prim. That is, if a grouping prim has
    /// only other grouping prims as children, then it itself is considered the leaf. The intention is to provide the
    /// minimal number of prims that can be deleted/deactivated in order to prune all leaf grouping prims. As such,
    /// every individual leaf grouping prim not necessarily be included in the result.
    ///
    /// Populates \p leafPrims with the result.
    ///
    /// \param prim The prim to start traversal from.
    /// \param predicate prim predicate to control what is traversed
    /// \param leafPrims Output result
    /// \return bool indicating whether all children of the prim were leaf grouping prims.
    bool findLeaves(const PXR_NS::UsdPrim& prim,
                    PXR_NS::Usd_PrimFlagsPredicate predicate,
                    std::vector<PXR_NS::UsdPrim>& leafPrims) const;

    /// Specify specific Prim Paths to recursively search for leaves to prune (inclusive).
    ///
    /// \param primPaths The paths to search
    void setPrimsFromPaths(const std::vector<std::string>& primPaths);

    std::vector<std::string> m_primPaths;
    RemoveMethod m_mode = RemoveMethod::eDelete;
    std::vector<PXR_NS::UsdPrim> m_prims;
    bool m_filterInactive = false;
};

} // namespace omni::scene::optimizer
