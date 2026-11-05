// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MeshPreparation.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/Log.h>
#include <usd_optimize/core/Operation.h>

// USD
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>
#include <pxr/usd/usdShade/shader.h>
#include <pxr/usd/usdUtils/stageCache.h>

// C++
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize
{

namespace
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((st, "st"))
    ((st0, "st0"))
    ((st1, "st1"))
    ((st2, "st2"))
    ((normals, "normals"))
    ((displayColor, "displayColor"))
    ((displayOpacity, "displayOpacity"))
    ((primvarsSt, "primvars:st"))
    ((primvarsSt0, "primvars:st0"))
    ((primvarsNormals, "primvars:normals"))
    ((UsdPrimvarReader_float2, "UsdPrimvarReader_float2"))
    ((varname, "varname"))
);
// LCOV_EXCL_STOP
// clang-format on

constexpr float s_fillDisplayColor = 0.18f;
constexpr float s_fillDisplayOpacity = 1.0f;

/// A mesh and its authored primvars while preparing a group.
struct MeshInfo
{
    UsdPrim prim;
    std::map<TfToken, UsdGeomPrimvar> primvars; // keyed by primvar name without the "primvars:" namespace
    bool uvsGenerated = false;
};


using Tally = MeshPreparationResult;


/// "1 mesh" or "N meshes", for log messages.
std::string _meshCount(size_t count)
{
    return std::to_string(count) + (count == 1 ? " mesh" : " meshes");
}


/// Collect the primvars that carry an authored value directly on the mesh.
MeshInfo _collectMeshInfo(const UsdPrim& prim)
{
    MeshInfo info;
    info.prim = prim;
    for (const UsdGeomPrimvar& primvar : UsdGeomPrimvarsAPI(prim).GetPrimvarsWithAuthoredValues())
    {
        info.primvars.emplace(primvar.GetPrimvarName(), primvar);
    }
    return info;
}


/// True if every opinion on the attribute is in the layer, so removing it there removes it from the stage.
bool _onlyInLayer(const UsdAttribute& attr, const SdfLayerHandle& layer)
{
    const SdfPropertySpecHandleVector stack = attr.GetPropertyStack(UsdTimeCode::Default());
    return !stack.empty() && std::all_of(stack.begin(),
                                         stack.end(),
                                         /* Accept only opinions in the editable layer. */
                                         [&](const SdfPropertySpecHandle& spec) { return spec->GetLayer() == layer; });
}


/// The neutral one element fill for a missing primvar, or empty if merge cannot combine the type.
VtValue _makeFillValue(const TfToken& name, const SdfValueTypeName& typeName)
{
    const TfType type = typeName.GetType();
    if (name == _tokens->displayColor && type == TfType::Find<VtArray<GfVec3f>>())
    {
        return VtValue(VtArray<GfVec3f>{ GfVec3f(s_fillDisplayColor) });
    }
    if (name == _tokens->displayOpacity && type == TfType::Find<VtArray<float>>())
    {
        return VtValue(VtArray<float>{ s_fillDisplayOpacity });
    }
    if (type == TfType::Find<VtArray<bool>>())
    {
        return VtValue(VtArray<bool>{ false });
    }
    if (type == TfType::Find<VtArray<int>>())
    {
        return VtValue(VtArray<int>{ 0 });
    }
    if (type == TfType::Find<VtArray<float>>())
    {
        return VtValue(VtArray<float>{ 0.0f });
    }
    if (type == TfType::Find<VtArray<TfToken>>())
    {
        return VtValue(VtArray<TfToken>{ TfToken() });
    }
    if (type == TfType::Find<VtArray<std::string>>())
    {
        return VtValue(VtArray<std::string>{ std::string() });
    }
    if (type == TfType::Find<VtArray<GfVec2f>>())
    {
        return VtValue(VtArray<GfVec2f>{ GfVec2f(0.0f) });
    }
    if (type == TfType::Find<VtArray<GfVec3f>>())
    {
        return VtValue(VtArray<GfVec3f>{ GfVec3f(0.0f) });
    }
    return VtValue();
}


