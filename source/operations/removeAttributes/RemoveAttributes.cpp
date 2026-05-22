// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "RemoveAttributes.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// TBB
#include <tbb/parallel_for.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::RemoveAttributesOperation);


namespace omni::scene::optimizer
{


RemoveAttributesOperation::RemoveAttributesOperation()
    : Operation("removeAttributes", "Remove Attributes", "Remove attributes from prims")
{
    addArgument("paths", "Prim Paths", kDisplayTypePrimPaths, "A list of prim paths to consider", m_primPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("mode", "Mode", kDisplayTypeEnum, "What to do with matching attributes", m_mode)
        .setEnumValues<Mode>({
            { Mode::eRemove, "Remove" },
            { Mode::eBlock, "Block" },
        });

    addArgument("attributes",
                "Attributes",
                kDisplayTypeAttributeList,
                "A list of attributes or namespaces to remove",
                m_attributes)
        .setPlaceholder("Add attribute/namespace");
}


std::string RemoveAttributesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion RemoveAttributesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string RemoveAttributesOperation::getCategory() const
{
    constexpr const char* s_category = "REMOVEATTRIBUTES";
    return s_category;
}


std::string RemoveAttributesOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


OperationResult RemoveAttributesOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|RemoveAttributes|Execute");

    // Create a set of the attribute names for faster lookup, and extract namespaces
    // to a separate vector.
    std::set<TfToken> attributeNames;
    std::vector<std::string> namespaces;

    for (const auto& value : m_attributes)
    {
        // We don't validate the namespace like we do the attribute as it's more annoying,
        // it doesn't really matter as an invalid one just wouldn't match any valid
        // attributes.
        if (TfStringEndsWith(value, ":"))
        {
            namespaces.emplace_back(value);
        }
        else
        {
            if (SdfPath::IsValidNamespacedIdentifier(value))
            {
                attributeNames.insert(TfToken(value));
            }
            else
            {
                SO_LOG_WARN("Invalid attribute name: %s", value.c_str());
            }
        }
    }

    // Verify there is something to do
    if (attributeNames.empty() && namespaces.empty())
    {
        SO_LOG_WARN("Operation called with no valid attributes or namespaces");
        return { false };
    }

    // Resolve prims
    constexpr bool meshesOnly = false;
    constexpr bool reverse = false;
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(getUsdStage(), m_primPaths, meshesOnly, reverse);

    // Vector of attributes found for removal.
    std::vector<UsdAttribute> toRemove;
    std::mutex toRemoveMutex;

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, prims.size()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            // Per-thread attributes to remove.
            // Collecting per thread and then combining into the overall result is more
            // efficient than e.g. using tbb::combinable, where we have to pay the cost
            // of repeated and sequential vector copies at the end.
            std::vector<UsdAttribute> _toRemove;

            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                const auto& prim = prims[i];
                const std::vector<UsdAttribute>& attributes = prim.GetAuthoredAttributes();

                for (const auto& attribute : attributes)
                {
                    // Check explicit attributes
                    if (attributeNames.find(attribute.GetName()) != attributeNames.end())
                    {
                        SO_LOG_VERBOSE("Matched attribute %s", attribute.GetPath().GetAsString().c_str());
                        _toRemove.push_back(attribute);
                        continue;
                    }

                    // Check namespaces
                    if (!namespaces.empty())
                    {
                        if (std::any_of(namespaces.cbegin(),
                                        namespaces.cend(),
                                        [&](const std::string& _namespace)
                                        { return TfStringStartsWith(attribute.GetName(), _namespace); }))
                        {
                            SO_LOG_VERBOSE("Matched namespace %s", attribute.GetPath().GetAsString().c_str());
                            _toRemove.push_back(attribute);
                        }
                    }
                }
            }

            // Append to overall results
            {
                std::lock_guard guard(toRemoveMutex);
                toRemove.insert(toRemove.end(), _toRemove.begin(), _toRemove.end());
            }
        });

    // Process anything that matched
    if (!toRemove.empty())
    {
        // Log output
        std::string operation = m_mode == Mode::eRemove ? "Removing" : "Blocking";
        std::string suffix = toRemove.size() == 1 ? "" : "s";
        SO_LOG_INFO("%s %lu attribute%s", operation.c_str(), toRemove.size(), suffix.c_str());

        SdfChangeBlock _changeBlock;

        for (const auto& attribute : toRemove)
        {
            switch (m_mode)
            {
            case Mode::eRemove:
                attribute.GetPrim().RemoveProperty(attribute.GetName());
                break;
            case Mode::eBlock:
                attribute.Block();
                break;
            }
        }
    }
    else
    {
        SO_LOG_INFO("Found no attributes to process");
    }

    return { true };
}


} // namespace omni::scene::optimizer
