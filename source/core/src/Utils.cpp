// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/Utils.h"

// USD
#include <pxr/base/arch/fileSystem.h>
#include <pxr/base/arch/vsnprintf.h>
#include <pxr/base/gf/matrix2f.h>
#include <pxr/base/gf/matrix3f.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/quaternion.h>
#include <pxr/base/vt/types.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/inherits.h>
#include <pxr/usd/usd/payloads.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/specializes.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// TBB
#include <tbb/parallel_for.h>

// C++
#include <iomanip>


namespace omni::scene::optimizer
{

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((geometry, "Geometry"))
    ((inputsDiffuseColor, "inputs:diffuseColor"))
    ((inputsDiffuseColorConstant, "inputs:diffuse_color_constant"))
    ((inputsDisplayColor, "inputs:displayColor"))
    ((inputsOpacity, "inputs:opacity"))
    (mdl)
    (nonpersistant)
    ((primvarsUVs, "st"))
    ((specifier, "specifier"))
    ((typeName, "typeName"))
);
// LCOV_EXCL_STOP
// clang-format on


ScopedTimer::ScopedTimer(const std::string& label, bool paused)
    : m_label(label)
    , m_level(LogLevel::eInfo)
{
    if (!paused)
    {
        start();
    }
}


ScopedTimer::ScopedTimer(const std::string& label, const std::string& category, LogLevel level, bool paused)
    : m_label(label)
    , m_category(category)
    , m_level(level)
{
    if (!paused)
    {
        start();
    }
}


ScopedTimer::~ScopedTimer()
{
    stop();
}


void ScopedTimer::start()
{
    m_start = std::chrono::high_resolution_clock::now();
    m_stopped = false;
    m_paused = false;
}


void ScopedTimer::pause()
{
    if (m_paused || m_stopped)
    {
        return;
    }

    m_paused = true;
    auto et = std::chrono::high_resolution_clock::now();
    m_accumMs += std::chrono::duration_cast<std::chrono::milliseconds>(et - m_start).count();
}


void ScopedTimer::stop()
{
    if (m_stopped)
    {
        return;
    }

    pause();
    m_stopped = true;

    double time = static_cast<double>(m_accumMs) / 1000.0;

    std::ostringstream oss;
    oss << m_label << " (" << time << "s)";

    // Log the message.
    SO_LOG(carbLevelFromLogLevel(m_level), "%s", oss.str().c_str());
}


static bool _isNonPersistent(const UsdAttribute& attribute)
{
    // Yes, the actual piece of metadata is spelled incorrectly
    // That is the least of its problems
    const VtValue& customVal = attribute.GetCustomDataByKey(_tokens->nonpersistant);
    if (customVal.IsHolding<bool>() && customVal.Get<bool>())
    {
        return true;
    }

    return false;
}


size_t _hashPrim(const UsdStageWeakPtr& usdStage, const UsdPrim& prim, HashCache& cache, const IncludeAttrValueFn& includeFn)
{
    return _hashPrim(usdStage, prim, &cache, includeFn);
}


size_t _hashPrim(const UsdStageWeakPtr& usdStage, const UsdPrim& prim, HashCache* cache, const IncludeAttrValueFn& includeFn)
{

    // Check cache. We might end up visiting prims multiple times eg to hash connections.
    if (cache)
    {
        auto findIt = cache->find(prim);
        if (findIt != cache->end())
        {
            return findIt->second;
        }
    }

    size_t hash = 0;

    for (const auto& attribute : prim.GetAuthoredAttributes())
    {
        // Some attributes may be non-persistent data. For example, Materials can hold an attribute
        // called "paused" that is temporary. This appears to be runtime data we probably don't want
        // to hash.
        if (_isNonPersistent(attribute))
        {
            continue;
        }

        // Hash the attribute name
        // Note: TfToken hashes are not stable, see note below.
        hash = TfHash::Combine(hash, attribute.GetName().GetString());

        // If there are any connections then hash them.
        if (attribute.HasAuthoredConnections())
        {

            SdfPathVector connections;
            if (attribute.GetConnections(&connections))
            {
                for (const auto& connection : connections)
                {
                    // Hash the connection name
                    hash = TfHash::Combine(hash, connection.GetNameToken().GetString());

                    if (UsdShadeOutput::IsOutput(attribute))
                    {
                        const UsdPrim& connectedPrim = usdStage->GetPrimAtPath(connection.GetPrimPath());
                        if (connectedPrim && connectedPrim != prim)
                        {
                            // Include the hash of the thing it is connected to. This way we can consider things the
                            // same eg inside shading networks even if the shaders have different names.
                            size_t connectionHash = _hashPrim(usdStage, connectedPrim, cache, includeFn);
                            hash = TfHash::Combine(hash, connectionHash);
                        }
                    }
                }
            }
        }
        else if (attribute.HasAuthoredValue())
        {

            // If there is an includeFn specified, filter whether we hash the value or not.
            if (includeFn && !includeFn(attribute))
            {
                continue;
            }

            VtValue val;
            if (attribute.Get(&val))
            {
                // For tokens or tokens[], we need to use the string if we want the hash to be
                // stable between processes. For some uses it doesn't matter, but for e.g.
                // ujitso caching, we need to do so.
                if (val.IsHolding<TfToken>())
                {
                    hash = TfHash::Combine(hash, val.UncheckedGet<TfToken>().GetString());
                }
                else if (val.IsHolding<VtTokenArray>())
                {
                    const auto& tokens = val.UncheckedGet<VtTokenArray>();
                    for (const auto& token : tokens)
                    {
                        hash = TfHash::Combine(hash, token.GetString());
                    }
                }
                else
                {
                    // For non-tokens, just straight combine.
                    hash = TfHash::Combine(hash, val);
                }
            }
        }
    }

    // Recurse in to children
    for (const auto& child : prim.GetAllChildren())
    {
        size_t childHash = _hashPrim(usdStage, child, cache, includeFn);
        hash = TfHash::Combine(hash, childHash);
    }

    // Cache
    if (cache)
    {
        (*cache)[prim] = hash;
    }

    return hash;
}


