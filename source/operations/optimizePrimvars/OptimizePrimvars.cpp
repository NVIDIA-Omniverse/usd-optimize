// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "OptimizePrimvars.h"

// Scene Optimizer
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/TbbCompat.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usdGeom/primvarsAPI.h>

// C++
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin with SO
SO_PLUGIN_INIT(omni::scene::optimizer::OptimizePrimvarsOperation);

namespace omni::scene::optimizer
{

constexpr const char* s_category = "OPTIMIZE_PRIMVARS";


OptimizePrimvarsOperation::OptimizePrimvarsOperation()
    : Operation("optimizePrimvars",
                "Optimize Primvars",
                "This operation provides utilities for optimizing primvars - simplifying their data (eg converting "
                "faceVarying to uniform if possible), and indexing or flattening.")
{

    addArgument("paths", "Prim Paths", kDisplayTypePrimPaths, "A list of prim paths to consider", m_primPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("primvars",
                "Primvar Names",
                kDisplayTypeTextList,
                "Optional comma-separated list of primvars to consider",
                m_primvars)
        .setPlaceholder("Primvar names");

    addArgument("mode", "Mode", kDisplayTypeEnum, "What to do with any matching primvars", m_mode)
        .setEnumValues<OptimizePrimvarsMode>({
            { OptimizePrimvarsMode::eIgnore, "Ignore" },
            { OptimizePrimvarsMode::eIndex, "Index" },
            { OptimizePrimvarsMode::eIndexForced, "Index (Forced)" },
            { OptimizePrimvarsMode::eFlatten, "Flatten" },
            { OptimizePrimvarsMode::eRemove, "Remove" },
        });

    addArgument(
        "simplify",
        "Simplify",
        kDisplayTypeBool,
        "If possible, find a simpler representation of a primvar (e.g. convert uniform to constant if all values match)",
        m_simplify);

    addArgument("removeIfBound",
                "Only Remove If Bound",
                kDisplayTypeBool,
                "Only remove primvars if their prim has a material bound",
                m_removeIfBound)
        .setVisibleIf("mode == 4");

    // Debug argument as an optimization
    // This expects the full path to an attribute (primvar) and means we can avoid a stage iteration,
    // so for example can be used by the validation extension to avoid iterating the stage many times
    addArgument("primvarPaths", "Primvar Paths", kDisplayTypeTextList, "Explicit full primvar paths to target", m_primvarPaths)
        .setVisible(false);
}


std::string OptimizePrimvarsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion OptimizePrimvarsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string OptimizePrimvarsOperation::getCategory() const
{
    return s_category;
}


std::string OptimizePrimvarsOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


bool OptimizePrimvarsOperation::getSupportsAnalysis() const
{
    return true;
}


void OptimizePrimvarsOperation::setPrimvars(const std::vector<std::string>& primvars)
{
    m_primvarTokens.clear();

    // Convert to tokens for faster checks during iteration
    for (const auto& primvar : primvars)
    {
        m_primvarTokens.insert(TfToken(primvar));
    }
}


template <typename T>
static void _indexPrimvar(const UsdGeomPrimvar& primvar, const VtValue& values)
{

    const auto& _values = values.UncheckedGet<VtArray<T>>();

    VtArray<T> indexedValues;
    VtIntArray indices;
    indices.reserve(_values.size());
    std::unordered_map<size_t, int> indexMap;

    for (const auto& value : _values)
    {
        auto insertIt = indexMap.insert(std::make_pair(VtHashValue(value), indexedValues.size()));

        // If the insert succeeded it is a new value
        if (insertIt.second)
        {
            indexedValues.push_back(value);
        }

        indices.push_back(insertIt.first->second);
    }

    primvar.Set(indexedValues);
    primvar.SetIndices(indices);
}


/// Process the values of a primvar and attempt to simplify the data.
///
/// Checks whether the primvar data can be reduced, e.g. from faceVarying to uniform or even constant,
/// if all the values happen to be equal.
///
/// If a simplification could be made then \p values is updated with the new, simplified data, and the
/// new interpolation is returned. If nothing could be done then UsdGeomTokens->none is returned.
template <typename T>
static TfToken _simplifyPrimvar(UsdGeomPrimvar& primvar, const VtIntArray& faceVertexCounts, VtValue& values)
{
    const auto& _values = values.UncheckedGet<VtArray<T>>();
    const TfToken& interpolation = primvar.GetInterpolation();

    // Handle faceVarying to either uniform or constant
    if (interpolation == UsdGeomTokens->faceVarying)
    {
        bool allValuesEqual = true;
        bool allFacesEqual = true;
        size_t faceOffset = 0;

        for (int faceCount : faceVertexCounts)
        {
            for (int i = 0; i < faceCount; ++i)
            {
                size_t index = faceOffset + i;

                if (index >= _values.size())
                {
                    return UsdGeomTokens->none; // LCOV_EXCL_LINE
                }

                const T& value = _values[index];

                // Check whether all values are equal
                if (allValuesEqual && value != _values[0])
                {
                    allValuesEqual = false;
                }

                // Check whether all values within the face are equal
                if (allFacesEqual && value != _values[faceOffset])
                {
                    allFacesEqual = false;
                    break;
                }
            }

            // Early out if there's nothing we can reduce
            if (!allValuesEqual && !allFacesEqual)
            {
                return UsdGeomTokens->none;
            }

            faceOffset += faceCount;
        }

        // Either all values or all values within each face are equal, we can reduce this
        // primvar
        if (allValuesEqual)
        {
            VtArray<T> newValues = { _values[0] };
            values = VtValue::Take(newValues);
            return UsdGeomTokens->constant;
        }
        else if (allFacesEqual)
        {
            VtArray<T> newValues;
            newValues.reserve(faceVertexCounts.size());

            size_t offset = 0;
            for (int faceCount : faceVertexCounts)
            {
                newValues.push_back(_values[offset]);
                offset += faceCount;
            }

            values = VtValue::Take(newValues);
            return UsdGeomTokens->uniform;
        }
    }
    else
    {
        // For anything not faceVarying just check each value. If they all match
        // then it can be reduced to constant.
        for (const auto& value : _values)
        {
            if (value != _values[0])
            {
                return UsdGeomTokens->none;
            }
        }

        VtArray<T> newValues = { _values[0] };
        values = VtValue::Take(newValues);
        return UsdGeomTokens->constant;
    }

    // We should have already aborted and returned none at this point.
    return UsdGeomTokens->none; // LCOV_EXCL_LINE
}


TfToken OptimizePrimvarsOperation::simplifyPrimvar(UsdGeomPrimvar& primvar,
                                                   const VtIntArray& faceVertexCounts,
                                                   VtValue& value)
{

    TfToken result = UsdGeomTokens->none;

    if (value.IsHolding<VtFloatArray>())
    {
        result = _simplifyPrimvar<float>(primvar, faceVertexCounts, value);
    }
    else if (value.IsHolding<VtVec2fArray>())
    {
        result = _simplifyPrimvar<GfVec2f>(primvar, faceVertexCounts, value);
    }
    else if (value.IsHolding<VtVec3fArray>())
    {
        result = _simplifyPrimvar<GfVec3f>(primvar, faceVertexCounts, value);
    }
    else if (value.IsHolding<VtStringArray>())
    {
        result = _simplifyPrimvar<std::string>(primvar, faceVertexCounts, value);
    }
    else
    {
        SO_LOG_WARN("Unsupported value type for %s: %s",
                    primvar.GetAttr().GetPath().GetAsString().c_str(),
                    value.GetTypeName().c_str());
    }

    if (getContext()->verbose && result != UsdGeomTokens->none)
    {
        SO_LOG_VERBOSE("Reduced %s to %s", primvar.GetAttr().GetPath().GetAsString().c_str(), result.GetString().c_str());
    }

    return result;
}


/// Set the new reduced/simplified values on a primvar.
///
/// This function is used if a primvar was not converted between indexed and flattened as those code paths
/// will set the values - so it is intended to maintain the existing indexing state.
//
// Note we do not set the new interpolation here, that was already done if the reduce did something
// earlier.
static void _setSimplifiedValues(UsdGeomPrimvar& primvar, const VtValue& values, const TfToken& interpolation)
{
    if (primvar.IsIndexed())
    {
        // For constant we need to block the indices and just set the new value
        if (interpolation == UsdGeomTokens->constant)
        {
            primvar.Set(values);
            primvar.BlockIndices();
        }
        else
        {
            // Index and set the flat reduced values
            if (values.IsHolding<VtArray<float>>())
            {
                _indexPrimvar<float>(primvar, values);
            }
            else if (values.IsHolding<VtVec2fArray>())
            {
                _indexPrimvar<GfVec2f>(primvar, values);
            }
            else if (values.IsHolding<VtVec3fArray>())
            {
                _indexPrimvar<GfVec3f>(primvar, values);
            }
        }
    }
    else
    {
        // Setting flat values, so we can just set them directly.
        primvar.Set(values);
    }
}


// Set of simple type arrays that do not use more memory than being indexed
static std::set<TfToken> s_simpleTypes = {
    SdfValueTypeNames->BoolArray.GetAsToken(),  SdfValueTypeNames->UCharArray.GetAsToken(),
    SdfValueTypeNames->IntArray.GetAsToken(),   SdfValueTypeNames->UIntArray.GetAsToken(),
    SdfValueTypeNames->Int64Array.GetAsToken(), SdfValueTypeNames->UInt64Array.GetAsToken(),
};

/// Helper struct for tracking primvars with issues
struct PrimvarIssue
{
    std::vector<UsdGeomPrimvar> primvars;
    std::mutex mutex;

    void addIssue(const UsdGeomPrimvar& primvar)
    {
        std::lock_guard<std::mutex> lock(mutex);
        primvars.push_back(primvar);
    }
};


/// Collection of primvar issues
struct PrimvarIssues
{
    PrimvarIssue indexable;
    PrimvarIssue outOfBounds;
    PrimvarIssue nonArray;
};


/// Custom comparator to handle operator< for various Gf types
struct Comparator
{
    bool operator()(const std::string& a, const std::string& b) const
    {
        return a < b;
    }

    bool operator()(const TfToken& a, const TfToken& b) const
    {
        return a < b;
    }

    bool operator()(const SdfAssetPath& a, const SdfAssetPath& b) const
    {
        return a < b;
    }

    bool operator()(const UsdTimeCode& a, const UsdTimeCode& b) const
    {
        return a < b;
    }

    template <typename T, typename std::enable_if<GfIsFloatingPoint<T>::value>::type* = nullptr>
    bool operator()(const T& a, const T& b) const
    {
        return a < b;
    }

    template <typename T, typename std::enable_if<GfIsGfVec<T>::value>::type* = nullptr>
    bool operator()(const T& a, const T& b) const
    {
        for (size_t i = 0; i < T::dimension; ++i)
        {
            if (a[i] < b[i])
            {
                return true;
            }
        }

        return false;
    }

    template <typename T, typename std::enable_if<GfIsGfQuat<T>::value>::type* = nullptr>
    bool operator()(const T& a, const T& b) const
    {
        if (a.GetReal() < b.GetReal())
        {
            return true;
        }

        // Call back in to this comparator with the imaginary vector type
        return this->operator()(a.GetImaginary(), b.GetImaginary());
    }

    template <typename T, typename std::enable_if<GfIsGfMatrix<T>::value>::type* = nullptr>
    bool operator()(const T& a, const T& b) const
    {
        for (int i = 0; i < static_cast<int>(T::numRows); ++i)
        {
            for (int j = 0; j < static_cast<int>(T::numColumns); ++j)
            {
                if (a[i][j] < b[i][j])
                {
                    return true;
                }
            }
        }

        return false;
    }
};


// Check whether a primvar could do with being indexed, or for already-indexed primvars, whether any
// of their values are out of bounds.
template <typename T>
static void _analyzePrimvar(const UsdGeomPrimvar& primvar, PrimvarIssues& issues)
{
    // Per AV, EarliestTime is used.
    static constexpr UsdTimeCode timeCode = UsdTimeCode::EarliestTime();

    if (primvar.IsIndexed())
    {
        // Get the values
        VtArray<T> values;
        primvar.Get(&values, timeCode);

        int valueCount = static_cast<int>(values.size());

        VtIntArray indices;
        primvar.GetIndices(&indices, timeCode);

        for (const auto& index : indices)
        {
            if (index >= valueCount)
            {
                // Found an index that is out of bounds - log issue and break
                issues.outOfBounds.addIssue(primvar);
                break;
            }
        }
    }
    else
    {
        VtArray<T> flattened;
        primvar.ComputeFlattened(&flattened, timeCode);

        // Use a set to find unique values.
        // Note we avoid unordered_set with VtHashValue, to avoid cases where values might hash to the
        // same thing.
        std::set<T, Comparator> unique;

        const T* _flattened = flattened.cdata();
        size_t size = flattened.size();
        for (size_t valIndex = 0; valIndex < size; ++valIndex)
        {
            auto insertIt = unique.insert(_flattened[valIndex]);
            if (!insertIt.second)
            {
                // Found a non-unique value - add issue and break, don't need to check any more
                issues.indexable.addIssue(primvar);
                break;
            }
        }
    }
}

// Macros to reduce boilerplate in type-dispatch switches
#define SO_IF_FILTER_TYPE(TYPENAME, T)                                                                                 \
    if (typeName == TYPENAME)                                                                                          \
    {                                                                                                                  \
        _analyzePrimvar<T>(primvar, issues);                                                                           \
    }

#define SO_ELIF_FILTER_TYPE(TYPENAME, T) else SO_IF_FILTER_TYPE(TYPENAME, T)


#define SO_IF_INDEX_PRIMVAR(T)                                                                                         \
    if (values.IsHolding<VtArray<T>>())                                                                                \
    {                                                                                                                  \
        _indexPrimvar<T>(primvar, values);                                                                             \
    }

#define SO_EL_INDEX_PRIMVAR(T) else SO_IF_INDEX_PRIMVAR(T)


struct OptimizePrimvarsOperation::Counters
{
    size_t indexed = 0;
    size_t flattened = 0;
    size_t reduced = 0;
    size_t removed = 0;
};


void OptimizePrimvarsOperation::processPrimvar(PXR_NS::UsdGeomPrimvar& primvar,
                                               bool isBound,
                                               const PXR_NS::VtIntArray& faceVertexCounts,
                                               Counters& counters)
{

    if (m_mode == OptimizePrimvarsMode::eRemove)
    {
        // If removeIfBound is set, then only remove primvars where the prim has a material bound.
        if (m_removeIfBound && !isBound)
        {
            return;
        }

        primvar.GetAttr().Block();
        primvar.BlockIndices();

        ++counters.removed;
        return;
    }

    // Currently unsupported
    if (primvar.GetElementSize() != 1)
    {
        SO_LOG_WARN("Skipping primvar %s with element size %d",
                    primvar.GetAttr().GetPath().GetAsString().c_str(),
                    primvar.GetElementSize());
        return;
    }

    // If we do not need to try and reduce, then we can skip doing anything if the
    // primvar is already flat / indexed, depending on mode.
    if (!m_simplify)
    {
        if (m_mode == OptimizePrimvarsMode::eFlatten && !primvar.IsIndexed())
        {
            return;
        }

        if (m_mode == OptimizePrimvarsMode::eIndex && primvar.IsIndexed())
        {
            return;
        }
    }

    // Otherwise either we need to index, flatten, and/or try to reduce.
    VtValue values;
    primvar.ComputeFlattened(&values);

    // Skip array primvars that have no values
    if (values.IsArrayValued() && values.GetArraySize() == 0)
    {
        SO_LOG_WARN("Primvar has no values: %s", primvar.GetAttr().GetPath().GetAsString().c_str());
        return;
    }

    bool setSimplifiedValues = false;
    TfToken reduceResult = UsdGeomTokens->none;
    const UsdPrim& prim = primvar.GetAttr().GetPrim();

    // First attempt to reduce, if specified.
    // We only need to simplify primvars if they are on a mesh, as inherited primvars can only
    // be constant anyway.
    // If the reduceResult is not none then values will have been updated with the new flattened and
    // reduced values.
    if (m_simplify && prim.IsA<UsdGeomMesh>() && primvar.GetInterpolation() != UsdGeomTokens->constant)
    {

        if (!faceVertexCounts.empty())
        {
            reduceResult = simplifyPrimvar(primvar, faceVertexCounts, values);
        }

        if (reduceResult != UsdGeomTokens->none)
        {
            // Set the new interpolation at this point. This means it is set regardless
            // of how we end up assigning the values (e.g. we might be indexing or flattening
            // later).
            primvar.SetInterpolation(reduceResult);
            ++counters.reduced;
        }
    }

    // Flatten an indexed primvar
    // This will set the reduced values
    if (m_mode == OptimizePrimvarsMode::eFlatten && primvar.IsIndexed())
    {
        primvar.BlockIndices();
        primvar.Set(values);

        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE("Flattened %s", primvar.GetAttr().GetPath().GetAsString().c_str());
        }

        ++counters.flattened;
        setSimplifiedValues = true;
    }
    else if ((m_mode == OptimizePrimvarsMode::eIndex && !primvar.IsIndexed()) ||
             m_mode == OptimizePrimvarsMode::eIndexForced)
    {
        // Don't bother indexing constant primvars
        if (primvar.GetInterpolation() == UsdGeomTokens->constant)
        {
            // If simplified, set now, as we will not do it below.
            if (reduceResult != UsdGeomTokens->none)
            {
                _setSimplifiedValues(primvar, values, reduceResult);
            }

            return;
        }

        // Index based on type
        // Note: the types are based on what is a supported indexable
        // primvar type in primvars.cpp within USD
        SO_IF_INDEX_PRIMVAR(float)
        SO_EL_INDEX_PRIMVAR(double)
        SO_EL_INDEX_PRIMVAR(GfHalf)
        SO_EL_INDEX_PRIMVAR(GfVec2f)
        SO_EL_INDEX_PRIMVAR(GfVec3f)
        SO_EL_INDEX_PRIMVAR(GfVec4f)
        SO_EL_INDEX_PRIMVAR(GfVec2d)
        SO_EL_INDEX_PRIMVAR(GfVec3d)
        SO_EL_INDEX_PRIMVAR(GfVec4d)
        SO_EL_INDEX_PRIMVAR(std::string)
        SO_EL_INDEX_PRIMVAR(GfVec2i)
        SO_EL_INDEX_PRIMVAR(GfVec3i)
        SO_EL_INDEX_PRIMVAR(GfVec4i)
        SO_EL_INDEX_PRIMVAR(GfVec2h)
        SO_EL_INDEX_PRIMVAR(GfVec3h)
        SO_EL_INDEX_PRIMVAR(GfVec4h)
        SO_EL_INDEX_PRIMVAR(GfMatrix3d)
        SO_EL_INDEX_PRIMVAR(GfMatrix4d)
        else
        {
            SO_LOG_WARN("Unsupported value type for %s: %s",
                        primvar.GetAttr().GetPath().GetAsString().c_str(),
                        values.GetTypeName().c_str());
            return;
        }

        ++counters.indexed;
        setSimplifiedValues = true;

        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE("Indexed %s", primvar.GetAttr().GetPath().GetAsString().c_str());
        }
    }

