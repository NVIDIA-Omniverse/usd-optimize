// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Usd Optimize Core
#include <usd_optimize/core/Operation.h>
#include <usd_optimize/core/RemovePrims.h>
#include <usd_optimize/core/UsdIncludes.h>

// USD
#include <pxr/usd/sdf/path.h>
#include <pxr/usd/usd/primFlags.h>

// C++
#include <unordered_map>


namespace usd_optimize
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
    UsdOptimizePluginVersion getVersion() const override;

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
                    const PXR_NS::Usd_PrimFlagsPredicate& predicate,
                    std::vector<PXR_NS::UsdPrim>& leafPrims) const;

    /// Collect the shallowest leaf grouping prims found among the children of \p prim.
    ///
    /// This is the child-iteration core shared by findLeaves() and by prototypeAllLeaves(). Unlike findLeaves(), the
    /// returned bool reflects only whether every child resolved to a leaf grouping prim - it is not gated by whether
    /// \p prim itself is a grouping prim. That distinction matters for prototype roots, which are typeless containers
    /// whose children we still want to classify.
    ///
    /// \param prim The prim whose children to classify.
    /// \param predicate prim predicate to control what is traversed
    /// \param leaves Output: the shallowest leaf grouping prims among the children.
    /// \return bool indicating whether every child of \p prim was a leaf grouping prim.
    bool findChildLeaves(const PXR_NS::UsdPrim& prim,
                         const PXR_NS::Usd_PrimFlagsPredicate& predicate,
                         std::vector<PXR_NS::UsdPrim>& leaves) const;

    /// Returns whether the prototype of \p instance is composed entirely of leaf grouping prims.
    ///
    /// The editable content of an instance lives in its prototype and is identical across every instance of that
    /// prototype, so the result is evaluated once per prototype and cached (keyed by prototype path) in
    /// m_prototypeLeafCache. This avoids re-walking the same prototype subtree once per instance via instance proxies.
    ///
    /// \param instance An instancing prim (IsInstance() == true).
    /// \param predicate prim predicate to control what is traversed
    /// \return bool indicating whether the instance's prototype contains only leaf grouping prims.
    bool prototypeAllLeaves(const PXR_NS::UsdPrim& instance, const PXR_NS::Usd_PrimFlagsPredicate& predicate) const;

    /// Specify specific Prim Paths to recursively search for leaves to prune (inclusive).
    ///
    /// \param primPaths The paths to search
    void setPrimsFromPaths(const std::vector<std::string>& primPaths);

    std::vector<std::string> m_primPaths;
    RemoveMethod m_mode = RemoveMethod::eDelete;
    std::vector<PXR_NS::UsdPrim> m_prims;
    bool m_filterInactive = false;
    bool m_preserveUnloadedPayloads = true;
    bool m_preserveRelationshipTargets = true;

    /// Per-prototype memoization of "does this prototype contain only leaf grouping prims", keyed by prototype path.
    /// Populated lazily during traversal by prototypeAllLeaves() and cleared at the start of each run, since the
    /// answer depends on the run's predicate (filterInactive) and payload-preservation settings.
    mutable std::unordered_map<PXR_NS::SdfPath, bool, PXR_NS::SdfPath::Hash> m_prototypeLeafCache;
};

} // namespace usd_optimize