size_t _hashPrim(const UsdStageWeakPtr& usdStage, const UsdPrim& prim, HashCache& cache)
{
    // Kept for compatibility, just call the base _hashPrim function with no filter
    return _hashPrim(usdStage, prim, cache, nullptr);
}


// Construct a list of unique prim paths based on a parent and a list of preferred names.
// This mimics the logic of UsdGeomSubset::CreateUniqueGeomSubset but operates in bulk.
SdfPathVector _getUniqueChildPaths(const UsdStageWeakPtr& usdStage,
                                   const SdfPath& parentPath,
                                   const TfTokenVector& preferredNames)
{
    // Construct an appropriately sized vector of paths.
    SdfPathVector paths;
    paths.reserve(preferredNames.size());

    // Track names that either already exist in the stage or will be created.
    std::set<TfToken> reservedNames;

    // Populate existing child prim names for the parent prim including deactivated and undefined children.
    const UsdPrim& parentPrim = usdStage->GetPrimAtPath(parentPath);
    if (parentPrim)
    {
        for (const auto& child : parentPrim.GetAllChildren())
        {
            reservedNames.insert(child.GetName());
        }
    }

    // When requesting large amounts of child paths for the same base name cache the index
    // so that subsequent lookups don't need to check the same names over and over.
    std::unordered_map<std::string, size_t> startIndices;

    // Check if preferred names are already reserved and increment a counter on them until an available name is found.
    for (const auto& preferredName : preferredNames)
    {
        // Convert the name token to a string once.
        const std::string& baseName = preferredName.GetString();

        // Get the latest index for this name.
        size_t& index = startIndices[baseName];

        std::string name = baseName;
        while (true)
        {
            const TfToken& nameToken = TfToken(name);
            if (!reservedNames.count(nameToken))
            {
                paths.push_back(parentPath.AppendChild(nameToken));
                reservedNames.insert(nameToken);
                break;
            }
            index++;
            name = TfStringPrintf("%s_%zu", baseName.c_str(), index);
        }
    }

    return paths;
}

void _safeCreatePrim(const UsdStageWeakPtr& usdStage,
                     const SdfPath& primPath,
                     const std::string& typeName,
                     const std::string& parentTypeName,
                     SdfLayerHandle& editLayer)
{
    // Build a list of the parent prim paths that do not already exist on the stage so that we can create them with
    // appropriate specifier and type name later. We cannot create them during iteration as that will create parent
    // prims with an over specifier.
    SdfPathVector parentPaths;
    for (const auto& parentPath : primPath.GetParentPath().GetAncestorsRange())
    {
        // break on the first prim path that exists on the stage
        if (usdStage->GetPrimAtPath(parentPath))
        {
            break;
        }
        parentPaths.push_back(parentPath);
    }

    // Define the new prim on the stage via Sdf so that parent specifiers are unchanged
    SdfPrimSpecHandle primSpec = SdfCreatePrimInLayer(editLayer, primPath);
    primSpec->SetTypeName(typeName);
    primSpec->SetSpecifier(SdfSpecifierDef);

    // Define missing parent prims on the stage via Sdf so that parent specifiers are unchanged
    for (const auto& parentPath : parentPaths)
    {
        SdfPrimSpecHandle parentPrimSpec = SdfCreatePrimInLayer(editLayer, parentPath);
        parentPrimSpec->SetTypeName(parentTypeName);
        parentPrimSpec->SetSpecifier(SdfSpecifierDef);
    }
}

