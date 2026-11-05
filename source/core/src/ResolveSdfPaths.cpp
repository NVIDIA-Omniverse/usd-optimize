// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "usd_optimize/core/ResolveSdfPaths.h"

// Usd Optimize Core
#include "usd_optimize/core/Core.h"
#include "usd_optimize/core/Utils.h"

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/sdf/pathExpressionEval.h>
#include <pxr/usd/sdf/pathPattern.h>
#include <pxr/usd/usd/collectionPredicateLibrary.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdLux/lightAPI.h>

// C++
#include <algorithm>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE


namespace usd_optimize
{


bool _resolvePrim(const UsdStageWeakPtr& usdStage, const UsdPrim& prim, std::set<UsdPrim>* primsSet, bool checkParents)
{
    if (primsSet != nullptr)
    {
        // Easy early-out if we have already seen this prim. If not, ensure it is recorded for next time.
        auto insertIt = primsSet->insert(prim);

        if (!insertIt.second)
        {
            return false;
        }
    }

    if (!prim.IsValid())
    {
        return false;
    }

    const SdfPath& path = prim.GetPrimPath();

    if (path.IsEmpty() || path.IsAbsoluteRootPath())
    {
        return false;
    }

    // Get prefixes.
    // If checking parents, we need all prefixes (pass 0). If not, just get the first.
    // Note: don't need to clear, as GetPrefixes will resize and populate accordingly.
    thread_local SdfPathVector prefixes;
    path.GetPrefixes(&prefixes, checkParents ? 0 : 1);

    // Apply some omni specific filters for certain prim names.
    const TfToken& firstComponent = prefixes.front().GetNameToken();

    if (TfStringStartsWith(firstComponent.GetString(), "Render"))
    {
        return false;
    }

    if (TfStringStartsWith(firstComponent.GetString(), "OmniverseKit_"))
    {
        return false;
    }

    // If specified, check all ancestors of this prim. Xforms/Meshes can exist underneath cameras/lights,
    // so unless the calling code has already pruned we want to ensure we don't resolve them.
    if (checkParents)
    {
        for (const auto& prefixPath : prefixes)
        {
            const auto& parentPrim = usdStage->GetPrimAtPath(prefixPath);
            if (parentPrim.IsA<UsdGeomCamera>() || UsdLuxLightAPI(parentPrim))
            {
                return false;
            }
        }
    }

    // Validations passed, this is a prim we can add.
    return true;
}

// Helper struct to resolve a path to a UsdObject for the
// CollectionPredicateLibrary
struct PathToObj
{
    UsdObject operator()(const SdfPath& path) const
    {
        return stage->GetObjectAtPath(path);
    }

    UsdStageWeakPtr stage;
};


// Literal prim paths skip SdfPathExpression's text grammar, which rejects non-ASCII prim names.
static SdfPathExpression _makePathExpression(const std::vector<std::string>& paths)
{
    for (const std::string& path : paths)
    {
        if (!SdfPath::IsValidPathString(path) || !SdfPath(path).IsAbsoluteRootOrPrimPath())
        {
            // Entries may split one expression, so any pattern parses the joined list; non-ASCII paths in it fail.
            return SdfPathExpression(TfStringJoin(paths, " "));
        }
    }

    SdfPathExpression expression;

    for (const std::string& path : paths)
    {
        SdfPathExpression atom = SdfPathExpression::MakeAtom(SdfPathPattern(SdfPath(path)));

        expression = expression.IsEmpty() ?
                         std::move(atom) :
                         SdfPathExpression::MakeOp(SdfPathExpression::Union, std::move(expression), std::move(atom));
    }

    return expression;
}


bool _isValidPathsArgument(const std::vector<std::string>& paths)
{
    return std::all_of(paths.begin(),
                       paths.end(),
                       [](const std::string& path) { return SdfPath::IsValidPathString(path); }) ||
           !_makePathExpression(paths).IsEmpty();
}


using PathExprEval = SdfPathExpressionEval<UsdObject>;

struct MatchEval
{

    explicit MatchEval(const UsdStageWeakPtr& stage, const SdfPathExpression& expr)
        : _stage(stage)
        , _eval(SdfMakePathExpressionEval(expr, UsdGetCollectionPredicateLibrary()))
    {
    }

    explicit MatchEval(const UsdStageWeakPtr& stage, const std::string& exprStr)
        : MatchEval(stage, SdfPathExpression(exprStr))
    {
    }

    SdfPredicateFunctionResult Match(const SdfPath& p) const
    {
        return _eval.Match(p, PathToObj{ _stage });
    }

    UsdStageWeakPtr _stage;
    PathExprEval _eval;
};


std::vector<UsdPrim> _resolveExpressionsToPrims(const UsdPrim& rootPrim,
                                                const std::vector<std::string>& paths,
                                                bool meshesOnly,
                                                bool reverse,
                                                const Usd_PrimFlagsPredicate& predicate,
                                                const ResolveFilter& filter)
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|_resolveExpressionsToPrims");