/// Split a comma separated list of numbers. Returns false if any item is not a number a float can hold.
bool _parseNumbers(const std::string& text, std::vector<double>& numbers)
{
    for (const std::string& item : TfStringSplit(text, ","))
    {
        char* end = nullptr;
        const std::string trimmed = TfStringTrim(item);
        const double value = std::strtod(trimmed.c_str(), &end);
        // The range test is false for nan, so it also rejects nan and inf.
        if (trimmed.empty() || *end != '\0' || !(std::abs(value) <= std::numeric_limits<float>::max()))
        {
            return false;
        }
        numbers.push_back(value);
    }
    return true;
}


/// The one element fill parsed from a caller's default, or empty if it does not fit the type.
VtValue _parseFillValue(const SdfValueTypeName& typeName, const std::string& text)
{
    const TfType type = typeName.GetType();
    std::vector<double> numbers;
    if (type == TfType::Find<VtArray<TfToken>>())
    {
        return VtValue(VtArray<TfToken>{ TfToken(text) });
    }
    if (type == TfType::Find<VtArray<std::string>>())
    {
        return VtValue(VtArray<std::string>{ text });
    }
    if (type == TfType::Find<VtArray<bool>>())
    {
        if (text == "true" || text == "1")
        {
            return VtValue(VtArray<bool>{ true });
        }
        if (text == "false" || text == "0")
        {
            return VtValue(VtArray<bool>{ false });
        }
        return VtValue();
    }
    if (!_parseNumbers(text, numbers))
    {
        return VtValue();
    }
    if (type == TfType::Find<VtArray<int>>() && numbers.size() == 1 && numbers[0] == std::floor(numbers[0]) &&
        numbers[0] >= std::numeric_limits<int>::min() && numbers[0] <= std::numeric_limits<int>::max())
    {
        return VtValue(VtArray<int>{ static_cast<int>(numbers[0]) });
    }
    if (type == TfType::Find<VtArray<float>>() && numbers.size() == 1)
    {
        return VtValue(VtArray<float>{ static_cast<float>(numbers[0]) });
    }
    if (type == TfType::Find<VtArray<GfVec2f>>() && numbers.size() == 2)
    {
        return VtValue(VtArray<GfVec2f>{ GfVec2f(numbers[0], numbers[1]) });
    }
    if (type == TfType::Find<VtArray<GfVec3f>>() && numbers.size() == 3)
    {
        return VtValue(VtArray<GfVec3f>{ GfVec3f(numbers[0], numbers[1], numbers[2]) });
    }
    return VtValue();
}


/// Rename the UV set \p from to \p to on one mesh. Returns false, and logs why, if the material \p readBy reads it,
/// it has time samples (only the default is copied), or it is authored outside the edit layer.
bool _renamePrimvar(MeshInfo& mesh,
                    const TfToken& from,
                    const TfToken& to,
                    const SdfPath& readBy,
                    const SdfLayerHandle& editLayer)
{
    const UsdGeomPrimvar& old = mesh.primvars.at(from);
    std::vector<UsdAttribute> attrs = { old.GetAttr() };
    if (const UsdAttribute oldIndices = old.GetIndicesAttr())
    {
        attrs.push_back(oldIndices);
    }
    /// Test both the primvar value and any index attribute before renaming.
    const auto anyAttr = [&](auto test) { return std::any_of(attrs.begin(), attrs.end(), test); };

    std::string reason;
    if (!readBy.IsEmpty())
    {
        reason = "material " + readBy.GetAsString() + " reads it";
    }
    else if (anyAttr(/* Retain sampled primvars under their original names. */ [](const UsdAttribute& attr)
                     { return attr.GetNumTimeSamples() > 0; }))
    {
        reason = "it has time samples";
    }
    else if (anyAttr(/* Retain names when another layer owns an opinion. */ [&](const UsdAttribute& attr)
                     { return !_onlyInLayer(attr, editLayer); }))
    {
        reason = "it is authored in a layer other than the edit target";
    }
    if (!reason.empty())
    {
        USD_OPTIMIZE_LOG_INFO("%s: not reconciled: cannot rename primvar %s to %s, %s",
                              mesh.prim.GetPath().GetAsString().c_str(),
                              from.GetText(),
                              to.GetText(),
                              reason.c_str());
        return false;
    }

    VtValue value;
    old.GetAttr().Get(&value);
    VtIntArray indices;
    const bool indexed = old.GetIndices(&indices);

    UsdGeomPrimvar created =
        UsdGeomPrimvarsAPI(mesh.prim).CreatePrimvar(to, old.GetTypeName(), old.GetInterpolation(), old.GetElementSize());
    created.GetAttr().Set(value);
    if (indexed)
    {
        created.SetIndices(indices);
    }
    const int unauthoredIndex = old.GetUnauthoredValuesIndex();
    if (unauthoredIndex != -1)
    {
        created.SetUnauthoredValuesIndex(unauthoredIndex);
    }

    for (const UsdAttribute& attr : attrs)
    {
        mesh.prim.RemoveProperty(attr.GetName());
    }

    mesh.primvars.erase(from);
    mesh.primvars.emplace(to, created);
    USD_OPTIMIZE_LOG_INFO("%s: renamed primvar %s to %s",
                          mesh.prim.GetPath().GetAsString().c_str(),
                          from.GetText(),
                          to.GetText());
    return true;
}