static bool _getColorFromPrim(const UsdPrim& prim, ColorValue& colorValue)
{
    // Attribute names that heuristics indicate may hold display color values
    static const TfTokenVector& attrNames = { _tokens->inputsDisplayColor,
                                              _tokens->inputsDiffuseColor,
                                              _tokens->inputsDiffuseColorConstant };

    bool result = false;

    // Check the various color attributes against the mesh to see if we can find one.
    for (const TfToken& attrName : attrNames)
    {
        const UsdAttribute& attr = prim.GetAttribute(attrName);
        if (attr && attr.Get(&colorValue.color))
        {
            result = true;
            break;
        }
    }

    // Found a color, so also check for opacity.
    if (result)
    {
        // Attempt to read opacity. If there was a value then it will be populated.
        const UsdAttribute& attr = prim.GetAttribute(_tokens->inputsOpacity);
        if (attr)
        {
            attr.Get(&colorValue.opacity);
        }
    }

    return result;
}

bool _getMaterialAlbedo(const UsdShadeMaterial& material, ColorValue& colorValue)
{
    // early out for invalid UsdShadeMaterial
    if (!material)
    {
        return false;
    }

    // Default to 1.0 (no opacity)
    colorValue.opacity = 1.0;

    // Check for attributes on the material first as these are likely to represent the public interface of the material
    // and hold actual values rather than connections to other input attributes that hold values
    const UsdPrim& materialPrim = material.GetPrim();
    if (_getColorFromPrim(materialPrim, colorValue))
    {
        return true;
    }

    // Check for an MDL or default surface output.
    UsdShadeShader shader = material.ComputeSurfaceSource({ _tokens->mdl, UsdShadeTokens->universalRenderContext });

    // No shader found. Return false as we couldn't find a prim to check for colors.
    if (!shader)
    {
        return false;
    }

    // Check for attributes on the surface shader if we didn't find anything on the material as the value may not have
    // been connected to the material prim inputs.
    const UsdPrim& shaderPrim = shader.GetPrim();

    // Check the shader. It either finds something or doesn't and that is the end result.
    return _getColorFromPrim(shaderPrim, colorValue);
}


void _findInstancedPrims(const UsdStageWeakPtr& stage, std::set<UsdPrim>& instancedPrims)
{
    // Get prototypes
#if PXR_VERSION >= 2011
    const std::vector<UsdPrim>& prototypes = stage->GetPrototypes();
#else
    const std::vector<UsdPrim>& prototypes = stage->GetMasters();
#endif

    // Thread this logic as the UsdPrimCompositionQuery is expensive. Scenes with large numbers of prototypes
    // suffer here.
    size_t count = prototypes.size();
    std::mutex insertMutex;

    tbb::parallel_for(tbb::blocked_range<size_t>(0, count),
                      [&](tbb::blocked_range<size_t> r)
                      {
                          for (size_t i = r.begin(); i < r.end(); ++i)
                          {
                              const auto& prim = prototypes[i];
                              for (const auto& child : prim.GetChildren())
                              {
                                  UsdPrimCompositionQuery compositionQuery(child);

                                  for (const auto& arc : compositionQuery.GetCompositionArcs())
                                  {
                                      // Right now specifically just looks for "references" and "payloads"
                                      const PcpArcType& arcType = arc.GetArcType();
                                      if (arcType == PcpArcTypeReference || arcType == PcpArcTypePayload)
                                      {
                                          // Get target. If it is valid, then insert its _parent_. This is the real
                                          // boundary we want to prevent merging outside.
                                          const auto& target = stage->GetPrimAtPath(arc.GetTargetNode().GetPath());
                                          if (target)
                                          {
                                              std::lock_guard<std::mutex> lock(insertMutex);
                                              instancedPrims.insert(target.GetParent());
                                          }
                                      }
                                  }
                              }
                          }
                      });
}

SdfPathVector _convertToSdfPaths(const std::vector<UsdPrim>& prims)
{
    SdfPathVector result;
    result.reserve(prims.size());

    for (const auto& prim : prims)
    {
        result.emplace_back(prim.GetPrimPath());
    }

    return result;
}


// Returns true if at least one op in OrderedXformOps contains the given suffix.
bool _containsOrderedXformOpsSuffix(const UsdPrim& prim, const TfToken& suffix)
{
    bool resetsXformStack = false;
    for (const auto& op : UsdGeomXformable(prim).GetOrderedXformOps(&resetsXformStack))
    {
        if (op.HasSuffix(suffix))
        {
            return true;
        }
    }
    return false;
}

// Return true if for the given prim an attribute ValueMightBeTimeVarying.
bool _mightBeTimeVarying(const UsdPrim& prim)
{
    for (const auto& attr : prim.GetAuthoredAttributes())
    {
        if (attr.ValueMightBeTimeVarying())
        {
            return true;
        }
    }
    return false;
}

/// Return true if an attribute of the prim has any authored time samples
bool _hasAuthoredTimeSamples(const UsdPrim& prim)
{
    for (const auto& attr : prim.GetAuthoredAttributes())
    {
        if (attr.GetNumTimeSamples() > 0)
        {
            return true;
        }
    }
    return false;
}

/// Filter a list of prims, removing a prim if an attribute has any authored time samples
void _removePrimsWithAuthoredTimeSamples(std::vector<UsdPrim>& prims)
{
    prims.erase(
        std::remove_if(prims.begin(), prims.end(), [](const UsdPrim& prim) { return _hasAuthoredTimeSamples(prim); }),
        prims.end());
}