    // If we did not index or flatten this primvar then we set the reduced values
    // at this point. If we did, they would have already been set above.
    if (m_simplify && !setSimplifiedValues && reduceResult != UsdGeomTokens->none)
    {
        _setSimplifiedValues(primvar, values, reduceResult);
    }
}


OperationResult OptimizePrimvarsOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|OptimizePrimvarsOperation|Execute");

    // Convert primvar name strings to Tokens
    setPrimvars(m_primvars);

    // Early-out if we know there is nothing to do.
    if (m_mode == OptimizePrimvarsMode::eIgnore && !m_simplify)
    {
        SO_LOG_INFO("Mode is ignore and simplify is disabled - nothing to do!");
        return { true };
    }

    // Create a set of tasks - basically a prim, and the primvars that will be processed.
    // This happens first so we can either use targeted primvar attributes as an optimziation,
    // or do a traversal (and optionally filter) if required.
    std::map<UsdPrim, std::vector<UsdGeomPrimvar>> tasks;

    const UsdStageWeakPtr& stage = getUsdStage();

    // First check for explicit primvar/attribute paths.
    if (!m_primvarPaths.empty())
    {
        for (const auto& primvarPath : m_primvarPaths)
        {
            const UsdAttribute& attribute = stage->GetAttributeAtPath(SdfPath(primvarPath));
            UsdGeomPrimvar primvar(attribute);
            if (primvar)
            {
                tasks[attribute.GetPrim()].emplace_back(primvar);
            }
        }
    }
    else
    {
        // If no explicit paths, then traverse, finding either all relevant primvars or filtering based on
        // arguments.
        bool meshesOnly = false;
        bool reverse = false;
        const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(getUsdStage(), m_primPaths, meshesOnly, reverse);

        for (const auto& prim : prims)
        {

            UsdGeomPrimvarsAPI primvarsAPI(prim);
            if (!primvarsAPI)
            {
                continue; // LCOV_EXCL_LINE
            }

            // Get any authored primvars
            std::vector<UsdGeomPrimvar> primvars = primvarsAPI.GetPrimvarsWithAuthoredValues();

            for (auto& primvar : primvars)
            {
                // Check primvar filter
                if (!m_primvarTokens.empty())
                {
                    if (m_primvarTokens.find(primvar.GetPrimvarName()) == m_primvarTokens.end())
                    {
                        continue;
                    }
                }

                tasks[prim].emplace_back(primvar);
            }
        }
    }

    std::string suffix = tasks.size() == 1 ? "" : "s";
    SO_LOG_INFO("Running optimize primvars on %s prim%s", std::to_string(tasks.size()).c_str(), suffix.c_str());

    Counters counters;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    // Now we can iterate the prims and relevant primvars as usual
    for (auto& [prim, primvars] : tasks)
    {
        // Can't author to instance proxies
        if (prim.IsInstanceProxy())
        {
            continue;
        }

        // If simplifying, and a mesh, get the faceVertexCounts.
        VtIntArray faceVertexCounts;
        UsdGeomMesh mesh(prim);
        if (m_simplify && mesh)
        {
            mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
        }

        // Check for a material binding.
        // So we can do this once per prim, rather than per primvar.
        bool isBound = false;
        if (m_mode == OptimizePrimvarsMode::eRemove && m_removeIfBound)
        {
            UsdShadeMaterialBindingAPI bindingAPI(prim);
            UsdShadeMaterial boundMaterial = bindingAPI.ComputeBoundMaterial(&bindingsCache, &collQueryCache);
            if (boundMaterial)
            {
                isBound = true;
            }
        }

        // Process the primvars
        for (auto& primvar : primvars)
        {
            processPrimvar(primvar, isBound, faceVertexCounts, counters);
        }
    }

    if (m_mode == OptimizePrimvarsMode::eIndex || m_mode == OptimizePrimvarsMode::eIndexForced)
    {
        suffix = counters.indexed == 1 ? "" : "s";
        SO_LOG_INFO("Indexed %s primvar%s", std::to_string(counters.indexed).c_str(), suffix.c_str());
    }
    else if (m_mode == OptimizePrimvarsMode::eFlatten)
    {
        suffix = counters.flattened == 1 ? "" : "s";
        SO_LOG_INFO("Flattened %s primvar%s", std::to_string(counters.flattened).c_str(), suffix.c_str());
    }
    else if (m_mode == OptimizePrimvarsMode::eRemove)
    {
        suffix = counters.removed == 1 ? "" : "s";
        SO_LOG_INFO("Removed %s primvar%s", std::to_string(counters.removed).c_str(), suffix.c_str());
    }

    if (m_simplify)
    {
        suffix = counters.reduced == 1 ? "" : "s";
        SO_LOG_INFO("Reduced %s primvar%s", std::to_string(counters.reduced).c_str(), suffix.c_str());
    }

    return { true };
}