/// The first material bound to the mesh, or to one of its material subsets, that reads the UV set \p name through a
/// UsdPrimvarReader_float2, or an empty path. Materials that pick UVs another way, such as by index, are not seen.
SdfPath _materialReading(const UsdPrim& prim, const TfToken& name)
{
    std::vector<UsdPrim> bound = { prim };
    for (const UsdGeomSubset& subset : UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets())
    {
        bound.push_back(subset.GetPrim());
    }

    // varname is a string, or a token in older assets.
    const VtValue asString(name.GetString());
    const VtValue asToken(name);
    for (const UsdPrim& boundPrim : bound)
    {
        for (const TfToken& purpose : { UsdShadeTokens->full, UsdShadeTokens->preview })
        {
            const UsdShadeMaterial material = UsdShadeMaterialBindingAPI(boundPrim).ComputeBoundMaterial(purpose);
            if (!material)
            {
                continue;
            }
            for (const UsdPrim& descendant : UsdPrimRange(material.GetPrim()))
            {
                const UsdShadeShader shader(descendant);
                TfToken shaderId;
                if (!shader || !shader.GetShaderId(&shaderId) || shaderId != _tokens->UsdPrimvarReader_float2)
                {
                    continue;
                }
                // Follows a connection to a material input.
                for (const UsdAttribute& attr : shader.GetInput(_tokens->varname).GetValueProducingAttributes())
                {
                    VtValue value;
                    if (attr.Get(&value) && (value == asString || value == asToken))
                    {
                        return material.GetPath();
                    }
                }
            }
        }
    }
    return SdfPath();
}


/// Give every mesh of the group the same UV set name. A mesh whose material reads its name keeps it, and if all such
/// meshes agree the others take that name. Otherwise the name most meshes had before conform wins, st on a tie.
void _reconcileUvNames(std::vector<MeshInfo>& meshes, const SdfLayerHandle& editLayer, Tally& tally)
{
    size_t onlySt = 0;
    size_t onlySt0 = 0;
    size_t generatedSt = 0; // generated UVs are st, and take the group's name instead of counting toward it
    for (const MeshInfo& mesh : meshes)
    {
        const bool hasSt = mesh.primvars.count(_tokens->st) > 0;
        const bool hasSt0 = mesh.primvars.count(_tokens->st0) > 0;
        if (hasSt && hasSt0)
        {
            // merge needs the same sets on every mesh.
            USD_OPTIMIZE_LOG_INFO("%s: not reconciled: has both st and st0", mesh.prim.GetPath().GetAsString().c_str());
            ++tally.notReconciled;
            return;
        }
        onlySt += hasSt;
        onlySt0 += hasSt0;
        generatedSt += mesh.uvsGenerated;
    }
    if (onlySt == 0 || onlySt0 == 0)
    {
        return;
    }

    std::vector<SdfPath> readBy(meshes.size());
    std::set<TfToken> namesRead;
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        for (const TfToken& name : { _tokens->st, _tokens->st0 })
        {
            if (meshes[i].primvars.count(name) > 0)
            {
                readBy[i] = _materialReading(meshes[i].prim, name);
                if (!readBy[i].IsEmpty())
                {
                    namesRead.insert(name);
                }
            }
        }
    }

    const TfToken target = namesRead.size() == 1          ? *namesRead.begin() :
                           onlySt0 + generatedSt > onlySt ? _tokens->st0 :
                                                            _tokens->st;
    const TfToken other = target == _tokens->st ? _tokens->st0 : _tokens->st;
    for (size_t i = 0; i < meshes.size(); ++i)
    {
        if (meshes[i].primvars.count(other) > 0)
        {
            ++(_renamePrimvar(meshes[i], other, target, readBy[i], editLayer) ? tally.renamed : tally.notReconciled);
        }
    }
}