GfVec3f _hsvToRgb(float hue, float saturation, float value)
{
    int h_i = int(hue * 6);
    float f = hue * 6 - (float)h_i;
    float p = value * (1 - saturation);
    float q = value * (1 - f * saturation);
    float t = value * (1 - (1 - f) * saturation);

    float r = 0.0, g = 0.0, b = 0.0;

    switch (h_i)
    {
    case 0:
        r = value;
        g = t;
        b = p;
        break;
    case 1:
        r = q;
        g = value;
        b = p;
        break;
    case 2:
        r = p;
        g = value;
        b = t;
        break;
    case 3:
        r = p;
        g = q;
        b = value;
        break;
    case 4:
        r = t;
        g = p;
        b = value;
        break;
    case 5:
        r = value;
        g = p;
        b = q;
        break;
    }

    return GfVec3f(r, g, b);
}

bool _primHasAuthoredUVsWithValues(const PXR_NS::UsdPrim& prim)
{
    UsdGeomPrimvarsAPI primvars(prim);
    auto authoredPrimvars = primvars.GetPrimvarsWithAuthoredValues();
    bool primHasAuthoredUVsWithValues = false;
    for (auto& primvar : authoredPrimvars)
    {
        if (primvar.GetBaseName() == _tokens->primvarsUVs)
        {
            primHasAuthoredUVsWithValues = true;
            break;
        }
    }
    return primHasAuthoredUVsWithValues;
}

void _flattenPropertyToPrimSpecWithValue(const UsdProperty& property, const SdfPrimSpecHandle& spec, const VtValue& value)
{
    // It is assumed that there is not already a property with this name present. The assumption is valid within the
    // current use of the function, but needs to be addressed if this were to be generalized.
    const TfToken& name = property.GetName();

    if (property.Is<UsdAttribute>())
    {
        const auto& attr = property.As<UsdAttribute>();

        SdfAttributeSpecHandle attrSpec = SdfAttributeSpec::New(spec, name, attr.GetTypeName());

        // Copy authored metadata
        for (const auto& metadata : attr.GetAllAuthoredMetadata())
        {
            attrSpec->SetInfo(metadata.first, metadata.second);
        }

        // Copy authored default value, or, use optional provided value instead.
        if (!value.IsEmpty())
        {
            attrSpec->SetInfo(SdfFieldKeys->Default, value);
        }
        else if (attr.HasAuthoredMetadata(SdfFieldKeys->Default))
        {
            VtValue defaultValue;
            if (attr.Get(&defaultValue))
            {
                attrSpec->SetInfo(SdfFieldKeys->Default, defaultValue);
            }
        }

        // TODO: Copy authored timesamples

        // Copy authored connections
        SdfPathVector sources;
        attr.GetConnections(&sources);
        if (!sources.empty())
        {
            attrSpec->GetConnectionPathList().GetExplicitItems() = sources; // LCOV_EXCL_LINE
        }
    }
    else if (property.Is<UsdRelationship>())
    {
        UsdRelationship rel = property.As<UsdRelationship>();
        SdfRelationshipSpecHandle relSpec = SdfRelationshipSpec::New(spec, name, /*custom*/ false, SdfVariabilityVarying);

        // Copy authored metadata
        for (const auto& metadata : rel.GetAllAuthoredMetadata())
        {
            relSpec->SetInfo(metadata.first, metadata.second);
        }

        // Copy authored targets
        SdfPathVector targets;
        rel.GetTargets(&targets);
        if (!targets.empty())
        {
            relSpec->GetTargetPathList().GetExplicitItems() = targets;
        }
    }
}


void _flattenPropertyToPrimSpec(const UsdProperty& property, const SdfPrimSpecHandle& spec)
{
    static VtValue emptyValue;
    _flattenPropertyToPrimSpecWithValue(property, spec, emptyValue);
}


// Returns true if the property can be meaningfully inherited by child prims.
//
// This is used to determine which properties can remain on a parent when splitting a prim into a parent Xform and
// child Gprim
bool _isInheritableProperty(const UsdProperty& property)
{
    const TfToken& nameToken = property.GetName();
    const std::string& name = nameToken.GetString();

    // Material bindings can be inherited.
    if (nameToken == UsdShadeTokens->materialBinding)
    {
        return true;
    }

    // The xformOpOrder and anything in the xformOp namespace can be inherited.
    if (nameToken == UsdGeomTokens->xformOpOrder || TfStringStartsWith(name, "xformOp:"))
    {
        return true;
    }

    // TODO: Handle constant primvars
    // TODO: Handle visibility

    return false;
}


// Helpers for the two-phase pattern used in `_batchedSplitIntoXformAndChild`.
// Phase 1 fills these records from USD in parallel; Phase 2 replays them onto Sdf serially.
struct _BatchSplitPropertyEdit
{
    TfToken name;
    bool isAttribute = true;
    bool goesOnXform = false;
    SdfValueTypeName typeName; // attributes only
    UsdMetadataValueMap authoredMetadata; // == std::map<TfToken, VtValue>
    VtValue defaultValue; // attributes; empty if not authored
    SdfPathVector connections; // attributes
    SdfPathVector targets; // relationships
};

