// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "RemoveUnusedUVs.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/ar/resolverScopedCache.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// TBB
#include <tbb/parallel_for.h>

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::RemoveUnusedUVsOperation);

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((primvarsSt, "primvars:st"))
    ((primvarsSt0, "primvars:st0"))
    ((primvarsSt1, "primvars:st1"))
    ((primvarsSt2, "primvars:st2"))
    (UsdPrimvarReader_float2)
    (UsdUVTexture)
    (varname)
);
// LCOV_EXCL_STOP
// clang-format on


namespace omni::scene::optimizer
{


RemoveUnusedUVsOperation::RemoveUnusedUVsOperation()
    : Operation("removeUnusedUVs", "Remove Unused UVs", "Remove unused texture coordinates")
{
    addArgument("paths", "Prim Paths", kDisplayTypePrimPaths, "A list of prim path expressions to consider", m_primPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("mode", "Mode", kDisplayTypeEnum, "What to do with unused attributes", m_mode)
        .setEnumValues<Mode>({
            { Mode::eRemove, "Remove" },
            { Mode::eBlock, "Block" },
        });

    addArgument("attributes",
                "UV Attributes",
                kDisplayTypeAttributeList,
                "A list of custom UV attribute names to check",
                m_attributes)
        .setPlaceholder("Add attribute");
}


std::string RemoveUnusedUVsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion RemoveUnusedUVsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string RemoveUnusedUVsOperation::getCategory() const
{
    constexpr const char* s_category = "UNUSED_UVS";
    return s_category;
}


std::string RemoveUnusedUVsOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


bool RemoveUnusedUVsOperation::getSupportsAnalysis() const
{
    return true;
}


/// Guess if a material *might* use UVs
///
/// - If a material has a shader that is in \p shaders then assume it does
/// - If a material has any Asset or AssetArray inputs, assume it does
///
/// For any shaders, we also check for float2 primvar readers. Any that
/// are found will have their \p varname populated in \p readers as the
/// material is stating it will read that primvar, so we know we can't
/// remove it.
static bool _mightUseUVs(const UsdShadeMaterial& material, const std::set<TfToken>& shaders, std::set<TfToken>& readers)
{
    // Map of types that might indicate a texture file
    static std::unordered_set<SdfValueTypeName, SdfValueTypeNameHashFunctor> textureTypeNames = {
        SdfValueTypeNames->Asset,
        SdfValueTypeNames->AssetArray
    };

    bool mightUse = false;

    Usd_PrimFlagsPredicate predicate = UsdPrimAllPrimsPredicate;

    auto primRange = UsdPrimRange(material.GetPrim(), predicate);
    for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
    {
        UsdShadeShader shader(*iter);
        if (!shader)
        {
            continue;
        }

        TfToken shaderId;
        shader.GetIdAttr().Get(&shaderId);

        // Certain shaders, for example UsdUVTexture, we can assume will read a texture
        // and therefore authored UVs would be required.
        auto findShaderIt = shaders.find(shaderId);
        if (findShaderIt != shaders.end())
        {
            return true;
        }

        // If the shader is a float2 primvar reader then get its varname
        // and populate that in our output arg.
        //
        // Note: we don't return true here - we don't know what attribute
        // triggered us being here, so the calling code can check that later
        // against whichever attributes it likes.
        if (shaderId == _tokens->UsdPrimvarReader_float2)
        {
            std::string varname;
            shader.GetInput(_tokens->varname).Get(&varname);
            if (!varname.empty())
            {
                readers.insert(TfToken(varname));
            }
        }

        // The last check we do is seeing if there is an asset typed input.
        // If so, currently we just assume this might be a texture and therefore
        // UVs would be required.
        for (const auto& input : shader.GetInputs())
        {
            const SdfValueTypeName& typeName = input.GetAttr().GetTypeName();
            if (textureTypeNames.find(typeName) != textureTypeNames.end())
            {
                mightUse = true;
                break;
            }
        }
    }

    return mightUse;
}


OperationResult RemoveUnusedUVsOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|RemoveUnusedUVs|Execute");

    // Default list of possible texture coordinate names
    // Can be extended via the "attributes" argument
    std::set<TfToken> attributeNames = { _tokens->primvarsSt,
                                         _tokens->primvarsSt0,
                                         _tokens->primvarsSt1,
                                         _tokens->primvarsSt2 };

    // Default list of shaders to assume use UVs
    // Note: there is not currently an argument to extend this
    std::set<TfToken> shaders = { _tokens->UsdUVTexture };

    // Append any custom UV names provided via args
    // Makes the assumption they will be primvars
    for (const auto& value : m_attributes)
    {
        if (SdfPath::IsValidNamespacedIdentifier(value))
        {
            attributeNames.insert(TfToken(value));
        }
    }

    // Expression callback to allow pruning parts of the hierarchy
    auto callback = [](const UsdPrim& prim, UsdPrimRange::iterator& iterator) -> bool
    {
        // Prune materials, don't need to check them
        if (prim.IsA<UsdShadeMaterial>())
        {
            iterator.PruneChildren();
            return false;
        }

        return true;
    };

    // Resolve prims
    constexpr bool meshesOnly = false;
    constexpr bool reverse = false;
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(),
                                                                   m_primPaths,
                                                                   meshesOnly,
                                                                   reverse,
                                                                   UsdPrimAllPrimsPredicate,
                                                                   callback);

