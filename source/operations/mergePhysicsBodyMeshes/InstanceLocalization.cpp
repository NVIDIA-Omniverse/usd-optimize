// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "InstanceLocalization.h"

#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/primRange.h>

#include <map>
#include <set>
#include <stdexcept>
#include <utility>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{
namespace
{

/// Snapshot composed prim and property content, including descendants reached through instance proxies.
/// GetAllMetadata excludes composition arcs; the root's instanceable flag is the intended change.
std::map<SdfPath, VtDictionary> _instanceContents(const UsdPrim& root)
{
    std::map<SdfPath, VtDictionary> contents;
    for (const auto& prim : UsdPrimRange(root, UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate)))
    {
        auto metadata = prim.GetAllMetadata();
        if (prim == root)
        {
            metadata.erase(SdfFieldKeys->Instanceable);
        }
        contents[prim.GetPath()] = { { "metadata", VtValue(metadata) },
                                     { "schemas", VtValue(prim.GetAppliedSchemas()) },
                                     { "active", VtValue(prim.IsActive()) },
                                     { "loaded", VtValue(prim.IsLoaded()) } };
        for (const auto& attribute : prim.GetAttributes())
        {
            VtValue value;
            attribute.Get(&value);
            std::vector<double> times;
            attribute.GetTimeSamples(&times);
            std::map<double, VtValue> samples;
            for (const auto time : times)
            {
                attribute.Get(&samples[time], time);
            }
            SdfPathVector connections;
            attribute.GetConnections(&connections);
            contents[attribute.GetPath()] = { { "metadata", VtValue(attribute.GetAllMetadata()) },
                                              { "value", value },
                                              { "samples", VtValue(samples) },
                                              { "connections", VtValue(connections) } };
            VtValue spline;
            if (attribute.GetMetadata(SdfFieldKeys->Spline, &spline))
            {
                contents[attribute.GetPath()]["spline"] = std::move(spline);
            }
        }
        for (const auto& relationship : prim.GetRelationships())
        {
            SdfPathVector targets;
            relationship.GetTargets(&targets);
            contents[relationship.GetPath()] = { { "metadata", VtValue(relationship.GetAllMetadata()) },
                                                 { "targets", VtValue(targets) } };
        }
    }
    return contents;
}

/// Return the first object whose composed content was added, removed, or changed by localization.
SdfPath _changedContent(const std::map<SdfPath, VtDictionary>& before, const std::map<SdfPath, VtDictionary>& after)
{
    for (const auto& [path, content] : before)
    {
        const auto found = after.find(path);
        if (found == after.end() || found->second != content)
        {
            return path;
        }
    }
    for (const auto& [path, content] : after)
    {
        if (!before.count(path))
        {
            return path;
        }
    }
    return {};
}

/// Open the original composition with a private session overlay for reversible localization checks.
UsdStageRefPtr _localizationStage(const UsdStageWeakPtr& source)
{
    const auto session = SdfLayer::CreateAnonymous();
    if (const auto sourceSession = source->GetSessionLayer())
    {
        session->SetSubLayerPaths({ sourceSession->GetIdentifier() });
    }
    auto stage = UsdStage::OpenMasked(source->GetRootLayer(),
                                      session,
                                      source->GetPathResolverContext(),
                                      source->GetPopulationMask(),
                                      UsdStage::LoadNone);
    if (!stage)
    {
        throw std::runtime_error("Cannot check selected instance composition");
    }
    stage->MuteAndUnmuteLayers(source->GetMutedLayers(), {});
    stage->SetLoadRules(source->GetLoadRules());
    stage->SetEditTarget(session);
    return stage;
}

} // namespace

/// Open safe selected instances on the working stage, including nested instances exposed by earlier passes.
SdfPathVector makeInstancesLocal(const UsdStageWeakPtr& source,
                                 const UsdStageWeakPtr& working,
                                 const SdfPathVector& startPaths,
                                 std::vector<PathIssue>& skipped)
{
    const auto validation = _localizationStage(source);
    SdfPathVector localized;
    std::set<SdfPath> rejected;
    for (;;)
    {
        SdfPathVector instances;
        for (const auto& path : startPaths)
        {
            const auto start = working->GetPrimAtPath(path);
            if (!start || start.IsInstanceProxy())
            {
                continue;
            }
            const auto range = UsdPrimRange(start);
            for (auto iter = range.begin(); iter != range.end(); ++iter)
            {
                if (iter->IsInstance())
                {
                    if (!rejected.count(iter->GetPath()))
                    {
                        instances.push_back(iter->GetPath());
                    }
                    iter.PruneChildren();
                }
            }
        }
        if (instances.empty())
        {
            return localized;
        }
        for (const auto& path : instances)
        {
            const auto instance = validation->GetPrimAtPath(path);
            const auto before = _instanceContents(instance);
            if (!instance.SetInstanceable(false))
            {
                throw std::runtime_error("Cannot check selected instance: " + path.GetAsString());
            }
            const auto changed = _changedContent(before, _instanceContents(instance));
            if (!changed.IsEmpty())
            {
                instance.ClearInstanceable();
                rejected.insert(path);
                skipped.push_back({ path.GetAsString(),
                                    "localizing the instance changes composed content: " + changed.GetAsString() });
                continue;
            }
            if (!working->GetPrimAtPath(path).SetInstanceable(false))
            {
                throw std::runtime_error("Cannot prepare selected instance: " + path.GetAsString());
            }
            localized.push_back(path);
        }
    }
}

} // namespace usd_optimize::physics_body_meshes