struct _BatchSplitPerPrim
{
    SdfPath sourcePath;
    SdfPath tempXformPath;
    SdfPath tempMeshPath;
    TfToken meshTypeName;
    bool hasExistingSpec = false;
    UsdMetadataValueMap meshMetadata;
    std::vector<_BatchSplitPropertyEdit> properties;
    VtTokenArray pivotXformOpOrder;
    std::vector<_BatchSplitPropertyEdit> pivotProperties;
};

// Capture everything `_flattenPropertyToPrimSpec` would have read from the UsdProperty,
// so the write phase can author without touching USD.
static void _fillBatchSplitPropertyEdit(const UsdProperty& property, bool goesOnXform, _BatchSplitPropertyEdit& out)
{
    out.name = property.GetName();
    out.goesOnXform = goesOnXform;

    if (property.Is<UsdAttribute>())
    {
        out.isAttribute = true;
        const auto& attr = property.As<UsdAttribute>();
        out.typeName = attr.GetTypeName();
        out.authoredMetadata = attr.GetAllAuthoredMetadata();
        if (attr.HasAuthoredMetadata(SdfFieldKeys->Default))
        {
            attr.Get(&out.defaultValue);
        }
        attr.GetConnections(&out.connections);
    }
    else if (property.Is<UsdRelationship>())
    {
        out.isAttribute = false;
        const UsdRelationship rel = property.As<UsdRelationship>();
        out.authoredMetadata = rel.GetAllAuthoredMetadata();
        rel.GetTargets(&out.targets);
    }
}

// Write-only counterpart of `_flattenPropertyToPrimSpec` — takes pre-fetched data.
static void _writeBatchSplitPropertyEdit(const _BatchSplitPropertyEdit& edit, const SdfPrimSpecHandle& spec)
{
    if (edit.isAttribute)
    {
        SdfAttributeSpecHandle attrSpec = SdfAttributeSpec::New(spec, edit.name, edit.typeName);
        for (const auto& md : edit.authoredMetadata)
        {
            attrSpec->SetInfo(md.first, md.second);
        }
        if (!edit.defaultValue.IsEmpty())
        {
            attrSpec->SetInfo(SdfFieldKeys->Default, edit.defaultValue);
        }
        if (!edit.connections.empty())
        {
            attrSpec->GetConnectionPathList().GetExplicitItems() = edit.connections;
        }
    }
    else
    {
        SdfRelationshipSpecHandle relSpec =
            SdfRelationshipSpec::New(spec, edit.name, /*custom*/ false, SdfVariabilityVarying);
        for (const auto& md : edit.authoredMetadata)
        {
            relSpec->SetInfo(md.first, md.second);
        }
        if (!edit.targets.empty())
        {
            relSpec->GetTargetPathList().GetExplicitItems() = edit.targets;
        }
    }
}