    // Vector of attributes found for removal.
    std::vector<UsdAttribute> toRemove;
    std::mutex toRemoveMutex;

    // Caches
    ArResolverScopedCache parentResolverScopedCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    tbb::parallel_for(tbb::blocked_range<size_t>(0, prims.size()),
                      [&](const tbb::blocked_range<size_t>& range)
                      {
                          // Per-thread attributes to remove.
                          std::vector<UsdAttribute> _toRemove;

                          // Per-thread resolver cache
                          ArResolverScopedCache _resolverCache(&parentResolverScopedCache);

                          for (size_t i = range.begin(); i < range.end(); ++i)
                          {
                              const auto& prim = prims[i];

                              bool mightUseUVs = false;
                              bool checkedMaterial = false;
                              std::set<TfToken> primvarReaders;

                              UsdGeomPrimvarsAPI primvarsAPI(prim);

                              for (const auto& attributeName : attributeNames)
                              {
                                  const UsdGeomPrimvar& primvar = primvarsAPI.GetPrimvar(attributeName);
                                  if (primvar && primvar.HasAuthoredValue())
                                  {
                                      // First, check if there is a bound material for this prim if we have not done so.
                                      if (!checkedMaterial)
                                      {
                                          UsdShadeMaterialBindingAPI bindingAPI(prim);
                                          UsdShadeMaterial boundMaterial =
                                              bindingAPI.ComputeBoundMaterial(&bindingsCache, &collQueryCache);

                                          if (boundMaterial)
                                          {
                                              mightUseUVs = _mightUseUVs(boundMaterial, shaders, primvarReaders);
                                          }

                                          checkedMaterial = true;
                                      }

                                      // Material may use UVs, so we can't remove.
                                      // This will remain true for any other attribute on this prim, so just break.
                                      if (mightUseUVs)
                                      {
                                          break;
                                      }

                                      // If there are any primvar readers, compare to this attribute name.
                                      // This is a direct indication that a specific primvar is required.
                                      if (primvarReaders.find(primvar.GetPrimvarName()) != primvarReaders.end())
                                      {
                                          continue;
                                      }

                                      // Otherwise, this is seemingly unused.
                                      _toRemove.push_back(primvar.GetAttr());

                                      // Also remove any indices
                                      if (primvar.IsIndexed())
                                      {
                                          _toRemove.push_back(primvar.GetIndicesAttr());
                                      }
                                  }
                              }
                          }

                          // Append to overall results
                          if (!_toRemove.empty())
                          {
                              std::lock_guard guard(toRemoveMutex);
                              toRemove.insert(toRemove.end(), _toRemove.begin(), _toRemove.end());
                          }
                      });

    // Handle analysis mode first - prepare payload then return.
    if (getContext()->analysisMode)
    {
        std::string suffix = toRemove.size() == 1 ? "" : "s";
        SO_LOG_INFO("Found %lu unused UV attribute%s", toRemove.size(), suffix.c_str());

        // Append payload - regardless of whether anything was found.
        JsObject resultJson;
        resultJson["analysis"] = _toJson(toRemove);

        OperationResult result{ true };
        result.output = getCStr(JsWriteToString(resultJson));

        return result;
    }

    // Process anything that matched
    if (!toRemove.empty())
    {
        // Log output
        std::string operation = m_mode == Mode::eRemove ? "Removing" : "Blocking";
        std::string suffix = toRemove.size() == 1 ? "" : "s";
        SO_LOG_INFO("%s %lu unused UV attribute%s...", operation.c_str(), toRemove.size(), suffix.c_str());

        SdfChangeBlock _changeBlock;

        for (const auto& attribute : toRemove)
        {
            if (getContext()->verbose || getContext()->generateReport)
            {
                SO_LOG_INFO("%s %s", operation.c_str(), attribute.GetPath().GetAsString().c_str());
            }

            switch (m_mode)
            {
            case Mode::eRemove:
                attribute.GetPrim().RemoveProperty(attribute.GetName());
                break;
            case Mode::eBlock:
                attribute.Block();
                break;
            default:
                break;
            }
        }
    }
    else
    {
        SO_LOG_INFO("Found no unused UV attributes");
    }

    return { true };
}


OperationResult RemoveUnusedUVsOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|RemoveUnusedUVsOperation|ExecuteAnalysis");

    auto result = executeImpl();

    if (getContext()->verbose)
    {
        SO_LOG_INFO("Analysis result: %s", result.output);
    }

    return result;
}


} // namespace omni::scene::optimizer