/// Add primvars that some meshes of the group lack, and report primvars whose type differs between meshes.
void _fillMissingPrimvars(const std::string& group,
                          std::vector<MeshInfo>& meshes,
                          const MeshPreparationOptions& options,
                          Tally& tally)
{
    // The meshes that have each primvar
    std::map<TfToken, std::vector<const UsdGeomPrimvar*>> owners;
    for (const MeshInfo& mesh : meshes)
    {
        for (const auto& [name, primvar] : mesh.primvars)
        {
            owners[name].push_back(&primvar);
        }
    }

    for (const auto& [name, haveIt] : owners)
    {
        const SdfValueTypeName typeName = haveIt.front()->GetTypeName();
        const bool sameType = std::all_of(haveIt.begin(),
                                          haveIt.end(),
                                          /* Require one common primvar type before supplying missing values. */
                                          [&](const UsdGeomPrimvar* primvar)
                                          { return primvar->GetTypeName().GetType() == typeName.GetType(); });
        if (!sameType)
        {
            USD_OPTIMIZE_LOG_INFO(
                "%s: not reconciled: primvar %s has different types on different meshes, merge will "
                "drop it",
                group.c_str(),
                name.GetText());
            ++tally.notReconciled;
            continue;
        }

        if (haveIt.size() == meshes.size())
        {
            continue;
        }

        // Normals and UVs are generated instead, as a made-up value would be wrong.
        if (name == _tokens->normals || name == _tokens->st || name == _tokens->st0)
        {
            continue;
        }
        // The fill is one element.
        const int elementSize = haveIt.front()->GetElementSize();
        if (elementSize != 1)
        {
            USD_OPTIMIZE_LOG_INFO(
                "%s: not reconciled: primvar %s has element size %d, which merge does not combine correctly",
                group.c_str(),
                name.GetText(),
                elementSize);
            ++tally.notReconciled;
            continue;
        }
        VtValue fill;
        const auto supplied = options.primvarDefaults.find(name);
        if (supplied != options.primvarDefaults.end())
        {
            fill = _parseFillValue(typeName, supplied->second);
            if (fill.IsEmpty())
            {
                USD_OPTIMIZE_LOG_INFO("%s: not reconciled: default \"%s\" does not fit primvar %s of type %s",
                                      group.c_str(),
                                      supplied->second.c_str(),
                                      name.GetText(),
                                      typeName.GetAsToken().GetText());
                ++tally.notReconciled;
                continue;
            }
        }
        else if (options.useDefaultPrimvarValues)
        {
            fill = _makeFillValue(name, typeName);
        }
        else
        {
            USD_OPTIMIZE_LOG_INFO("%s: missing primvar %s has no supplied default", group.c_str(), name.GetText());
            ++tally.notReconciled;
            continue;
        }
        if (fill.IsEmpty())
        {
            USD_OPTIMIZE_LOG_INFO("%s: not reconciled: primvar %s has type %s, which merge cannot combine",
                                  group.c_str(),
                                  name.GetText(),
                                  typeName.GetAsToken().GetText());
            ++tally.notReconciled;
            continue;
        }

        for (MeshInfo& mesh : meshes)
        {
            if (mesh.primvars.count(name) > 0)
            {
                continue;
            }
            UsdGeomPrimvar created = UsdGeomPrimvarsAPI(mesh.prim).CreatePrimvar(name, typeName, UsdGeomTokens->constant);
            created.GetAttr().Set(fill);
            mesh.primvars.emplace(name, created);
            ++tally.filled;
            USD_OPTIMIZE_LOG_INFO("%s: added missing primvar %s",
                                  mesh.prim.GetPath().GetAsString().c_str(),
                                  name.GetText());
        }
    }
}