void _batchedSplitIntoXformAndChild(const UsdStageWeakPtr& stage, const SdfPathVector& primPaths)
{
    const SdfLayerHandle& editLayer = stage->GetEditTarget().GetLayer();

    // Two-phase pattern: parallel reads into a cache, then serial Sdf writes from the cache.
    // The per-prim USD attribute machinery (path interning, token refcounting, attribute lookups
    // through composition) dominated this function's wall time when single-threaded; moving the
    // reads onto a parallel_for lets us recruit otherwise-idle workers while the (non-thread-safe)
    // Sdf write phase stays single-threaded.
    std::vector<_BatchSplitPerPrim> edits(primPaths.size());

    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, primPaths.size()),
        [&](const tbb::blocked_range<size_t>& range)
        {
            for (size_t i = range.begin(); i < range.end(); ++i)
            {
                const SdfPath& primPath = primPaths[i];
                _BatchSplitPerPrim& e = edits[i];

                e.sourcePath = primPath;
                const TfToken tempXformName = TfToken("__temp_name__" + primPath.GetName());
                e.tempXformPath = primPath.GetParentPath().AppendChild(tempXformName);
                e.tempMeshPath = e.tempXformPath.AppendChild(_tokens->geometry);
                e.hasExistingSpec = editLayer->HasSpec(primPath);

                const UsdPrim& prim = stage->GetPrimAtPath(primPath);
                e.meshTypeName = prim.GetTypeName();

                // Metadata destined for the temp Mesh spec.
                for (const auto& md : prim.GetAllAuthoredMetadata())
                {
                    if (md.first == _tokens->typeName || md.first == _tokens->specifier)
                    {
                        continue;
                    }
                    e.meshMetadata.emplace(md.first, md.second);
                }

                // Properties: pre-fetch everything the write phase will need.
                for (const auto& property : prim.GetAuthoredProperties())
                {
                    _BatchSplitPropertyEdit pe;
                    _fillBatchSplitPropertyEdit(property, _isInheritableProperty(property), pe);
                    e.properties.push_back(std::move(pe));
                }

                // Pivot xform ops: forward ops carry the value to flatten onto the Mesh;
                // both forward and inverse op names go into the new xformOpOrder so the
                // pivot/inverse pair round-trips correctly.
                UsdGeomXformable xformable(prim);
                if (xformable)
                {
                    bool reset = false;
                    const std::vector<UsdGeomXformOp> xformOps = xformable.GetOrderedXformOps(&reset);

                    for (const auto& op : _getPivotXformOps(xformOps))
                    {
                        _BatchSplitPropertyEdit pe;
                        _fillBatchSplitPropertyEdit(op.GetAttr(), /*goesOnXform=*/false, pe);
                        e.pivotProperties.push_back(std::move(pe));
                        e.pivotXformOpOrder.push_back(op.GetOpName());
                    }
                }
            }
        },
        tbb::auto_partitioner());

    // DANGER DANGER DANGER
    // Be very careful how edits are made while this block is in place. We are now responsible for tracking the
    // changes we make to layers as API read calls will be out of date.
    SdfChangeBlock _changeBlock;
    SdfBatchNamespaceEdit removeEdits;

    std::vector<SdfNamespaceEdit> renameOps;
    renameOps.reserve(edits.size());

    for (const auto& e : edits)
    {
        SdfPrimSpecHandle tempXformSpec = SdfCreatePrimInLayer(editLayer, e.tempXformPath);
        tempXformSpec->SetTypeName("Xform");
        tempXformSpec->SetSpecifier(SdfSpecifierDef);

        SdfPrimSpecHandle tempMeshSpec = SdfCreatePrimInLayer(editLayer, e.tempMeshPath);
        tempMeshSpec->SetTypeName(e.meshTypeName);
        tempMeshSpec->SetSpecifier(SdfSpecifierDef);

        for (const auto& md : e.meshMetadata)
        {
            tempMeshSpec->SetInfo(md.first, md.second);
        }

        for (const auto& pe : e.properties)
        {
            _writeBatchSplitPropertyEdit(pe, pe.goesOnXform ? tempXformSpec : tempMeshSpec);
        }

        for (const auto& pe : e.pivotProperties)
        {
            _writeBatchSplitPropertyEdit(pe, tempMeshSpec);
        }

        if (!e.pivotXformOpOrder.empty())
        {
            SdfAttributeSpecHandle attrSpec = SdfAttributeSpec::New(tempMeshSpec,
                                                                    UsdGeomTokens->xformOpOrder.GetString(),
                                                                    SdfValueTypeNames->TokenArray);
            attrSpec->SetField(SdfFieldKeys->Default, e.pivotXformOpOrder);
        }

        if (e.hasExistingSpec)
        {
            removeEdits.Add(SdfNamespaceEdit::Remove(e.sourcePath));
        }
        renameOps.push_back(SdfNamespaceEdit::Rename(e.tempXformPath, e.sourcePath.GetNameToken()));
    }

    // Apply the remove edits first otherwise renames will fail as the layer has prim specs with those names.
    editLayer->Apply(removeEdits);

    // Chunk the application of the edits. Apply() appears to be O(N^2) so the
    // cost balloons quickly as the number of edits grows. Chunking keeps things
    // significantly faster.
    constexpr size_t kRenameChunkSize = 500;
    const size_t totalOps = renameOps.size();

    for (size_t chunkStart = 0; chunkStart < totalOps; chunkStart += kRenameChunkSize)
    {
        const size_t chunkEnd = std::min(chunkStart + kRenameChunkSize, totalOps);

        SdfBatchNamespaceEdit chunk;
        for (size_t k = chunkStart; k < chunkEnd; ++k)
        {
            chunk.Add(renameOps[k]);
        }

        editLayer->Apply(chunk);
    }
}


std::string _getTempFile(const std::string& prefix, const std::string& suffix)
{
    std::string tmpDir = ArchGetTmpDir();

    // ArchGetTmpDir() returns /var/tmp on Linux; prefer /tmp for shorter paths.
    if (tmpDir == "/var/tmp")
    {
        tmpDir = "/tmp"; // LCOV_EXCL_LINE
    }

    static std::atomic<int> nCalls(1);
    const int n = nCalls++;
#if defined(_MSC_VER)
    int pid = _getpid();
#else
    int pid = getpid();
#endif

    if (n == 1)
    {
        return ArchStringPrintf("%s/%s.%d%s", tmpDir.c_str(), prefix.c_str(), pid, suffix.c_str());
    }
    else
    {
        return ArchStringPrintf("%s/%s.%d.%d%s", tmpDir.c_str(), prefix.c_str(), pid, n, suffix.c_str());
    }
}


float _calculteUVScaleValue(const UsdStageWeakPtr& stage, float scaleFactor, float scaleUnits)
{
    float scaleMult = 1.0f;
    if (abs(scaleUnits) >= std::numeric_limits<float>::epsilon())
    {
        scaleMult = UsdGeomGetStageMetersPerUnit(stage) / scaleUnits;
    }
    return scaleFactor * scaleMult;
}