OperationResult OptimizePrimvarsOperation::executeAnalysisImpl()
{
    PrimvarIssues issues;

    std::vector<UsdPrim> iterPrims = { getUsdStage()->GetPseudoRoot() };
    tbbcompat::parallelForEach(iterPrims.begin(),
                               iterPrims.end(),
                               [&issues](const UsdPrim& prim, tbbcompat::Feeder<UsdPrim>& feeder)
                               {
                                   if (prim.IsInstanceProxy())
                                   {
                                       return;
                                   }

                                   // Queue all children for traversal via parallel_for_each
                                   const auto& children = prim.GetChildren();
                                   for (const auto& child : children)
                                   {
                                       feeder.add(child);
                                   }

                                   UsdGeomPrimvarsAPI primvarsAPI(prim);
                                   if (primvarsAPI)
                                   {
                                       const auto& primvars = primvarsAPI.GetPrimvarsWithAuthoredValues();

                                       for (const auto& primvar : primvars)
                                       {

                                           const auto& typeName = primvar.GetTypeName();

                                           // No need to index or check constant primvars
                                           if (primvar.GetInterpolation() == UsdGeomTokens->constant)
                                           {
                                               continue;
                                           }

                                           // For anything non-constant, the type should be an array
                                           if (!typeName.IsArray())
                                           {
                                               issues.nonArray.addIssue(primvar);
                                               continue;
                                           }

                                           // For simple type arrays, we don't need to check for indexing
                                           // The assumption being they use optimized storage and take up less space
                                           // than larger types, meaning the indexing doesn't add as much value.
                                           auto findTypeIt = s_simpleTypes.find(primvar.GetTypeName().GetAsToken());
                                           if (findTypeIt != s_simpleTypes.end())
                                           {
                                               continue;
                                           }

                                           // OM-123165: usdSkel related primvars cannot be indexed in the same way as a
                                           // regular primvar.
                                           static TfToken _primvarsSkelToken("primvars:skel");
                                           if (primvar.GetNamespace() == _primvarsSkelToken)
                                           {
                                               continue;
                                           }

                                           // Per AssetValidator: "ComputeFlattened in most USD version does not work
                                           // correctly with elementSize > 1"
                                           if (primvar.GetElementSize() > 1)
                                           {
                                               continue;
                                           }

                                           // For anything that made it this far, check for issues
                                           SO_IF_FILTER_TYPE(SdfValueTypeNames->HalfArray, GfHalf)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->FloatArray, float)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->DoubleArray, double)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->StringArray, std::string)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Int2Array, GfVec2i)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Int3Array, GfVec3i)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Int4Array, GfVec4i)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Half2Array, GfVec2h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Half3Array, GfVec3h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Half4Array, GfVec4h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Float2Array, GfVec2f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Float3Array, GfVec3f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Float4Array, GfVec4f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Double2Array, GfVec2d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Double3Array, GfVec3d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Double4Array, GfVec4d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Point3fArray, GfVec3f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Point3dArray, GfVec3d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Vector3hArray, GfVec3h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Vector3fArray, GfVec3f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Vector3dArray, GfVec3d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Normal3hArray, GfVec3h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Normal3fArray, GfVec3f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Normal3dArray, GfVec3d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Color3hArray, GfVec3h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Color3fArray, GfVec3f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Color3dArray, GfVec3d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Color4hArray, GfVec4h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Color4fArray, GfVec4f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Color4dArray, GfVec4d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Matrix3dArray, GfMatrix3d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Matrix4dArray, GfMatrix4d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->Frame4dArray, GfMatrix4d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->TexCoord2hArray, GfVec2h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->TexCoord2fArray, GfVec2f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->TexCoord2dArray, GfVec2d)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->TexCoord3hArray, GfVec3h)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->TexCoord3fArray, GfVec3f)
                                           SO_ELIF_FILTER_TYPE(SdfValueTypeNames->TexCoord3dArray, GfVec3d)
                                       }
                                   }
                               });

    bool log = getContext()->generateReport || getContext()->verbose;

    auto processIssues = [&log](const std::vector<UsdGeomPrimvar>& primvars, const char* message) -> JsArray
    {
        JsArray result;
        result.reserve(primvars.size());

        for (const auto& primvar : primvars)
        {
            const auto& path = primvar.GetAttr().GetPath().GetAsString();
            result.emplace_back(JsValue(path));

            if (log)
            {
                SO_LOG_VERBOSE(message, path.c_str());
            }
        }

        return result;
    };

    // Construct analysis result
    JsObject analysisResult;
    analysisResult["indexable"] = processIssues(issues.indexable.primvars, "Found indexable primvar at %s");
    analysisResult["outOfBounds"] =
        processIssues(issues.outOfBounds.primvars, "Found primvar with out-of-bounds indices at %s");
    analysisResult["nonArray"] = processIssues(issues.nonArray.primvars, "Non-constant primvar is not of array type %s");

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    if (getContext()->verbose)
    {
        SO_LOG_VERBOSE("Analysis Result: %s", result.output);
    }

    return result;
}


} // namespace omni::scene::optimizer