/// Make doubleSided the same on every mesh when allowed, or report the difference.
void _reconcileDoubleSided(const std::string& group, std::vector<MeshInfo>& meshes, bool makeDoubleSided, Tally& tally)
{
    std::vector<UsdGeomMesh> singleSided;
    bool anyDouble = false;
    for (const MeshInfo& mesh : meshes)
    {
        UsdGeomMesh geomMesh(mesh.prim);
        bool value = false;
        geomMesh.GetDoubleSidedAttr().Get(&value);
        if (value)
        {
            anyDouble = true;
        }
        else
        {
            singleSided.push_back(geomMesh);
        }
    }

    if (!anyDouble || singleSided.empty())
    {
        return;
    }
    if (!makeDoubleSided)
    {
        USD_OPTIMIZE_LOG_INFO("%s: not reconciled: doubleSided differs", group.c_str());
        ++tally.notReconciled;
        return;
    }
    for (const UsdGeomMesh& geomMesh : singleSided)
    {
        geomMesh.CreateDoubleSidedAttr().Set(true);
        ++tally.schemaFixed;
        USD_OPTIMIZE_LOG_INFO("%s: set doubleSided to true", geomMesh.GetPath().GetAsString().c_str());
    }
}

/// True if the mesh has normals, as the normals attribute or as the normals primvar.
bool _hasNormals(const UsdPrim& prim)
{
    const UsdAttribute normalsAttr = UsdGeomMesh(prim).GetNormalsAttr();
    if (normalsAttr && normalsAttr.HasAuthoredValue())
    {
        return true;
    }
    const UsdGeomPrimvar primvar = UsdGeomPrimvarsAPI(prim).GetPrimvar(_tokens->normals);
    return primvar && primvar.HasAuthoredValue();
}


/// True if the mesh has a UV set that merge compares.
bool _hasUvs(const MeshInfo& mesh)
{
    return mesh.primvars.count(_tokens->st) > 0 || mesh.primvars.count(_tokens->st0) > 0;
}


/// Run another operation on the meshes, with every argument but the paths at its default.
void _runOnMeshes(ExecutionContext& parent, const char* operationName, const std::vector<MeshInfo*>& meshes)
{
    auto operation = UsdOptimizeCore::getInstance().getOperation(operationName);
    if (operation == nullptr)
    {
        return; // LCOV_EXCL_LINE
    }

    JsArray paths;
    for (const MeshInfo* mesh : meshes)
    {
        paths.push_back(JsValue(mesh->prim.GetPath().GetAsString()));
    }
    JsObject args;
    args["paths"] = JsValue(paths);

    ExecutionContext childContext;
    usd_optimize_execution_context_copy(&childContext, &parent);
    auto result = operation->execute(&childContext, args);
    usd_optimize_operation_result_free(&result);
    usd_optimize_execution_context_free(&childContext);
}


/// Generate normals and UVs for the meshes that lack them, when another mesh of the group has them.
void _generateMissing(ExecutionContext& context,
                      const std::string& group,
                      std::vector<MeshInfo>& meshes,
                      bool generateNormals,
                      bool generateUVs,
                      Tally& tally)
{
    // Generate a missing channel only where another mesh in the group already has it. Return the changed meshes.
    const auto generate = [&](const char* operationName, const char* what, bool enabled, bool polygonsOnly, auto hasIt)
    {
        std::vector<MeshInfo*> lacking;
        for (MeshInfo& mesh : meshes)
        {
            if (!hasIt(mesh))
            {
                lacking.push_back(&mesh);
            }
        }
        if (lacking.empty() || lacking.size() == meshes.size())
        {
            return std::vector<MeshInfo*>();
        }

        if (!enabled)
        {
            USD_OPTIMIZE_LOG_INFO("%s: not reconciled: %s missing from %s",
                                  group.c_str(),
                                  what,
                                  _meshCount(lacking.size()).c_str());
            ++tally.notReconciled;
            return std::vector<MeshInfo*>();
        }

        // USD says normals should not be authored on a subdivision surface, as it computes its own. merge keeps meshes
        // with different subdivision schemes apart, so the meshes of a group all have the same one.
        TfToken scheme;
        UsdGeomMesh(lacking.front()->prim).GetSubdivisionSchemeAttr().Get(&scheme);
        if (polygonsOnly && scheme != UsdGeomTokens->none)
        {
            USD_OPTIMIZE_LOG_INFO("%s: not reconciled: %s missing from %s, not generated for subdivisionScheme %s",
                                  group.c_str(),
                                  what,
                                  _meshCount(lacking.size()).c_str(),
                                  scheme.GetText());
            ++tally.notReconciled;
            return std::vector<MeshInfo*>();
        }

        _runOnMeshes(context, operationName, lacking);

        // Check each mesh, as generateNormals leaves a mesh it fails on unchanged.
        std::vector<MeshInfo*> generated;
        for (MeshInfo* mesh : lacking)
        {
            *mesh = _collectMeshInfo(mesh->prim);
            if (!hasIt(*mesh))
            {
                USD_OPTIMIZE_LOG_INFO("%s: not reconciled: could not generate %s",
                                      mesh->prim.GetPath().GetAsString().c_str(),
                                      what);
                ++tally.notReconciled;
                continue;
            }
            generated.push_back(mesh);
            ++tally.generated;
            USD_OPTIMIZE_LOG_INFO("%s: generated %s", mesh->prim.GetPath().GetAsString().c_str(), what);
        }
        return generated;
    };

    generate("generateNormals",
             "normals",
             generateNormals,
             true,
             /* Select meshes with authored normals. */ [](const MeshInfo& mesh) { return _hasNormals(mesh.prim); });
    for (MeshInfo* mesh : generate("generateProjectionUVs", "UVs", generateUVs, false, _hasUvs))
    {
        mesh->uvsGenerated = true;
    }
}