char* getCStr(const std::string& name)
{
    size_t len = name.length() + 1;

    auto result = (char*)malloc(sizeof(char) * len);

    strncpy(result, name.c_str(), len - 1);
    result[len - 1] = '\0';

    return result;
}

UsdPrim _copyPrim(const UsdPrim& prim, const SdfLayerHandle& targetLayer, const SdfPath& targetPath)
{

    for (const auto& specHandle : prim.GetPrimStack())
    {
        if (!specHandle.GetSpec().GetPrimAtPath(prim.GetPrimPath()))
        {
            continue;
        }

        auto specLayer = specHandle.GetSpec().GetLayer();
        if (!specLayer)
        {
            continue;
        }

        // Copy the prim (also copies all child specs)
        SdfCopySpec(specLayer, prim.GetPrimPath(), targetLayer, targetPath);
    }

    return prim.GetStage()->GetPrimAtPath(targetPath);
}


bool _flattenInstance(const UsdPrim& prim)
{
    // Create an anonymous layer. We will "copy" the main properties and children
    // of the instance in order to persist it, prior to removing its composition
    // arcs. This data can then be restored on to the prim.
    SdfLayerRefPtr anonLayer = SdfLayer::CreateAnonymous();

    // Target prim needs to exist in order to flatten properties to it
    SdfCreatePrimInLayer(anonLayer, prim.GetPrimPath());

    // Open in a new stage
    UsdStageRefPtr anonStage = UsdStage::Open(anonLayer);
    UsdPrim targetPrim = anonStage->GetPrimAtPath(prim.GetPrimPath());

    // Get each authored property (attribute/relationship) and author to the new target
    // prim in the anonymous layer/stage.
    //
    // Note: we don't want to just SdfCopySpec the entire prim, this will copy the composition
    // arcs which is what we're trying to avoid.
    std::vector<UsdProperty> authoredProperties = prim.GetAuthoredProperties();
    for (const auto& property : authoredProperties)
    {
        property.FlattenTo(targetPrim);
    }

    // To handle external references, we need to manually copy the child prims to the
    // anonymous stage. Internal is fine, we could just copy the source prim children
    // directly to the final prim later. But the copy function does not work with those
    // when they are external, nor does it work with prototype prims.
    for (const auto& child : prim.GetFilteredDescendants(UsdTraverseInstanceProxies()))
    {
        SdfPath targetPath = child.GetPrimPath();
        UsdPrim targetChildPrim = anonStage->DefinePrim(targetPath, child.GetTypeName());

        const std::vector<UsdProperty>& authoredChildProperties = child.GetAuthoredProperties();
        for (const auto& property : authoredChildProperties)
        {
            property.FlattenTo(targetChildPrim);
        }

        // Also copy metadata (e.g. for API Schemas)
        const auto& metadata = child.GetAllAuthoredMetadata();
        for (const auto& [key, val] : metadata)
        {
            targetChildPrim.SetMetadata(key, val);
        }
    }

    // Disable instanceable so this is no longer an instance.
    // Then clear composition ARCS.
    // We may want to be more picky about this, for now, just nuke the lot.
    prim.SetInstanceable(false);
    prim.GetReferences().ClearReferences();
    prim.GetInherits().ClearInherits();
    prim.GetPayloads().ClearPayloads();
    prim.GetSpecializes().ClearSpecializes();

    // The original prim is now empty, so copy the properties and children back.
    // Note: again, we do this manually, to copy the minimum properties we can and not
    // mess with any other metadata (type etc.). Above we create a placeholder target
    // prim as an over, so we also don't want to copy that kind of info. The original
    // prim should be fine as it was, we mainly just need the properties.
    std::vector<UsdProperty> targetProperties = targetPrim.GetAuthoredProperties();
    for (const auto& property : targetProperties)
    {
        property.FlattenTo(prim);
    }

    // Finally copy the children this prim used to contain as instance proxies back.
    const auto& targetLayer = prim.GetStage()->GetRootLayer();

    for (const auto& child : targetPrim.GetChildren())
    {
        SdfPath targetPath = prim.GetPrimPath().AppendChild(child.GetName());
        _copyPrim(child, targetLayer, targetPath);
    }

    return true;
}


std::string _getFormattedBytes(double bytes)
{
    constexpr double KB = 1024;
    constexpr double MB = KB * 1024;
    constexpr double GB = MB * 1024;
    constexpr double TB = GB * 1024;

    std::string units = "";

    if (bytes < KB)
    {
        units = "B";
    }
    else if (bytes < MB)
    {
        bytes /= KB;
        units = "KB";
    }
    else if (bytes < GB)
    {
        bytes /= MB;
        units = "MB";
    }
    else if (bytes < TB)
    {
        bytes /= GB;
        units = "GB";
    }
    else
    {
        bytes /= TB;
        units = "TB";
    }

    std::stringstream oss;

    if (std::fmod(bytes, 1.0) != 0.0)
    {
        oss << std::fixed << std::setprecision(2);
    }

    oss << bytes << " " << units;

    return oss.str();
}


