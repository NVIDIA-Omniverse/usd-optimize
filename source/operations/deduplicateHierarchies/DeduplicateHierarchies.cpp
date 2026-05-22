// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "DeduplicateHierarchies.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/Log.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD (extras beyond UsdIncludes.h)
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/subset.h>

// std
#include <algorithm>
#include <cinttypes>
#include <cstdio>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::DeduplicateHierarchiesOperation);


namespace omni::scene::optimizer
{

constexpr const char* s_categoryDedupHierarchies = "DEDUPLICATE_HIERARCHIES";

using PrimVector = std::vector<UsdPrim>;


// Material-related prim filter — mirrors `_is_material_related` in the Python
// processor. Keeps the C++ port in lock-step with the script's traversal so
// the two tiers produce comparable results on the same asset.
static bool _isMaterialRelated(const UsdPrim& prim)
{
    static const std::set<std::string> kMaterialScopes = { "Looks", "Materials" };
    static const std::vector<std::string> kTexturePrefixes = { "Diffuse",   "Specular", "Normal",
                                                               "Roughness", "Metallic", "Emissive",
                                                               "Opacity",   "AO",       "Displacement" };

    if (prim.IsA<UsdShadeMaterial>() || prim.IsA<UsdShadeShader>() || prim.IsA<UsdShadeNodeGraph>() ||
        prim.IsA<UsdGeomSubset>())
    {
        return true;
    }

    const std::string name = prim.GetName().GetString();
    if (kMaterialScopes.count(name) > 0)
    {
        return true;
    }

    for (const auto& prefix : kTexturePrefixes)
    {
        if (name.rfind(prefix, 0) == 0)
        {
            return true;
        }
    }

    return false;
}


// True if the prim authors any references or payloads. Such prims are
// excluded from the duplicate set in internal-reference mode (the script
// does the same — see `_has_references_or_payloads`).
static bool _hasReferencesOrPayloads(const UsdPrim& prim)
{
    return prim.HasAuthoredReferences() || prim.HasAuthoredPayloads();
}


// FNV-1a-64 constants and per-field mixer used by `_structuralHash`.
constexpr uint64_t kFnvOffset = 0xcbf29ce484222325ull;
constexpr uint64_t kFnvPrime = 0x100000001b3ull;

static uint64_t _fnvMix(uint64_t hash, const std::string& s)
{
    for (unsigned char c : s)
    {
        hash ^= c;
        hash *= kFnvPrime;
    }
    // Domain separator between fields: prevents two adjacent fields from
    // colliding with one longer field of the same byte sequence.
    hash ^= 0xff;
    hash *= kFnvPrime;
    return hash;
}


// Structural hash of a subtree. Walks the subtree (depth-first via UsdPrimRange)
// and accumulates an FNV-1a-64 hash over, per descendant:
//   - the descendant's path relative to `root`
//   - the descendant's type name
//   - the descendant's authored property names (sorted, so the hash is
//     order-independent)
//
// Excludes attribute *values* and mesh data by design: pointwise mesh
// equality is `deduplicateGeometry`'s responsibility, and including
// root transforms here would prevent matching duplicates that differ
// only in placement — which is the entire point of instancing.
//
// Returns a 16-character lowercase hex string. Two subtrees produce the
// same string iff their (shape, types, authored property names) match.
static std::string _structuralHash(const UsdPrim& root)
{
    uint64_t hash = kFnvOffset;
    const SdfPath rootPath = root.GetPath();

    for (const UsdPrim& descendant : UsdPrimRange(root))
    {
        const SdfPath relPath = descendant.GetPath().MakeRelativePath(rootPath);
        hash = _fnvMix(hash, relPath.GetAsString());
        hash = _fnvMix(hash, descendant.GetTypeName().GetString());

        std::vector<std::string> propNames;
        for (const TfToken& name : descendant.GetAuthoredPropertyNames())
        {
            propNames.push_back(name.GetString());
        }
        std::sort(propNames.begin(), propNames.end());
        for (const std::string& name : propNames)
        {
            hash = _fnvMix(hash, name);
        }
        // Domain separator between prims.
        hash ^= 0xee;
        hash *= kFnvPrime;
    }

    char buf[17];
    std::snprintf(buf, sizeof(buf), "%016" PRIx64, static_cast<uint64_t>(hash));
    return std::string(buf);
}


// Returns true if a property name is a transform-related attribute that should
// be excluded from value comparison. Instances are expected to differ in
// placement, so xformOp values and xformOpOrder are not meaningful signals.
static bool _isXformProperty(const TfToken& name)
{
    return (UsdGeomXformOp::IsXformOp(name) || name == UsdGeomTokens->xformOpOrder);
}

// Compare two VtValues, applying tolerance for floating-point types
// (VtArray<float/double/GfVec2-4f/d> and scalar float/double). All other
// types — integers, strings, tokens, etc. — always require exact match
// regardless of tolerance. Forwards to the shared `isClose` family in
// Utils.h, which performs all arithmetic in double precision.
static bool _valuesEqual(const VtValue& valA, const VtValue& valB, double tolerance)
{
    if (tolerance <= 0.0)
    {
        return valA == valB;
    }

    auto tryClose = [&](auto sample) -> std::optional<bool>
    {
        using T = decltype(sample);
        if (valA.IsHolding<T>() && valB.IsHolding<T>())
        {
            return isClose(valA.UncheckedGet<T>(), valB.UncheckedGet<T>(), tolerance);
        }
        return std::nullopt;
    };

#define SO_TRY_CLOSE(T)                                                                                                \
    if (auto r = tryClose(T{}))                                                                                        \
        return *r;
    SO_TRY_CLOSE(VtFloatArray)
    SO_TRY_CLOSE(VtDoubleArray)
    SO_TRY_CLOSE(VtVec2fArray)
    SO_TRY_CLOSE(VtVec2dArray)
    SO_TRY_CLOSE(VtVec3fArray)
    SO_TRY_CLOSE(VtVec3dArray)
    SO_TRY_CLOSE(VtVec4fArray)
    SO_TRY_CLOSE(VtVec4dArray)
    SO_TRY_CLOSE(float)
    SO_TRY_CLOSE(double)
#undef SO_TRY_CLOSE

    // All other types: exact match
    return valA == valB;
}


// Recursively compare authored property values between two subtrees. Returns
// true if the subtrees are value-equivalent. xformOp properties are only
// skipped on the root prims (whose placement is expected to differ between
// instances); descendant transforms must match exactly or the internal layout
// of the subtree would change after instancing.
// Float/vec arrays are compared within the given tolerance; everything else
// requires exact match.
static bool _subtreeValuesEqual(const UsdPrim& rootA, const UsdPrim& rootB, double tolerance, bool ignoreShaderOutputs)
{
    auto rangeA = UsdPrimRange(rootA);
    auto rangeB = UsdPrimRange(rootB);

    auto itA = rangeA.begin();
    auto itB = rangeB.begin();

    while (itA != rangeA.end() && itB != rangeB.end())
    {
        const UsdPrim& primA = *itA;
        const UsdPrim& primB = *itB;
        const bool isRoot = (primA == rootA);

        auto shouldSkip = [&](const UsdAttribute& attr) {
            return (isRoot && _isXformProperty(attr.GetName())) ||
                   (ignoreShaderOutputs && UsdShadeOutput::IsOutput(attr));
        };

        // Pass 1: every authored attr on A must be authored on B with an equal value.
        std::set<TfToken> seenOnA;
        for (const UsdAttribute& attrA : primA.GetAuthoredAttributes())
        {
            if (shouldSkip(attrA))
            {
                continue;
            }
            const TfToken& name = attrA.GetName();
            seenOnA.insert(name);

            const UsdAttribute attrB = primB.GetAttribute(name);
            if (!attrB || !attrB.HasAuthoredValue())
            {
                return false;
            }

            VtValue valA, valB;
            attrA.Get(&valA);
            attrB.Get(&valB);
            if (!_valuesEqual(valA, valB, tolerance))
            {
                return false;
            }
        }

        // Pass 2: catch attrs authored on B but not on A — pass 1 already
        // covered the intersection, so we only need a name-membership check.
        for (const UsdAttribute& attrB : primB.GetAuthoredAttributes())
        {
            if (shouldSkip(attrB))
            {
                continue;
            }
            if (!seenOnA.count(attrB.GetName()))
            {
                return false;
            }
        }

        ++itA;
        ++itB;
    }

    return (itA == rangeA.end() && itB == rangeB.end());
}


// Given a hierarchy map produced by the BFS grouping pass, refine each group
// by comparing property values. Members whose subtree values differ from the
// prototype (beyond tolerance for float types) are removed from the group.
// Returns the refined map (groups with fewer than 1 duplicate are dropped).
static HierarchyMap _refineByValues(const HierarchyMap& candidates,
                                    const UsdStageWeakPtr& stage,
                                    double tolerance,
                                    bool ignoreShaderOutputs,
                                    bool verbose)
{
    HierarchyMap refined;
    size_t totalDropped = 0;

    for (const auto& [prototype, duplicates] : candidates)
    {
        const UsdPrim protoPrim = stage->GetPrimAtPath(prototype);
        if (!protoPrim || !protoPrim.IsValid())
        {
            continue;
        }

        SdfPathVector confirmed;
        confirmed.reserve(duplicates.size());

        for (const SdfPath& dupPath : duplicates)
        {
            const UsdPrim dupPrim = stage->GetPrimAtPath(dupPath);
            if (!dupPrim || !dupPrim.IsValid())
            {
                continue;
            }

            if (_subtreeValuesEqual(protoPrim, dupPrim, tolerance, ignoreShaderOutputs))
            {
                confirmed.push_back(dupPath);
            }
            else
            {
                ++totalDropped;
            }
        }

        if (!confirmed.empty())
        {
            refined[prototype] = std::move(confirmed);
        }
    }

    if (totalDropped > 0 && verbose)
    {
        SO_LOG_INFO("Value refinement: %zu candidate(s) dropped due to differing property values.", totalDropped);
    }

    return refined;
}


// Walk one BFS level and merge any newly discovered duplicate groups into
// `outDuplicates`. Returns the next level's prims (children of any
// non-matched prim from this level, with material scopes filtered out).
static PrimVector _processLevel(const PrimVector& currentLevel, HierarchyMap& outDuplicates, bool verbose)
{
    // Group prims by their structural hash. The hash always returns a
    // non-empty hex string, so every non-material-related prim gets keyed.
    std::unordered_map<std::string, PrimVector> groups;

    for (const UsdPrim& prim : currentLevel)
    {
        if (_isMaterialRelated(prim))
        {
            continue;
        }
        groups[_structuralHash(prim)].push_back(prim);
    }

    // For each group of 2+ prims: mark all matched, filter ones that already
    // carry refs/payloads, and record the (prototype -> duplicates) pair.
    SdfPathSet matched;
    for (auto& [key, group] : groups)
    {
        if (group.size() < 2)
        {
            continue;
        }
        for (const UsdPrim& p : group)
        {
            matched.insert(p.GetPath());
        }

        PrimVector valid;
        valid.reserve(group.size());
        for (const UsdPrim& p : group)
        {
            if (!_hasReferencesOrPayloads(p))
            {
                valid.push_back(p);
            }
        }
        if (valid.size() < 2)
        {
            continue;
        }

        const SdfPath prototype = valid.front().GetPath();
        SdfPathVector& duplicates = outDuplicates[prototype];
        duplicates.reserve(valid.size() - 1);
        for (size_t i = 1; i < valid.size(); ++i)
        {
            duplicates.push_back(valid[i].GetPath());
        }

        if (verbose)
        {
            SO_LOG_VERBOSE("Duplicate group '%s': prototype=%s, duplicates=%zu",
                           key.c_str(),
                           prototype.GetAsString().c_str(),
                           duplicates.size());
        }
    }

    // Build next BFS level from children of unmatched prims, skipping
    // material-related prims for the same reason we skip them at the
    // per-prim filter.
    PrimVector nextLevel;
    for (const UsdPrim& prim : currentLevel)
    {
        if (matched.count(prim.GetPath()) > 0)
        {
            continue;
        }
        if (_isMaterialRelated(prim))
        {
            continue;
        }
        for (const UsdPrim& child : prim.GetChildren())
        {
            nextLevel.push_back(child);
        }
    }
    return nextLevel;
}


// Collect the BFS starting prims. If user-supplied paths are given, those
// are the roots; otherwise we start from the children of the default prim
// (matching the Python processor's default behaviour).
static PrimVector _resolveStartingPrims(const UsdStageWeakPtr& stage, const std::vector<std::string>& paths)
{
    PrimVector starting;
    if (!paths.empty())
    {
        for (const std::string& s : paths)
        {
            const SdfPath path(s);
            if (!path.IsAbsolutePath() || !path.IsPrimPath())
            {
                SO_LOG_WARN("Skipping non-absolute prim path: %s", s.c_str());
                continue;
            }
            UsdPrim prim = stage->GetPrimAtPath(path);
            if (!prim || !prim.IsValid())
            {
                SO_LOG_WARN("Path not found on stage: %s", s.c_str());
                continue;
            }
            // The user-supplied paths are *subtree roots* — the BFS starts
            // at their children (the same level the default prim's children
            // sit at when no paths are given).
            for (const UsdPrim& child : prim.GetChildren())
            {
                starting.push_back(child);
            }
        }
        return starting;
    }

    UsdPrim defaultPrim = stage->GetDefaultPrim();
    if (!defaultPrim || !defaultPrim.IsValid())
    {
        // Not an error: a stage without a default prim is legal USD, and
        // callers may legitimately invoke this operation against a global
        // pipeline that doesn't always set one. The result is a safe no-op —
        // we surface a warning so the caller can decide whether they meant
        // to provide `paths`, but the operation succeeds.
        SO_LOG_WARN(
            "Stage has no default prim; nothing to deduplicate. Provide `paths` to restrict to a subtree if this is unexpected.");
        return starting;
    }
    for (const UsdPrim& child : defaultPrim.GetChildren())
    {
        starting.push_back(child);
    }
    return starting;
}


// Replace each duplicate with an instanceable internal reference to the
// prototype. Children of the duplicate are deleted first (mirroring the
// Python processor and OrganizePrototypes' `_convertProtoToInstance`).
static bool _applyInternalReferences(const UsdStageWeakPtr& stage, const HierarchyMap& hierarchies)
{
    bool allOk = true;
    size_t total = 0;
    for (const auto& [_proto, dups] : hierarchies)
    {
        total += dups.size();
    }
    SO_LOG_INFO("Authoring %zu instanceable internal references.", total);

    for (const auto& [prototype, duplicates] : hierarchies)
    {
        for (const SdfPath& dupPath : duplicates)
        {
            UsdPrim dup = stage->GetPrimAtPath(dupPath);
            if (!dup || !dup.IsValid())
            {
                SO_LOG_WARN("Skipping invalid duplicate prim: %s", dupPath.GetAsString().c_str());
                allOk = false;
                continue;
            }

            // Collect children up front because we'll mutate during deletion.
            PrimVector childrenToDelete;
            for (const UsdPrim& child : dup.GetChildren())
            {
                childrenToDelete.push_back(child);
            }
            _deletePrims(stage, childrenToDelete, true /* deactivate fallback */);

            UsdReferences refs = dup.GetReferences();
            refs.ClearReferences();
            if (!refs.AddInternalReference(prototype))
            {
                SO_LOG_WARN("Failed to add internal reference on %s -> %s",
                            dupPath.GetAsString().c_str(),
                            prototype.GetAsString().c_str());
                allOk = false;
                continue;
            }
            dup.SetInstanceable(true);
        }
    }
    return allOk;
}


DeduplicateHierarchiesOperation::DeduplicateHierarchiesOperation()
    : Operation("deduplicateHierarchies",
                "Deduplicate Hierarchies",
                "Find duplicate prim hierarchies and replace duplicates with instanceable "
                "internal references to the first instance. Groups prims by subtree shape "
                "then verifies all authored property values match.")
    , m_paths()
    , m_tolerance(0.001)
    , m_ignoreShaderOutputs(true)
{
    addArgument("paths",
                "Prim Paths",
                kDisplayTypePrimPaths,
                "Optional subtree roots. Empty = walk children of the default prim.",
                m_paths);

    addArgument("tolerance",
                "Tolerance",
                kDisplayTypeFloat,
                "Acceptable difference for floating-point array properties (points, normals, "
                "UVs, etc.) and scalar float/double values when comparing subtrees. "
                "The value is in stage units. All other types — including transforms "
                "(matrices, scalar vectors, quaternions), topology indices, strings, "
                "etc. — always require an exact match. Set to 0 for bitwise-exact comparison.",
                m_tolerance);

    addArgument("ignoreShaderOutputs",
                "Ignore Shader Outputs",
                kDisplayTypeBool,
                "Skip shader output attributes (outputs:surface, outputs:displacement, etc.) "
                "during value comparison. These often differ between material instances even "
                "when the geometry is identical. Enabled by default.",
                m_ignoreShaderOutputs);
}


DeduplicateHierarchiesOperation::~DeduplicateHierarchiesOperation() = default;


std::string DeduplicateHierarchiesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion DeduplicateHierarchiesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string DeduplicateHierarchiesOperation::getCategory() const
{
    return s_categoryDedupHierarchies;
}


std::string DeduplicateHierarchiesOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


bool DeduplicateHierarchiesOperation::getSupportsAnalysis() const
{
    return true;
}


HierarchyMap DeduplicateHierarchiesOperation::_findDuplicates()
{
    const UsdStageWeakPtr stage = getUsdStage();
    if (!stage)
    {
        SO_LOG_ERROR("No USD stage available.");
        return {};
    }

    PrimVector currentLevel = _resolveStartingPrims(stage, m_paths);
    if (currentLevel.empty())
    {
        SO_LOG_INFO("No prims to scan; nothing to deduplicate.");
        return {};
    }

    if (getContext()->verbose)
    {
        SO_LOG_INFO("Grouping by structural hash (subtree shape + types + authored property names).");
    }

    HierarchyMap hierarchies;
    int level = 1;
    while (!currentLevel.empty())
    {
        if (getContext()->verbose)
        {
            SO_LOG_INFO("Scanning level %d (%zu prims)", level, currentLevel.size());
        }
        currentLevel = _processLevel(currentLevel, hierarchies, getContext()->verbose);
        ++level;
    }

    if (hierarchies.empty())
    {
        SO_LOG_INFO("No duplicate hierarchies found.");
        return {};
    }

    hierarchies = _refineByValues(hierarchies, stage, m_tolerance, m_ignoreShaderOutputs, getContext()->verbose);
    if (hierarchies.empty())
    {
        SO_LOG_INFO("No true duplicates remain after value comparison.");
    }

    return hierarchies;
}


OperationResult DeduplicateHierarchiesOperation::executeAnalysisImpl()
{
    HierarchyMap hierarchies = _findDuplicates();

    // Convert to a JSON object: { "analysis": { "proto_path": ["dup1", "dup2", ...], ... } }
    JsObject analysisObj;
    for (const auto& [prototype, duplicates] : hierarchies)
    {
        analysisObj[prototype.GetAsString()] = _toJson(duplicates);
    }

    JsObject resultJson;
    resultJson["analysis"] = std::move(analysisObj);

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    return result;
}


OperationResult DeduplicateHierarchiesOperation::executeImpl()
{
    HierarchyMap hierarchies = _findDuplicates();

    if (hierarchies.empty())
    {
        return { true };
    }

    const bool ok = _applyInternalReferences(getUsdStage(), hierarchies);
    return { ok };
}


} // namespace omni::scene::optimizer
