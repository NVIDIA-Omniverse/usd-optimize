// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"

// USD
#include <pxr/usd/usd/primRange.h>


namespace omni::scene::optimizer
{

/// This is a helper function for _resolveExpressionsToPrims.
///
/// It takes a specific prim and filters it out based on various conditions (invalid, /, a camera, etc).
/// If it is valid it will be appended to \p prims.
/// If specified, a uniqueness check will be done on \p primsSet to ensure duplicate prims are not
/// added.
///
/// If enabled, \p checkParents will check all the ancestors of a prim to see if they are a type
/// we would normally filter or prune, like a Camera or Light. These prims can have descendants that
/// are meshes or xforms, but generally speaking, we don't want to touch these in SO operations.
/// Note that this is expensive, so calling code that is already traversing and can prune should
/// do that and disable this argument.
///
/// \param usdStage The stage the prim exists in
/// \param prim The prim
/// \param primsSet Optional set for uniqueness
/// \param checkParents If specified, check ancestors to see if they are a camera/light.
/// \return Whether the prim was valid and added to \p prims.
OMNI_SO_EXPORT
bool _resolvePrim(const PXR_NS::UsdStageWeakPtr& usdStage,
                  const PXR_NS::UsdPrim& prim,
                  std::set<PXR_NS::UsdPrim>* primsSet,
                  bool checkParents = true);


/// Optional callback that can be further used to filter whether prims resolve.
///
/// \param prim The prim being resolved
/// \param iter The current iterator, which could be pruned if required
///
/// \return A bool indicating whether the prim is valid to resolve.
using ResolveFilter = std::function<bool(const PXR_NS::UsdPrim& prim, PXR_NS::UsdPrimRange::iterator& iterator)>;

/// Resolve prim paths/expressions to UsdPrims.
///
/// Iterates a prim and its descendants. If \p paths is populated, then all children
/// are filtered against the expressions. It can contain anything valid for an SdfPathExpression,
/// so either absolute prim paths or an actual expression.
///
/// If \p paths is empty, then all prims are returned.
///
/// \param rootPrim The prim from which to iterate
/// \param paths A vector of prim paths or SdfPathExpression expressions
/// \param meshesOnly Whether only meshes should be returned
/// \param reverse Reverse the order of the result before returning
/// \param predicate Predicate to filter stage traversal
/// \param filter Optional callback for calling code to provide custom filtering
///
/// \return Vector of prims
OMNI_SO_EXPORT
std::vector<PXR_NS::UsdPrim> _resolveExpressionsToPrims(const PXR_NS::UsdPrim& rootPrim,
                                                        const std::vector<std::string>& paths,
                                                        bool meshesOnly,
                                                        bool reverse,
                                                        const PXR_NS::Usd_PrimFlagsPredicate& predicate,
                                                        const ResolveFilter& filter);

/// Resolve prim paths/expressions to UsdPrims.
///
/// This function is an overload that provides an empty callback.
OMNI_SO_EXPORT
std::vector<PXR_NS::UsdPrim> _resolveExpressionsToPrims(
    const PXR_NS::UsdPrim& rootPrim,
    const std::vector<std::string>& paths,
    bool meshesOnly = false,
    bool reverse = true,
    const PXR_NS::Usd_PrimFlagsPredicate& predicate = PXR_NS::UsdPrimDefaultPredicate);

/// Resolve prim paths/expressions to UsdPrims.
///
/// This function is an overload that calls the base version with the PseudoRoot of the specified
/// usd stage.
OMNI_SO_EXPORT
std::vector<PXR_NS::UsdPrim> _resolveExpressionsToPrims(
    const PXR_NS::UsdStageWeakPtr& usdStage,
    const std::vector<std::string>& paths,
    bool meshesOnly = false,
    bool reverse = true,
    PXR_NS::Usd_PrimFlagsPredicate predicate = PXR_NS::UsdPrimDefaultPredicate);

OMNI_SO_EXPORT
[[deprecated("Use _resolveExpressionsToPrims")]] std::vector<PXR_NS::UsdPrim> _resolvePathsToPrims(
    const PXR_NS::UsdStageWeakPtr& usdStage,
    const std::vector<std::string>& paths,
    bool considerHierarchy,
    bool meshesOnly = false,
    bool reverse = true);

// Consolidated path resolver.
// Input argument "paths" can contain absolute USD stage paths,
// regular expressions that will be resolved to one or more
// absolute USD stage paths, or prim names that will be expanded
// to absolute paths.
// Maintained for SdfPathVector compatibility
OMNI_SO_EXPORT
PXR_NS::SdfPathVector _resolveSdfPaths(const PXR_NS::UsdStageWeakPtr& usdStage, const std::vector<std::string>& paths);

OMNI_SO_EXPORT
[[deprecated("considerHierarchy flag removed, use previous function")]] PXR_NS::SdfPathVector _resolveSdfPaths(
    const PXR_NS::UsdStageWeakPtr& usdStage,
    const std::vector<std::string>& paths,
    bool considerHierarchy);

} // namespace omni::scene::optimizer