// Define the types we are interested in - the basic numeric types, and the basic vec types
// (GfVec[2-4]f, etc.)
#define SO_VT_TYPES                                                                                                    \
    VT_BUILTIN_NUMERIC_VALUE_TYPES                                                                                     \
    VT_VEC_VALUE_TYPES

#define SO_TYPE_AND_SIZE_PAIR(UNUSED, T) { std::type_index(typeid(VT_TYPE(T))), sizeof(VT_TYPE(T)) },

size_t _getSizeFromSdfValueType(const SdfValueTypeName& value)
{
    // Static map of types, one-off thread-safe creation
    static std::map<std::type_index, std::size_t> s_typeToSizeMap{
        TF_PP_SEQ_FOR_EACH(SO_TYPE_AND_SIZE_PAIR, ~, SO_VT_TYPES)
    };

    const auto& findIt = s_typeToSizeMap.find(value.GetType().GetTypeid());
    if (findIt != s_typeToSizeMap.end())
    {
        return findIt->second;
    }

    return 0;
}


// Define types that can be default constructed
#define SO_VT_DEFAULT_CONSTRUCTOR_TYPES                                                                                \
    VT_STRING_VALUE_TYPES                                                                                              \
    VT_VEC_VALUE_TYPES                                                                                                 \
    VT_MATRIX_VALUE_TYPES                                                                                              \
    VT_QUATERNION_VALUE_TYPES

#define SO_CREATE_DEFAULT_VALUE_NUMERIC(UNUSED, T)                                                                     \
    if (value.IsHolding<VT_TYPE(T)>() || value.IsHolding<VtArray<VT_TYPE(T)>>())                                       \
    {                                                                                                                  \
        VT_TYPE(T) v = static_cast<VT_TYPE(T)>(0.0);                                                                   \
        return VtValue(v);                                                                                             \
    }

#define SO_CREATE_DEFAULT_VALUE_DEFAULT_CONSTRUCTOR(UNUSED, T)                                                         \
    if (value.IsHolding<VT_TYPE(T)>() || value.IsHolding<VtArray<VT_TYPE(T)>>())                                       \
    {                                                                                                                  \
        return VtValue(VT_TYPE(T)());                                                                                  \
    }

VtValue _getDefaultSingleVtValue(const VtValue& value)
{
    // use USD preprocessor magic to create an if statement chain for all the types we care about
    TF_PP_SEQ_FOR_EACH(SO_CREATE_DEFAULT_VALUE_NUMERIC, ~, VT_BUILTIN_NUMERIC_VALUE_TYPES);
    TF_PP_SEQ_FOR_EACH(SO_CREATE_DEFAULT_VALUE_DEFAULT_CONSTRUCTOR, ~, SO_VT_DEFAULT_CONSTRUCTOR_TYPES);

    // if none of the types matched, return an empty VtValue
    return VtValue();
}


// Define types that can be converted to single-element arrays
#define SO_TO_ARRAY_TYPES                                                                                              \
    VT_BUILTIN_NUMERIC_VALUE_TYPES                                                                                     \
    VT_STRING_VALUE_TYPES                                                                                              \
    VT_VEC_VALUE_TYPES                                                                                                 \
    VT_MATRIX_VALUE_TYPES                                                                                              \
    VT_QUATERNION_VALUE_TYPES

#define SO_TO_ARRAY(UNUSED, T)                                                                                         \
    if (value.IsHolding<VT_TYPE(T)>())                                                                                 \
    {                                                                                                                  \
        return VtValue(VtArray<VT_TYPE(T)>(1, value.UncheckedGet<VT_TYPE(T)>()));                                      \
    }

VtValue _toArrayVtValue(const VtValue& value)
{
    TF_PP_SEQ_FOR_EACH(SO_TO_ARRAY, ~, SO_TO_ARRAY_TYPES);

    return value;
}


std::vector<UsdGeomXformOp> _getPivotXformOps(const std::vector<UsdGeomXformOp>& orderedXformOps)
{
    std::vector<UsdGeomXformOp> pivotOps;
    for (const UsdGeomXformOp& xformOp : orderedXformOps)
    {
        // skip inverse ops as we are looking for the forward ops in the first loop
        if (xformOp.IsInverseOp())
        {
            continue;
        }
        const std::string opName = xformOp.GetOpName().GetString();

        for (const UsdGeomXformOp& otherXformOp : orderedXformOps)
        {
            // we're only interested in inverse ops in the second loop
            if (!otherXformOp.IsInverseOp())
            {
                continue;
            }

            // if the inverse op has the same name as the forward op (minus the "!invert!" prefix) then this is a pivot
            static const std::string invertPrefix = "!invert!";
            const std::string commonName =
                otherXformOp.GetOpName().GetString().substr(invertPrefix.length(), std::string::npos);
            if (commonName == opName)
            {
                pivotOps.push_back(xformOp);
                break;
            }
        }
    }
    return pivotOps;
}


} // namespace omni::scene::optimizer
