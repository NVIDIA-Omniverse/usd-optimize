// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/RemovePrims.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/ResolveSdfPaths.h"

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((hidden, "hidden"))
);
// LCOV_EXCL_STOP
// clang-format on


void _deletePrims(const UsdStageWeakPtr& usdStage, const std::vector<UsdPrim>& prims, bool deactivate, bool deleteOvers)
{
    // Early out if no prims were provided.
    if (prims.empty())
    {
        return;
    }

    // Cache the default prim
    const UsdPrim& defaultPrim = usdStage->GetDefaultPrim();

    // Map of parent to the children to be deleted.
    // This allows us to compress all children of a parent in to one set, rather than
    // individually deleting children. On larger scenes this reduces recomposition
    // substantially.
    std::map<SdfPrimSpecHandle, std::set<SdfPrimSpecHandle>> parentToChildren;

    {
        SdfChangeBlock changeBlock;

        auto editLayer = usdStage->GetEditTarget().GetLayer();

        for (const UsdPrim& prim : prims)
        {

            if (!prim.IsValid())
            {
                continue;
            }

            if (defaultPrim.IsValid() && defaultPrim == prim)
            {
                usdStage->ClearDefaultPrim();
            }

            bool removed = false;

            const auto& primStack = prim.GetPrimStack();
            for (auto&& primSpec : primStack)
            {
                auto layer = primSpec->GetLayer();

                // Only remove from the stage edit target layer
                if (editLayer != layer)
                {
                    continue;
                }
                // this is common for prototype prims, we want to deactivate the prototypes rather than delete them
                if (primSpec->GetPath() != prim.GetPath())
                {
                    continue;
                }

                // Skip specs that are an over in this layer (assuming there is a def elsewhere that we might be
                // able to set inactive, but can't necessarily delete the over)
                if (!deleteOvers && primSpec->GetSpecifier() == SdfSpecifierOver)
                {
                    continue;
                }

                // Get parent and remove this child
                auto parentSpec = primSpec->GetRealNameParent();
                if (!parentSpec)
                {
                    continue;
                }

                parentToChildren[parentSpec].insert(primSpec);

                removed = true;
                break;
            }

            // Couldn't remove the prim (an over, not in the stage, etc). Set it inactive instead.
            if (!removed && deactivate)
            {
                prim.SetActive(false);
            }
        }
    }

    {
        SdfChangeBlock changeBlock;

        SdfPrimSpecHandleVector newChildren;

        for (const auto& it : parentToChildren)
        {
            // Depending on the order, it's possible we deleted something that was a parent
            // of this, so verify the pointer before continuing
            if (!it.first)
            {
                continue; // LCOV_EXCL_LINE
            }

            const auto& children = it.first->GetNameChildren();
            newChildren.clear();

            // Check the existing children. Append any that were NOT marked
            // for delete to newChildren, then set newChildren.
            for (const auto& child : children)
            {
                if (!it.second.count(child))
                {
                    newChildren.push_back(child);
                }
            }

            it.first->SetNameChildren(newChildren);
        }
    }
}


void _deletePrims(const UsdStageWeakPtr& usdStage, const std::vector<std::string>& primPaths)
{

    // Early out if no paths were supplied
    if (primPaths.empty())
    {
        return;
    }

    // Resolve paths to prims
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(usdStage, primPaths);

    // Delete
    _deletePrims(usdStage, prims);
}


void _deactivatePrims(const std::vector<UsdPrim>& prims)
{
    for (const auto& prim : prims)
    {
        prim.SetActive(false);
    }
}


void _hidePrims(const std::vector<UsdPrim>& prims)
{
    for (const auto& prim : prims)
    {
        UsdGeomImageable imageable(prim);
        if (imageable)
        {
            imageable.GetVisibilityAttr().Set(UsdGeomTokens->invisible);
        }
    }
}


void _setAttributeOnPrims(const std::vector<UsdPrim>& prims, bool hidden)
{
    for (const auto& prim : prims)
    {
        UsdAttribute hiddenAttr = prim.GetAttribute(_tokens->hidden);
        if (!hiddenAttr)
        {
            hiddenAttr = prim.CreateAttribute(_tokens->hidden, SdfValueTypeNames->Bool);
        }
        if (hiddenAttr)
        {
            hiddenAttr.Set(hidden);
        }
    }
}


void _removePrims(RemoveMethod method,
                  const UsdStageWeakPtr& usdStage,
                  const std::vector<UsdPrim>& removePrims,
                  const std::vector<UsdPrim>& visiblePrims)
{
    switch (method)
    {
    case RemoveMethod::eIgnore:
        break;
    case RemoveMethod::eDelete:
        _deletePrims(usdStage, removePrims);
        break;
    case RemoveMethod::eDeactivate:
        _deactivatePrims(removePrims);
        break;
    case RemoveMethod::eHide:
        _hidePrims(removePrims);
        break;
    case RemoveMethod::eSetAttribute:
        _setAttributeOnPrims(removePrims, true);
        _setAttributeOnPrims(visiblePrims, false);
        break;
    }
}

} // namespace omni::scene::optimizer