/// Split the meshes by whether they have normals and UVs, as merge keeps meshes that differ in these apart.
std::vector<std::vector<MeshInfo>> _splitByNormalsAndUvs(std::vector<MeshInfo> meshes)
{
    std::map<std::pair<bool, bool>, std::vector<MeshInfo>> parts;
    for (MeshInfo& mesh : meshes)
    {
        const std::pair<bool, bool> key(_hasNormals(mesh.prim), _hasUvs(mesh));
        parts[key].push_back(std::move(mesh));
    }

    std::vector<std::vector<MeshInfo>> result;
    for (auto& [key, part] : parts)
    {
        result.push_back(std::move(part));
    }
    return result;
}


} // namespace

/// Return whether the mesh authors normals as an attribute or primvar.
bool meshHasNormals(const UsdPrim& prim)
{
    return _hasNormals(prim);
}

/// Parse one constant primvar value, returning empty for invalid or unsupported input.
VtValue parsePrimvarDefault(const SdfValueTypeName& typeName, const std::string& text)
{
    return _parseFillValue(typeName, text);
}

/// Reconcile the requested visual properties within one compatible mesh group.
MeshPreparationResult prepareMeshes(ExecutionContext& context,
                                    const std::string& group,
                                    const std::vector<UsdPrim>& prims,
                                    const MeshPreparationOptions& options)
{
    Tally tally;
    if (prims.empty())
    {
        return tally;
    }

    const UsdStageRefPtr stage = UsdUtilsStageCache::Get().Find(UsdStageCache::Id::FromLongInt(context.usdStageId));
    if (!stage)
    {
        throw std::invalid_argument("Cannot prepare meshes: execution context has no USD stage");
    }
    for (const UsdPrim& prim : prims)
    {
        if (!prim || !prim.IsA<UsdGeomMesh>())
        {
            throw std::invalid_argument("Cannot prepare meshes: expected a valid Mesh prim");
        }
        if (prim.GetStage() != UsdStageWeakPtr(stage))
        {
            throw std::invalid_argument("Cannot prepare mesh " + prim.GetPath().GetAsString() +
                                        ": it belongs to another USD stage");
        }
    }

    std::vector<MeshInfo> meshes;
    for (const auto& prim : prims)
    {
        meshes.push_back(_collectMeshInfo(prim));
    }
    _generateMissing(context, group, meshes, options.generateNormals, options.generateUVs, tally);
    const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();
    for (auto& part : _splitByNormalsAndUvs(std::move(meshes)))
    {
        if (part.size() < 2)
        {
            continue;
        }
        _reconcileUvNames(part, editLayer, tally);
        _fillMissingPrimvars(group, part, options, tally);
        _reconcileDoubleSided(group, part, options.makeDoubleSided, tally);
    }
    return tally;
}

} // namespace usd_optimize