    std::vector<UsdPrim> resolvedPrims; // use vector to preserve order of insertion

    std::vector<std::string> expressions;
    expressions.reserve(paths.size());

    std::unique_ptr<MatchEval> eval = nullptr;
    if (!paths.empty())
    {
        SdfPathExpression expression = _makePathExpression(paths);

        if (expression.IsEmpty())
        {
            USD_OPTIMIZE_LOG_ERROR("Invalid Expression: %s", TfStringJoin(paths, " ").c_str());
            return {};
        }

        eval = std::make_unique<MatchEval>(rootPrim.GetStage(), expression);
    }

    const UsdStageWeakPtr& stage = rootPrim.GetStage();

    // Iterate.
    UsdPrimRange range(rootPrim, predicate);
    for (auto iter = range.begin(); iter != range.end(); ++iter)
    {
        const auto& prim = (*iter);

        // There's a bit of repeated validation here to try and avoid regex matching and prune
        // where possible.
        if (!prim.IsValid() || prim.GetPrimPath().IsAbsoluteRootPath())
        {
            continue;
        }

        // Ignore cameras and prune
        if (prim.IsA<UsdGeomCamera>())
        {
            iter.PruneChildren();
            continue;
        }

        // Also filter lights
        if (UsdLuxLightAPI(prim))
        {
            iter.PruneChildren();
            continue;
        }

        // Some minor optimizations for "meshes only".
        if (meshesOnly)
        {
            // Make the assumption we won't encounter meshes inside a material
            if (prim.IsA<UsdShadeMaterial>())
            {
                iter.PruneChildren();
                continue;
            }

            if (!prim.IsA<UsdGeomMesh>())
            {
                continue;
            }
        }

        // If there are expressions then eval. Failure means skip.
        if (eval && !eval->Match(prim.GetPrimPath()))
        {
            continue;
        }

        // If an optional filter callback is specified use it to check whether this prim
        // should be filtered out.
        if (filter && !filter(prim, iter))
        {
            continue;
        }

        // At this point, use the resolve function. If that succeeds then append to the result.
        // Note 1: do not use a set/cache, we are doing a single traversal and essentially do not get cache
        // hits, so this is just redundant inserts/finds.
        // Note 2: do not checkParents, we pruned the relevant prims just above.
        constexpr bool checkParents = false;
        if (_resolvePrim(stage, prim, nullptr, checkParents))
        {
            resolvedPrims.push_back(prim);
        }
    }

    // Reverse paths to start at leaves so traversal for deletion will work
    if (reverse)
    {
        std::reverse(resolvedPrims.begin(), resolvedPrims.end());
    }

    return resolvedPrims;
}


std::vector<UsdPrim> _resolveExpressionsToPrims(const UsdPrim& rootPrim,
                                                const std::vector<std::string>& paths,
                                                bool meshesOnly,
                                                bool reverse,
                                                const Usd_PrimFlagsPredicate& predicate)
{
    return _resolveExpressionsToPrims(rootPrim, paths, meshesOnly, reverse, predicate, nullptr);
}


std::vector<UsdPrim> _resolveExpressionsToPrims(const UsdStageWeakPtr& usdStage,
                                                const std::vector<std::string>& paths,
                                                bool meshesOnly,
                                                bool reverse,
                                                Usd_PrimFlagsPredicate predicate)
{
    return _resolveExpressionsToPrims(usdStage->GetPseudoRoot(), paths, meshesOnly, reverse, predicate);
}


/// Maintained for SdfPathVector compatibility
SdfPathVector _resolveSdfPaths(const UsdStageWeakPtr& usdStage, const std::vector<std::string>& paths)
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|_resolveSdfPaths");

    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(usdStage, paths);
    return _convertToSdfPaths(prims);
}


// Maintained for backwards compatibility, as considerHierarchy is no longer used
//
// Exclude from coverage, the following functions are marked deprecated
// LCOV_EXCL_START
std::vector<UsdPrim> _resolvePathsToPrims(const UsdStageWeakPtr& usdStage,
                                          const std::vector<std::string>& paths,
                                          bool considerHierarchy,
                                          bool meshesOnly,
                                          bool reverse)
{
    return _resolveExpressionsToPrims(usdStage, paths, meshesOnly, reverse);
}

/// Maintained for SdfPathVector compatibility
SdfPathVector _resolveSdfPaths(const UsdStageWeakPtr& usdStage,
                               const std::vector<std::string>& paths,
                               bool considerHierarchy)
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|_resolveSdfPaths");

    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(usdStage, paths);
    return _convertToSdfPaths(prims);
}
// LCOV_EXCL_STOP

} // namespace usd_optimize
