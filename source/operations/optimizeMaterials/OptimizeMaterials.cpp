// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "OptimizeMaterials.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/TbbCompat.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/ar/resolverScopedCache.h>
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// TBB
#include <tbb/enumerable_thread_specific.h>

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (varname)
    (result)
);
// LCOV_EXCL_STOP
// clang-format on

SO_PLUGIN_INIT(omni::scene::optimizer::OptimizeMaterialsOperation);


namespace omni::scene::optimizer
{

// Constants
constexpr const char* s_categoryOptimizeMaterials = "OPTIMIZE_MATERIALS";


static void _deleteMaterials(const UsdStageWeakPtr& usdStage, const std::vector<UsdPrim>& prims)
{
    // Bit of an edge-case here. First delete any prims we can actually delete, but don't deactivate
    // anything. This is to avoid adding Overs with active=false in to a stage where materials might
    // be referenced, but deleting the original materials first would prevent this happening.
    // Once we have deleted things proper, and the ChangeBlock has closed, we can then try again with
    // deactivate=true to catch anything that we should actually deactivate.
    {
        SdfChangeBlock changeBlock;
        _deletePrims(usdStage, prims, false);
    }

    {
        SdfChangeBlock changeBlock;
        _deletePrims(usdStage, prims, true);
    }
}


static bool _hasComposition(const UsdPrim& prim)
{

    UsdPrimCompositionQuery compositionQuery(prim);
    for (const auto& arc : compositionQuery.GetCompositionArcs())
    {
        // Right now specifically just looks for "references" and "payloads"
        const PcpArcType& arcType = arc.GetArcType();
        if (arcType == PcpArcTypeReference || arcType == PcpArcTypePayload)
        {
            return true;
        }
    }

    return false;
}


static bool _hasSpecializes(const UsdPrim& prim)
{
    UsdPrimCompositionQuery compositionQuery(prim);
    for (const auto& arc : compositionQuery.GetCompositionArcs())
    {
        if (arc.GetArcType() == PcpArcTypeSpecialize)
        {
            return true;
        }
    }

    return false;
}


OptimizeMaterialsOperation::OptimizeMaterialsOperation()
    : Operation("optimizeMaterials",
                "Optimize Materials",
                "Run operations to optimize materials in a stage. Run this optimization to replace duplicates with "
                "references to unique materials. This can reduce memory usage and also improve performance.")
{

    addArgument("materialPrimPaths",
                "Materials to Optimize",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider",
                m_primPaths)
        .setPlaceholder("Add materials or all will be processed");

    addArgument("optimizeMaterialsMode", "Method", kDisplayTypeEnum, "The material optimization to perform", m_mode)
        .setEnumValues<OptimizeMaterialMode>(
            { { OptimizeMaterialMode::eDeduplicate, "Deduplicate" },
              { OptimizeMaterialMode::eConvertToColor, "Convert to color" },
              { OptimizeMaterialMode::eRemoveUnbound, "Remove unbound" },
              { OptimizeMaterialMode::eDeduplicateWithPrimvars, "Deduplicate with primvars" } });

    addArgument("materialsPath", "Materials Path", kDisplayTypePrimPath, "Path to create new Materials at", m_materialsPath)
        .setPlaceholder("/World/Looks")
        .setVisibleIf("optimizeMaterialsMode == 3");

    // Analysis/debug option
    addArgument("analysisCheckPrimvars",
                "Include Duplicates with Primvars",
                kDisplayTypeBool,
                "Whether to check for materials that can be deduplicated with primvars during analysis mode",
                m_analysisCheckPrimvars)
        .setVisible(false);
}


std::string OptimizeMaterialsOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion OptimizeMaterialsOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string OptimizeMaterialsOperation::getCategory() const
{
    return s_categoryOptimizeMaterials;
}


std::string OptimizeMaterialsOperation::getDisplayGroup() const
{
    return s_displayGroupMaterials;
}


bool OptimizeMaterialsOperation::getSupportsAnalysis() const
{
    return true;
}


OperationResult OptimizeMaterialsOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|OptimizeMaterialsOperation|Execute");
    // If a filter is specified then resolve the paths. If there is no filter then don't resolve - while this
    // is functionally correct (it will resolve all materials in the scene), it is a performance hit we don't
    // need to suffer.
    std::vector<UsdPrim> prims;
    if (!m_primPaths.empty())
    {
        prims = _resolveExpressionsToPrims(getUsdStage(), m_primPaths);
    }

    OperationResult result;

    switch (m_mode)
    {
    case OptimizeMaterialMode::eRemoveUnbound:
        result.success = removeUnboundMaterials(prims);
        break;
    case OptimizeMaterialMode::eDeduplicateWithPrimvars:
        result.success = convertToPrimvars(prims);
        break;
    default:
        result.success = optimizeMaterials(prims);
    }

    return result;
}


bool OptimizeMaterialsOperation::removeUnboundMaterials(const std::vector<UsdPrim>& prims)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|OptimizeMaterialsOperation|removeUnboundMaterials");

    // Do an initial traversal of the stage to find all materials. We will populate this set with all of them, and
    // then traverse again, removing any that we find to be bound.
    //
    // These iterations are threaded due to the high cost of the prim composition queries.
    std::set<UsdPrim> allMaterials;
    std::mutex allMaterialsMutex;

    std::vector<UsdPrim> iterMaterials = { getUsdStage()->GetPseudoRoot() };
    tbbcompat::parallelForEach(iterMaterials.begin(),
                               iterMaterials.end(),
                               [&](const UsdPrim& prim, tbbcompat::Feeder<UsdPrim>& feeder)
                               {
                                   // Don't need to worry about instance proxies. We will find instance proxies that are
                                   // bound and resolve them back to their base material.
                                   if (prim.IsInstanceProxy())
                                   {
                                       return;
                                   }

                                   const auto& children = prim.GetChildren();

                                   // Queue all children for traversal via parallel_for_each
                                   for (const auto& child : children)
                                   {
                                       feeder.add(child);
                                   }

                                   if (prim.IsA<UsdShadeMaterial>())
                                   {
                                       // If this material has some kind of composition then just ignore it, this will
                                       // filter out materials that are instances or references etc. We just want to
                                       // resolve back to the base definition.
                                       if (_hasComposition(prim))
                                       {
                                           return;
                                       }

                                       std::lock_guard<std::mutex> lock(allMaterialsMutex);
                                       allMaterials.insert(prim);
                                   }
                               });

    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    // Iterate over the stage again, this time including instance proxies. Now that we have a list of all materials
    // in the scene we can filter that down by removing any that are bound. This should leave us with just unused
    // materials.
    std::vector<UsdPrim> iterPrims = { getUsdStage()->GetPseudoRoot() };
    tbbcompat::parallelForEach(
        iterPrims.begin(),
        iterPrims.end(),
        [&](const UsdPrim& prim, tbbcompat::Feeder<UsdPrim>& feeder)
        {
            // Don't need to check materials or their children
            if (prim.IsA<UsdShadeMaterial>())
            {
                return;
            }

            const auto& children = prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));

            // Queue all children for traversal via parallel_for_each
            for (const auto& child : children)
            {
                feeder.add(child);
            }

            UsdShadeMaterialBindingAPI bindingAPI(prim);
            auto boundMaterial = bindingAPI.ComputeBoundMaterial(&bindingsCache, &collQueryCache);

            // Nothing bound, no need to continue.
            if (!boundMaterial)
            {
                return;
            }

            const auto& materialPrim = boundMaterial.GetPrim();

            // This material is used so remove it from the delete queue
            {
                std::lock_guard<std::mutex> lock(allMaterialsMutex);
                allMaterials.erase(materialPrim);
            }

            // Check if the material references/instances something. If so, we need to erase its
            // targets from the set of materials. This lets us avoid deleting materials that aren't
            // bound directly, but that eg have an instance of themselves bound.
            UsdPrimCompositionQuery compositionQuery(materialPrim);
            for (const auto& arc : compositionQuery.GetCompositionArcs())
            {
                // Right now specifically just looks for "references" and "payloads"
                const PcpArcType& arcType = arc.GetArcType();
                if (arcType == PcpArcTypeReference || arcType == PcpArcTypePayload)
                {
                    // Get the actual target - this is the base material we want to avoid deleting,
                    // as it is used indirectly by this reference.
                    const auto& target = getUsdStage()->GetPrimAtPath(arc.GetTargetNode().GetPath());
                    if (target)
                    {
                        std::lock_guard<std::mutex> lock(allMaterialsMutex);
                        allMaterials.erase(target);
                    }
                }
            }
        });

    // Final step: delete any materials that were not found to be used.
    if (!allMaterials.empty())
    {
        std::vector<UsdPrim> materialsToDelete;
        materialsToDelete.reserve(allMaterials.size());
        materialsToDelete.insert(materialsToDelete.begin(), allMaterials.begin(), allMaterials.end());

        _deleteMaterials(getUsdStage(), materialsToDelete);

        std::ostringstream oss;
        oss << "Removed " << materialsToDelete.size() << " unused materials.";
        SO_LOG_INFO(oss.str().c_str());
    }
    else
    {
        SO_LOG_INFO("No materials found.");
    }

    return true;
}


// Creates a map of material to its instanced boundary path. This path is used in the uniqueness
// hash, to ensure we don't try and deduplicate materials between instances.
static void _findInstancedParents(const UsdStageWeakPtr& usdStage, std::map<UsdPrim, SdfPath>& materialsToPaths)
{
    // Track the first parent that is an instance or the pseudo root.
    SdfPathVector instancedParentPaths = { SdfPath::AbsoluteRootPath() };

    // Cache instanced prims, so we don't bind materials outside their boundaries.
    std::set<UsdPrim> instancedPrims;
    _findInstancedPrims(usdStage, instancedPrims);

    UsdPrimRange range = UsdPrimRange::PreAndPostVisit(usdStage->GetPseudoRoot(), UsdPrimAllPrimsPredicate);

    for (auto it = range.begin(); it != range.end(); ++it)
    {
        const auto& prim = (*it);

        // Skip Pseudo Root
        if (prim.GetPath().IsAbsoluteRootPath())
        {
            continue;
        }

        // m_materialModes that rebind materials need to be aware of instanceable reference boundaries
        // Build a stack of instanced parents during traversal pushing on pre visit and popping on post visit
        if (it.IsPostVisit())
        {
            // Post visit
            // Pop the last path if it is the current prim.
            if (prim.GetPath() == instancedParentPaths.back())
            {
                instancedParentPaths.pop_back();
            }

            // Early out as this is the only logic used during post visits
            continue;
        }
        else
        {
            // Pre visit
            // Push the prim path if it is instanced.
            if (instancedPrims.count(prim))
            {
                instancedParentPaths.push_back(prim.GetPath());
            }
        }

        if (prim.IsA<UsdShadeMaterial>())
        {
            materialsToPaths[prim] = instancedParentPaths.back();
            it.PruneChildren();
        }
    }
}


// Callback when a material is hashed
using HashedMaterialFn = std::function<void(const UsdPrim& prim, size_t hash)>;

/// Takes a material and hashes it. If the material is a reference, then the source material is first
/// resolved and hashed, ensuring that if we are going to rebind uniqueMaterials in a stage that the least
/// referenced one is the one we rebind to.
///
/// An optional includeFn can be provided to specify which attributes should be considered for hashing.
/// A required hashFn is required as a callback after a material has been hashed.
///
/// Note that the callback may be called multiple times, for example when finding a source material.
static void _hashMaterial(const UsdPrim& prim,
                          const std::map<UsdPrim, SdfPath>& primToBoundary,
                          HashCache* hashCache,
                          const IncludeAttrValueFn& includeFn,
                          const HashedMaterialFn& hashFn,
                          bool checkArcs = true)
{
    // If anything was referenced we will hit it a second time - check the cache to
    // avoid running the expensive composition query/hash again.
    if (hashCache)
    {
        if (hashCache->find(prim) != hashCache->end())
        {
            return;
        }
    }

    const auto& stage = prim.GetStage();

    // First check whether this material references something else. If that is the case we want
    // to hash it first, so that we'd rebind to the least referenced material in the stage.
    // This works around binding within reference boundaries where possible.
    //
    // This is gated by a (default true) argument so that when we want to hash materials in
    // multiple ways, e.g. to check for primvar deduplication, we can avoid the expense of
    // this in subsequent calls.
    if (checkArcs)
    {
        UsdPrimCompositionQuery compositionQuery(prim);
        std::vector<UsdPrimCompositionQueryArc> arcs = compositionQuery.GetCompositionArcs();

        // Do a first pass. If any of the arcs are "specializes", then we cannot remove
        // this material. This is used to "copy" materials to compensate for reference
        // boundaries so in that sense is an intentional duplicate.
        for (const auto& arc : arcs)
        {
            if (arc.GetArcType() == PcpArcTypeSpecialize)
            {
                return;
            }
        }

        for (const auto& arc : arcs)
        {
            // Right now specifically just looks for "references"
            const PcpArcType& arcType = arc.GetArcType();
            if (arcType == PcpArcTypeReference)
            {
                // Get the actual target - this is the base material we want to avoid deleting, as
                // it is used indirectly by this reference.
                const auto& target = stage->GetPrimAtPath(arc.GetTargetNode().GetPath());

                if (target == prim)
                {
                    // LCOV_EXCL_START
                    SO_LOG_WARN("Not hashing material with reference target of itself: %s",
                                target.GetPrimPath().GetString().c_str());
                    continue;
                    // LCOV_EXCL_STOP
                }

                if (target)
                {
                    _hashMaterial(target, primToBoundary, hashCache, includeFn, hashFn, checkArcs);
                }
            }
        }
    }

    // Hash the material
    size_t hash = _hashPrim(stage, prim, hashCache, includeFn);

    // Include the path of the first parent that is instanced in the hash, so that we do not
    // bind prims to uniqueMaterials that are outside the scope of the prototype created by scene instancing.
    auto instancedPathIt = primToBoundary.find(prim);
    if (instancedPathIt != primToBoundary.end())
    {
        hash = TfHash::Combine(hash, VtHashValue(instancedPathIt->second));
    }
    else
    {
        // LCOV_EXCL_START
        SO_LOG_WARN("Failed to find boundary for %s", prim.GetPrimPath().GetAsString().c_str());
        // LCOV_EXCL_STOP
    }

    // Trigger callback with the material prim and the calculated hash.
    hashFn(prim, hash);
}


// Map of the types we support converting to a primvar reader
static const std::unordered_map<SdfValueTypeName, TfToken, SdfValueTypeNameHashFunctor> s_typeToReader = {
    { SdfValueTypeNames->Color3f, TfToken("UsdPrimvarReader_float3") },
    { SdfValueTypeNames->Color4f, TfToken("UsdPrimvarReader_float4") },
    { SdfValueTypeNames->Float, TfToken("UsdPrimvarReader_float") },
    { SdfValueTypeNames->Float2, TfToken("UsdPrimvarReader_float2") },
    { SdfValueTypeNames->Float3, TfToken("UsdPrimvarReader_float3") },
    { SdfValueTypeNames->Float4, TfToken("UsdPrimvarReader_float4") },
};


// Returns whether we support creating a primvar reader for the specified type
static bool _isSupportedType(const SdfValueTypeName& typeName)
{
    return s_typeToReader.find(typeName) != s_typeToReader.end();
}


static UsdPrim _createNewMaterial(const UsdPrim& originalMaterial, const std::string& materialsPath)
{
    // First check whether the target material location exists. If not, create.
    SdfPath baseMaterialPath(materialsPath);
    const auto& stage = originalMaterial.GetStage();
    auto layer = stage->GetEditTarget().GetLayer();

    const auto& looksPrim = stage->GetPrimAtPath(baseMaterialPath);
    if (!looksPrim)
    {
        _safeCreatePrim(stage, baseMaterialPath, "Xform", "", layer);
    }

    // Default to a material with the same name as the original, suffixed by _pv.
    // If this already exists, then we'll iterate appending an index until we find
    // an unused prim path.
    std::string newName = originalMaterial.GetName().GetString() + "_pv";
    std::string basePath = baseMaterialPath.AppendElementString(newName).GetAsString();
    std::string newPath = basePath;
    size_t index = 1;

    while (true)
    {
        const auto& prim = stage->GetPrimAtPath(SdfPath(newPath));

        // If not valid, then it doesn't exist and that's our new path.
        if (!prim.IsValid())
        {
            break;
        }

        newPath = basePath + std::to_string(index);
        ++index;
    }

    // Create the new prim by copying the original to the new location.
    UsdPrim newMaterial = _copyPrim(originalMaterial, layer, SdfPath(newPath));
    if (!newMaterial)
    {
        SO_LOG_WARN("Failed to copy material %s to %s",
                    originalMaterial.GetPrimPath().GetAsString().c_str(),
                    newPath.c_str());
    }

    return newMaterial;
}


static UsdPrim _createPrimvarReader(const UsdShadeMaterial& material,
                                    const TfToken& readerId,
                                    const UsdShadeInput& input,
                                    const UsdShadeInput& targetInput,
                                    const TfToken& primvarName)
{
    // First look to see if there is an existing primvar reader for this primvar.
    // It may already have one that we didn't create (which could be called anything), so we need to
    // look at the shaders and check their varname to verify.
    for (const auto& child : material.GetPrim().GetAllChildren())
    {
        if (child.IsA<UsdShadeShader>())
        {
            UsdShadeShader shader(child);

            TfToken id;
            shader.GetShaderId(&id);

            // Same type?
            if (id == readerId)
            {
                std::string varname;
                shader.GetInput(_tokens->varname).Get(&varname);

                // Matching primvar
                if (varname == primvarName)
                {
                    return child;
                }
            }
        }
    }

    TfToken readerName("PrimvarReader_" + primvarName.GetString());

    // If not found, then create a new primvar reader and connect it.
    SdfPath path = material.GetPath().AppendChild(readerName);

    UsdShadeShader readerShader = UsdShadeShader::Define(material.GetPrim().GetStage(), path);

    // Set the ID attr to the correct primvar reader type
    readerShader.CreateIdAttr().Set(readerId);

    // Create the varname attr and set the primvar name
    UsdShadeInput varnameInput = readerShader.CreateInput(_tokens->varname, SdfValueTypeNames->String);
    varnameInput.Set(primvarName.GetString());

    // Create the output and connect the input
    UsdShadeOutput output = readerShader.CreateOutput(_tokens->result, input.GetTypeName());
    targetInput.ConnectToSource(output);

    return readerShader.GetPrim();
}


// Struct to track groups of equivalent materials, and hashes of their shaders
struct MaterialInfo
{
    std::vector<UsdPrim> materials;
    std::unordered_map<size_t, UsdShadeShader> shaders;
};


// Custom includeAttr function
// For shaders, don't include the _value_ of an attribute in the hash if it
// is a type we can map to a primvar reader.
static bool includeAttr(const UsdAttribute& attr)
{
    // Only apply to shader prims
    if (!attr.GetPrim().IsA<UsdShadeShader>())
    {
        return true;
    }

    // If we don't have a map of this type to a primvar reader, then include it
    return !_isSupportedType(attr.GetTypeName());
}


/// Populates \p targets with any materials that have another material reference them
/// via the specializes composition arc. \p sources is populated with any materials that
/// specialize those targets.
///
/// There are materials in converted scenes that use this as a mechanism to work around
/// reference boundary issues, so we do not want to deduplicate those materials.
///
///
static void _findSpecializeTargets(const UsdStageWeakPtr& usdStage, std::set<SdfPath>& targets, std::set<SdfPath>* sources)
{

    std::mutex insertMutex;

    std::vector<UsdPrim> iterPrims = { usdStage->GetPseudoRoot() };
    tbbcompat::parallelForEach(iterPrims.begin(),
                               iterPrims.end(),
                               [&](const UsdPrim& prim, tbbcompat::Feeder<UsdPrim>& feeder)
                               {
                                   // Only need to check materials
                                   if (prim.IsA<UsdShadeMaterial>())
                                   {
                                       UsdPrimCompositionQuery compositionQuery(prim);
                                       for (const auto& arc : compositionQuery.GetCompositionArcs())
                                       {
                                           const PcpArcType& arcType = arc.GetArcType();
                                           if (arcType == PcpArcTypeSpecialize)
                                           {

                                               const SdfPath& targetPath = arc.GetTargetNode().GetPath();
                                               if (!targetPath.IsEmpty())
                                               {
                                                   std::lock_guard lock(insertMutex);
                                                   targets.insert(targetPath);

                                                   if (sources)
                                                   {
                                                       sources->insert(prim.GetPrimPath());
                                                   }
                                               }
                                           }
                                       }
                                   }
                                   else
                                   {
                                       // Don't need to recurse in to materials, so only add children if
                                       // not a material
                                       for (const auto& child : prim.GetAllChildren())
                                       {
                                           feeder.add(child);
                                       }
                                   }
                               });
}


// Iterate a stage, hashing materials in to groups of duplicates and recording which
// prims are bound to which materials.
static void _collectMaterialsAndBindings(const UsdStageWeakPtr& stage,
                                         const std::vector<UsdPrim>& prims,
                                         HashCache& hashCache,
                                         std::unordered_map<size_t, MaterialInfo>& materials,
                                         std::map<SdfPath, std::vector<UsdPrim>>& bound,
                                         std::map<SdfPath, std::vector<UsdPrim>>& directBound)
{

    // Map of material to its instanced boundary path
    std::map<UsdPrim, SdfPath> materialsToPaths;
    _findInstancedParents(stage, materialsToPaths);

    // Callback for a hashed material.
    // Groups materials with a matching hash.
    auto hashedFn = [&materials](const UsdPrim& prim, const size_t hash)
    {
        auto& info = materials[hash];
        info.materials.push_back(prim);
    };

    // Convert to set for faster lookup
    std::set<UsdPrim> _prims;
    _prims.insert(prims.begin(), prims.end());

    UsdPrimRange range(stage->GetPseudoRoot(), UsdPrimAllPrimsPredicate);
    for (auto it = range.begin(); it != range.end(); ++it)
    {
        const auto& prim = (*it);

        if (prim.IsPseudoRoot())
        {
            continue;
        }

        // Hash materials
        if (prim.IsA<UsdShadeMaterial>())
        {
            // Check whether this prim is included
            if (!_prims.empty())
            {
                if (_prims.find(prim) == _prims.end())
                {
                    continue;
                }
            }

            // Ignore prims that specialize something
            if (_hasSpecializes(prim))
            {
                it.PruneChildren();
                continue;
            }

            // Hash the material
            _hashMaterial(prim, materialsToPaths, &hashCache, includeAttr, hashedFn);

            // Prune children, no need to recurse deeper
            it.PruneChildren();
        }
        else
        {
            // For any other prim, check if it has a bound material.
            // We only check for direct bindings, as that's all we are going to rebind (i.e., we don't want
            // to add extra bindings to stuff that inherited a binding previously).
            UsdShadeMaterialBindingAPI bindingAPI(prim);

            auto directBind = bindingAPI.GetDirectBinding();
            if (directBind.GetMaterial())
            {
                // Record direct bindings.
                // This is what we will re-bind later.
                directBound[directBind.GetMaterialPath()].push_back(prim);

                // Also compute the actual bound material.
                // We may have a prim with a strongerThanDescendant binding, and then descendants that have
                // another direct binding (that isn't used, but exists). We need to avoid authoring primvar
                // values based on the child prims, as they will override the parent values. So, we track
                // the computed material as well - this is what we'll use to work out what attribute value
                // to author as primvars.
                bound[bindingAPI.ComputeBoundMaterial().GetPath()].push_back(prim);
            }
        }
    }
}


/// Base class for storing primvar data
class PrimvarDataBase
{
public:
    PrimvarDataBase() = default;
    virtual ~PrimvarDataBase() = default;

    /// Resize the member data, with an optional default value
    virtual void resize(size_t size, const VtValue& defaultValue) = 0;

    /// Set the specified value for each of the indices
    virtual void set(const VtIntArray& indices, const VtValue& value) = 0;

    /// Set the target typename
    virtual void setTypeName(const SdfValueTypeName& typeName)
    {
        m_typeName = typeName;
    }

    /// Author a primvar with the specified name on a mesh
    virtual void apply(const TfToken& name, const UsdGeomMesh& mesh) = 0;

protected:
    SdfValueTypeName m_typeName;
};


/// Templated primvar class to handle various data types
template <class T>
class PrimvarData : public PrimvarDataBase
{
private:
    T m_default;

public:
    explicit PrimvarData(const T& defaultValue)
        : m_default(defaultValue)
    {
    }

    void resize(size_t size, const VtValue& defaultValue) override
    {
        // We might have a default value, e.g. from a fallback material. We also may
        // not - in which case, we don't care, this should really just mean we don't
        // need those particular values as nothing will read them, so no drama (other
        // than ensuring it isn't just random memory, hence m_default).
        T _defaultValue = m_default;
        if (!defaultValue.IsEmpty())
        {
            _defaultValue = defaultValue.UncheckedGet<T>();
        }

        m_values.resize(size, _defaultValue);
    }


    void set(const VtIntArray& indices, const VtValue& value) override
    {
        const T& _value = value.UncheckedGet<T>();
        for (int index : indices)
        {
            m_values[index] = _value;
        }
    }


    void apply(const TfToken& name, const UsdGeomMesh& mesh) override
    {
        UsdGeomPrimvarsAPI primvarsAPI(mesh);

        // If all values are equal, then we can set this as a constant primvar with a scalar value
        if (std::adjacent_find(m_values.begin(), m_values.end(), std::not_equal_to<>()) == m_values.end())
        {
            UsdGeomPrimvar primvar = primvarsAPI.CreatePrimvar(name, m_typeName.GetScalarType(), UsdGeomTokens->constant);
            if (!m_values.empty())
            {
                primvar.Set(m_values[0]);
            }
        }
        else
        {
            // Else set the values as a uniform array
            UsdGeomPrimvar primvar = primvarsAPI.CreatePrimvar(name, m_typeName, UsdGeomTokens->uniform);
            primvar.Set(m_values);
        }
    }

private:
    VtArray<T> m_values;
};


using PrimvarDataBasePtr = std::shared_ptr<PrimvarDataBase>;


template <typename T>
static PrimvarDataBasePtr _getPrimvarDataTyped(const T& defaultValue)
{
    return std::make_shared<PrimvarData<T>>(defaultValue);
}


/// Get a base pointer that is created with the correct templated type, and some
/// "sensible" default value for unused indices.
static PrimvarDataBasePtr _getPrimvarData(const UsdShadeInput& input)
{
    const auto& typeName = input.GetTypeName();

    PrimvarDataBasePtr data = nullptr;

    if (typeName == SdfValueTypeNames->Color3f || typeName == SdfValueTypeNames->Float3)
    {
        data = _getPrimvarDataTyped<GfVec3f>(GfVec3f(0.0, 0.0, 0.0));
    }
    else if (typeName == SdfValueTypeNames->Color4f || typeName == SdfValueTypeNames->Float4)
    {
        data = _getPrimvarDataTyped<GfVec4f>(GfVec4f(0.0, 0.0, 0.0, 0.0));
    }
    else if (typeName == SdfValueTypeNames->Float)
    {
        data = _getPrimvarDataTyped<float>(0.0);
    }
    else if (typeName == SdfValueTypeNames->Float2)
    {
        data = _getPrimvarDataTyped<GfVec2f>(GfVec2f(0.0, 0.0));
    }

    // Need the typeName later to author.
    if (data)
    {
        data->setTypeName(typeName.GetArrayType());
    }

    return data;
}


// Typedefs
using DefaultValues = std::map<TfToken, VtValue>;
using PathToPrims = std::map<PXR_NS::SdfPath, std::vector<PXR_NS::UsdPrim>>;
using PrimToSubsets = std::map<PXR_NS::UsdPrim, std::vector<PXR_NS::UsdGeomSubset>>;


// Helper function, given a fallback material from a mesh that has subsets, extract
// the value of any shader attributes and store them in a map keyed on the input name.
// Only deals with "supported types" for conversion to primvar readers.
static void _getDefaultValues(const UsdPrim& prim, DefaultValues& defaultValues)
{
    UsdShadeMaterialBindingAPI bindingAPI(prim);
    auto material = bindingAPI.ComputeBoundMaterial();
    if (material)
    {
        for (const auto& child : material.GetPrim().GetAllChildren())
        {
            if (!child.IsA<UsdShadeShader>())
            {
                continue;
            }

            UsdShadeShader shader(child);

            for (const auto& input : shader.GetInputs())
            {
                if (_isSupportedType(input.GetTypeName()))
                {
                    VtValue val;
                    if (input.Get(&val))
                    {
                        defaultValues[input.GetBaseName()] = val;
                    }
                }
            }
        }
    }
}


static void _processSubsets(const PathToPrims& bound, PrimToSubsets& subsetPrims, const std::set<UsdPrim>& canDedupe)
{
    // First create a map of prims to their child GeomSubsets
    for (const auto& it : bound)
    {
        for (const auto& boundPrim : it.second)
        {
            UsdGeomSubset subset(boundPrim);
            if (subset)
            {
                // Quick sanity check that there are indices
                VtIntArray indices;
                subset.GetIndicesAttr().Get(&indices);

                if (!indices.empty())
                {
                    subsetPrims[boundPrim.GetParent()].push_back(subset);
                }
            }
        }
    }

    // For each subset, populate an array-typed primvar per shader input for the specified subset indices.
    for (const auto& [targetPrim, subsets] : subsetPrims)
    {
        // Assumes mesh for now
        UsdGeomMesh mesh(targetPrim);
        size_t faceCount = mesh.GetFaceCount(UsdTimeCode::Default());

        // Check for default values.
        // Essentially if there's a default/fallback material for faces that are not covered by subset
        // indices, then we will use that value to populate when resizing the primvar. If there is no
        // fallback it doesn't matter as they're unused values. If there is and it uses them, then
        // they will be correct.
        DefaultValues defaultValues;
        _getDefaultValues(targetPrim, defaultValues);

        // primvar name > array data
        std::map<TfToken, PrimvarDataBasePtr> primvars;

        for (const auto& subset : subsets)
        {
            VtIntArray subsetFaceIndices;
            subset.GetIndicesAttr().Get(&subsetFaceIndices);

            UsdShadeMaterialBindingAPI bindingAPI(subset);
            if (auto material = bindingAPI.ComputeBoundMaterial())
            {
                // First, we need to check whether we can actually dedupe this material.
                // If not, we don't want to copy its inputs as primvars.
                if (canDedupe.find(material.GetPrim()) == canDedupe.end())
                {
                    continue;
                }

                // Check each shader of the material.
                // For each one we want to find any of the types we can convert to primvar readers
                // and create arrays for them.
                for (const auto& child : material.GetPrim().GetAllChildren())
                {
                    if (!child.IsA<UsdShadeShader>())
                    {
                        continue;
                    }

                    UsdShadeShader shader(child);

                    for (const auto& input : shader.GetInputs())
                    {
                        if (!_isSupportedType(input.GetTypeName()))
                        {
                            continue;
                        }

                        // We can handle this, so get the value
                        VtValue val;
                        input.Get(&val);

                        PrimvarDataBasePtr primvarData = nullptr;
                        auto insertIt = primvars.insert(std::make_pair(input.GetBaseName(), nullptr));
                        if (insertIt.second)
                        {
                            // First time we've seen this input. Create a primvar data object
                            // and resize it
                            primvarData = _getPrimvarData(input);

                            // We may have a default value available to use for the resize
                            VtValue value;
                            auto findDefaultIt = defaultValues.find(input.GetBaseName());
                            if (findDefaultIt != defaultValues.end())
                            {
                                value = findDefaultIt->second;
                            }

                            // Resize, then replace the default nullptr in the map
                            primvarData->resize(faceCount, value);
                            insertIt.first->second = primvarData;
                        }
                        else
                        {
                            // Previously seen, just grab it
                            primvarData = insertIt.first->second;
                        }

                        // We can now set the value from this input for the subset indices
                        primvarData->set(subsetFaceIndices, val);
                    }
                }
            }
        }

        // Found everything, so we can author the primvars on the actual mesh
        // The material creation will happen later, and we'll skip the value setting later as we did it here.
        {
            SdfChangeBlock _changeBlock;
            for (const auto& it : primvars)
            {
                it.second->apply(it.first, mesh);
            }
        }
    }
}


bool OptimizeMaterialsOperation::convertToPrimvars(const std::vector<UsdPrim>& prims) const
{
    // Add a scoped asset resolver cache to improve performance querying asset path types,
    // in particular when counting unique materials that may have a source asset.
    ArResolverScopedCache resolverScopedCache;

    // map of material > bound prims
    PathToPrims bound;
    PathToPrims directBound;

    // material hash > matching materials
    std::unordered_map<size_t, MaterialInfo> materialInfo;

    HashCache hashCache;
    const auto& stage = getUsdStage();

    // Get targets of specializes arcs
    std::set<SdfPath> specializeTargets;
    std::set<SdfPath> specializeSources;
    _findSpecializeTargets(stage, specializeTargets, &specializeSources);

    // Collect the duplicate materials and prim material bindings.
    _collectMaterialsAndBindings(stage, prims, hashCache, materialInfo, bound, directBound);

    // We need a set of which materials can actually be deduped, for processing subsets below.
    std::set<UsdPrim> canDedupe;
    for (auto& materialGroupIt : materialInfo)
    {
        auto& [materials, shaders] = materialGroupIt.second;

        // Skip if there is only one material, nothing to dedupe
        if (materials.size() <= 1)
        {
            continue;
        }

        bool canAdd = true;

        // Filter materials with specializes arc
        for (const auto& material : materials)
        {
            // Skip if this material is part of a specializes arc
            const auto& path = material.GetPrimPath();
            if (specializeTargets.find(path) != specializeTargets.end() ||
                specializeSources.find(path) != specializeSources.end())
            {
                canAdd = false;
                break;
            }
        }

        if (canAdd)
        {
            // Insert all, these are fine.
            canDedupe.insert(materials.begin(), materials.end());
        }
    }

    // Basic materials are fine to convert. Subsets are more complicated - we can't author primvars
    // on the subsets, as this is not supported. We have to therefore author primvars on the parent
    // mesh instead, which requires a bunch of extra work.  We do this all first, and we can continue
    // as usual later creating/binding the new materials, and just skip the value setting for these
    // prims at that point.
    PrimToSubsets subsetPrims;
    _processSubsets(bound, subsetPrims, canDedupe);

    std::vector<UsdPrim> oldMaterials;

    // Process material groups
    for (auto& materialGroupIt : materialInfo)
    {
        auto& [materials, shaders] = materialGroupIt.second;

        // Skip if there is only one material, nothing to dedupe
        if (materials.size() <= 1)
        {
            continue;
        }

        // Copy one of the materials to a new material, which will be the single
        // one we will retarget and modify.
        auto& firstMat = materials.front();

        // Skip if this material is part of a specializes arc
        if (specializeTargets.find(firstMat.GetPrimPath()) != specializeTargets.end())
        {
            continue;
        }

        UsdPrim newMaterialPrim = _createNewMaterial(firstMat, m_materialsPath);
        if (!newMaterialPrim)
        {
            continue;
        }

        UsdShadeMaterial newMaterial(newMaterialPrim);

        // After creating the material, loop over the child shaders and hash them.
        // This means we can use the hash of the shaders we are collapsing to match their
        // equivalent new shader.
        for (const auto& child : UsdPrimRange(newMaterial.GetPrim()))
        {
            if (child.IsA<UsdShadeShader>())
            {
                size_t hash = _hashPrim(stage, child, hashCache, includeAttr);
                shaders[hash] = UsdShadeShader(child);
            }
        }

        bool first = true;

        // Next, for each of the old materials/shaders we want to make sure we have a primvar reader
        // for any of the relevant inputs, and then author the input values as primvars on the bound
        // prims.
        for (const auto& materialPrim : materials)
        {
            for (const auto& child : materialPrim.GetAllChildren())
            {
                if (!child.IsA<UsdShadeShader>())
                {
                    continue;
                }

                // Each of the individual shader prims would have already been hashed as part of hashing
                // the original materials, so we can just do a lookup.
                auto hashIt = hashCache.find(child);
                if (hashIt == hashCache.end())
                {
                    SO_LOG_WARN("Shader %s missing hash", child.GetPrimPath().GetAsString().c_str());
                    continue;
                }

                // Using the shader hash, we can find the equivalent new shader - this is where we
                // want to author primvar readers and connect them up.
                auto newShaderIt = shaders.find(hashIt->second);
                if (newShaderIt == shaders.end())
                {
                    SO_LOG_WARN("Couldn't find new shader for %s", child.GetPrimPath().GetAsString().c_str());
                    continue;
                }

                UsdShadeShader oldShader(child);
                UsdShadeShader newShader = newShaderIt->second;

                for (const auto& input : oldShader.GetInputs())
                {
                    auto typeIt = s_typeToReader.find(input.GetTypeName());

                    // Only process the relevant types
                    if (typeIt == s_typeToReader.end())
                    {
                        continue;
                    }

                    const auto& name = input.GetBaseName();

                    UsdShadeInput targetInput = newShader.GetInput(name);
                    if (!targetInput)
                    {
                        SO_LOG_WARN("Couldn't find shader input %s", name.GetText());
                        continue;
                    }

                    // Clear any old value. This would have come from one of the original shaders
                    // (whichever one we happened to copy to create the new one) but not all the shaders
                    // would necessarily have the same value for it. Looks a bit weird to keep an arbitrary
                    // one of these values as a fallback, so clearing it is nicer.
                    if (targetInput.GetAttr().HasValue())
                    {
                        targetInput.GetAttr().Clear();
                    }

                    // Ensure there is a primvar reader for this primvar.
                    // Seeing as we require that materials match other than shader attribute *values*, we
                    // know the first material will have all the required inputs we are going to
                    // create as primvar readers - so we only need to create these from the first
                    // material.
                    if (first)
                    {
                        _createPrimvarReader(newMaterial, typeIt->second, input, targetInput, name);
                    }

                    // Get the value of this input, we need to apply it as a primvar to any bound prims.
                    // Note: we may find an already-connected input (e.g., an existing primvar reader)
                    // so it's possible there is no value.
                    VtValue value;
                    input.Get(&value);

                    if (!value.IsEmpty())
                    {
                        // For each prim that was bound to this material, set the input value as a primvar.
                        auto findIt = bound.find(materialPrim.GetPrimPath());
                        if (findIt != bound.end())
                        {
                            for (const auto& boundPrim : findIt->second)
                            {
                                // Skip subsets - these were processed earlier. Also skip meshes that _had_ one
                                // of these subsets, as we also processed them.
                                if (boundPrim.IsA<UsdGeomSubset>() || subsetPrims.find(boundPrim) != subsetPrims.end())
                                {
                                    continue;
                                }

                                UsdGeomPrimvarsAPI papi(boundPrim);
                                UsdGeomPrimvar primvar = papi.CreatePrimvar(name, input.GetTypeName());
                                primvar.Set(value);
                            }
                        }
                    }
                }
            }

            // Now we have the new deduplicated material, rebind any prims that were using the old material.
            auto findIt = directBound.find(materialPrim.GetPrimPath());
            if (findIt != directBound.end())
            {
                // Rebind each prim.
                // Note we only looked for direct bindings earlier - we know the prims will have an existing
                // relationship and we just need to reset the targets.
                for (const auto& boundPrim : findIt->second)
                {
                    boundPrim.GetRelationship(UsdShadeTokens->materialBinding).SetTargets({ newMaterialPrim.GetPrimPath() });
                }

                oldMaterials.push_back(materialPrim);
            }

            // Minor optimization, so we only need to create the primvar readers on the first
            // material.
            first = false;
        }

        SO_LOG_INFO("Created new material %s", newMaterialPrim.GetPrimPath().GetText());

        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE("Replaces duplicates:");
            for (const auto& materialPrim : materials)
            {
                SO_LOG_VERBOSE("%s", materialPrim.GetPrimPath().GetText());
            }
        }
    }

    // Delete the old materials
    if (!oldMaterials.empty())
    {
        _deleteMaterials(stage, oldMaterials);
    }

    return true;
}


bool OptimizeMaterialsOperation::optimizeMaterials(const std::vector<UsdPrim>& prims)
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|OptimizeMaterialsOperation|optimizeMaterials");

    // Set of materials to consider for deduplication.
    std::set<UsdPrim> primSet(prims.begin(), prims.end());

    // Some m_materialModes will create new material bindings.
    bool willBindMaterials = (m_mode != OptimizeMaterialMode::eConvertToColor);

    // Add a scoped asset resolver cache to improve performance querying asset path types,
    // in particular when counting unique materials that may have a source asset.
    ArResolverScopedCache resolverScopedCache;

    // Material hash to path
    std::map<size_t, SdfPath> materials;
    HashCache hashCache;

    // Material path > color
    std::map<SdfPath, ColorValue> materialPathToColor;

    // Current material path and prims bound to it
    std::map<SdfPath, std::vector<UsdPrim>> bound;

    // Duplicate material to first instance of a material, this controls what will be rebound
    std::map<SdfPath, SdfPath> rebind;

    // Map of material to its instanced boundary path
    std::map<UsdPrim, SdfPath> materialsToPaths;

    // Materials that are the source or target of a specializes arc
    std::set<SdfPath> specializeTargets;

    // If we are removing duplicates (i.e. rebinding the duplicates) then we need to do a first pass of the stage
    // to work out any boundaries around which we can't merge (currently instances). We need this done up front
    // so that when we iterate materials later and find references we can jump to the source but have this path
    // already determined to include in the hash. This lets us hash source materials rather than references of
    // materials *first*, which is necessary when rebinding meshes within references.
    //
    // Likewise, we need to find the targets of any specialize relationships so that we can avoid removing them.
    // There are scenes that use specializes to "copy" materials to handle reference boundaries, so we have to
    // be careful to leave them in place.
    if (willBindMaterials)
    {
        _findInstancedParents(getUsdStage(), materialsToPaths);

        _findSpecializeTargets(getUsdStage(), specializeTargets, nullptr);
    }


    // Callback when a material is hashed
    auto hashedFn = [&](const UsdPrim& prim, size_t hash)
    {
        // Cannot de-dupe something that is the target of a specialize relationship.
        if (specializeTargets.find(prim.GetPrimPath()) != specializeTargets.end())
        {
            return;
        }

        // Insert the new material. If it inserted then it is unique. If not, we can add a remapping for
        // this to the existing one as it is a duplicate.
        auto insertIt = materials.insert(std::make_pair(hash, prim.GetPrimPath()));
        if (!insertIt.second)
        {
            if (insertIt.first->second != prim.GetPrimPath())
            {
                rebind[prim.GetPrimPath()] = insertIt.first->second;
                if (getContext()->verbose)
                {
                    std::ostringstream oss;
                    oss << prim.GetPrimPath() << " is a duplicate of " << insertIt.first->second;
                    SO_LOG_VERBOSE(oss.str().c_str());
                }
            }
        }
    };

    // We need to find the duplicate materials, but also all the prims that may use those duplicates so that
    // we can rebind them. Therefore, we need to do another full iteration of the stage.
    UsdPrimRange range(getUsdStage()->GetPseudoRoot(), UsdPrimAllPrimsPredicate);
    for (auto it = range.begin(); it != range.end(); ++it)
    {
        const auto& prim = (*it);

        // Skip Pseudo Root
        if (prim.GetPath().IsAbsoluteRootPath())
        {
            continue;
        }

        // Hash any materials that are found. Record any duplicates as well.
        if (prim.IsA<UsdShadeMaterial>())
        {
            // If there is a prim filter then skip materials that do not match
            if (!primSet.empty())
            {
                if (primSet.find(prim) == primSet.end())
                {
                    continue;
                }
            }

            // Depending on the current operation, do stuff
            if (m_mode == OptimizeMaterialMode::eConvertToColor)
            {
                // Check if we can convert this material to a simple color. If so, store it.
                UsdShadeMaterial material(prim);
                ColorValue materialColor;
                if (_getMaterialAlbedo(material, materialColor))
                {
                    materialPathToColor[prim.GetPrimPath()] = materialColor;
                }
            }
            else
            {
                _hashMaterial(prim, materialsToPaths, &hashCache, nullptr, hashedFn);
            }

            // Prune children, no need to check child shaders
            it.PruneChildren();
        }
        else
        {
            // For any other prim, check if it has a bound material.
            UsdShadeMaterialBindingAPI bindingAPI(prim);

            // Check for a direct bind. We don't want to rebind child prims that don't have a direct binding,
            // or we end up binding more prims that inherit a material and didn't originally have a direct
            // binding.
            //
            // TODO: Deal with collection bindings
            auto directBind = bindingAPI.GetDirectBinding();

            if (directBind.GetMaterial())
            {
                bound[directBind.GetMaterialPath()].push_back(prim);
            }
        }
    }

    // For each duplicate material rebind any prims that use it
    std::vector<UsdPrim> materialsToDelete;

    // If we found colors then set the color on the bound prims and mark the materials to be deleted.
    if (!materialPathToColor.empty())
    {

        for (const auto& it : materialPathToColor)
        {

            const ColorValue& materialColor = it.second;

            auto findIt = bound.find(it.first);
            if (findIt != bound.end())
            {

                VtArray<GfVec3f> colorValue{ materialColor.color };
                VtArray<float> opacityValue{ materialColor.opacity };

                bool considerOpacity = materialColor.opacity != 1.0;

                for (const auto& prim : findIt->second)
                {
                    // Set display color primvar
                    UsdGeomGprim gprim(prim);
                    UsdGeomPrimvar displayColorPrimvar = gprim.CreateDisplayColorPrimvar(UsdGeomTokens->constant);
                    // Force the interpolation to constant in case it already existed with a different value.
                    displayColorPrimvar.SetInterpolation(UsdGeomTokens->constant);
                    displayColorPrimvar.Set(colorValue);

                    // Create/set a displayOpacity primvar if opacity is non-opaque
                    if (considerOpacity)
                    {
                        UsdGeomPrimvar displayOpacityPrimvar = gprim.CreateDisplayOpacityPrimvar(UsdGeomTokens->constant);
                        displayOpacityPrimvar.SetInterpolation(UsdGeomTokens->constant);
                        displayOpacityPrimvar.Set(opacityValue);
                    }

                    // Remove material binding
                    UsdRelationship relationship = prim.GetRelationship(UsdShadeTokens->materialBinding);
                    relationship.ClearTargets(true);

                    // The materials may be bound to something that is intended to be inherited, so we need to also
                    // traverse the children and block any primvars that might override this value.
                    auto primRange = UsdPrimRange(prim);
                    if (!primRange.empty())
                    {
                        // Ignore the original prim, only care about its children
                        primRange.increment_begin();

                        for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
                        {

                            const auto& child = (*iter);

                            // It's possible the material lives under the prim which it's bound to. Avoid authoring
                            // unnecessary extra blocks.
                            //
                            // Example scene this is based on was:
                            //
                            // xform (material binding > xform/material)
                            //   - material
                            //   - mesh
                            if (child.IsA<UsdShadeMaterial>())
                            {
                                iter.PruneChildren();
                                continue;
                            }

                            UsdGeomPrimvarsAPI papi(child);

                            UsdGeomPrimvar childDisplayColor = papi.GetPrimvar(UsdGeomTokens->primvarsDisplayColor);
                            if (childDisplayColor.HasAuthoredValue())
                            {
                                childDisplayColor.GetAttr().Block();
                            }

                            // Only check for opacity if we applied a value, otherwise leave it as is.
                            if (considerOpacity)
                            {
                                UsdGeomPrimvar childOpacity = papi.GetPrimvar(UsdGeomTokens->primvarsDisplayOpacity);
                                if (childOpacity.HasAuthoredValue())
                                {
                                    childOpacity.GetAttr().Block();
                                }
                            }
                        }
                    }
                }
            }

            // Mark for delete
            materialsToDelete.push_back(getUsdStage()->GetPrimAtPath(it.first));
        }
    }
    else if (!rebind.empty())
    {
        // If we found duplicates we want to rebind, then rebind them now.
        materialsToDelete.reserve(rebind.size());

        SdfChangeBlock changeBlock;

        for (const auto& it : rebind)
        {
            auto findIt = bound.find(it.first);
            if (findIt != bound.end())
            {
                for (const auto& prim : findIt->second)
                {
                    // Manually create target relationship (this is substantially faster than using Bind())
                    prim.CreateRelationship(UsdShadeTokens->materialBinding).SetTargets({ it.second });
                }
            }

            // Queue the duplicate material for delete. Note that this will delete the duplicate regardless of whether
            // anything was bound to it, so that it also cleans up unused duplicates.
            materialsToDelete.push_back(getUsdStage()->GetPrimAtPath(it.first));
        }

        std::ostringstream oss;
        std::string suffix = rebind.size() == 1 ? "" : "s";
        oss << "Rebound " << rebind.size() << " duplicate material" << suffix << ".";
        SO_LOG_INFO(oss.str().c_str());
    }
    else
    {
        SO_LOG_INFO("Did not find any prims with duplicate materials.");
    }

    // Delete the now-unused duplicate materials
    if (!materialsToDelete.empty())
    {
        _deleteMaterials(getUsdStage(), materialsToDelete);

        std::ostringstream oss;
        std::string suffix = materialsToDelete.size() == 1 ? "" : "s";
        oss << "Removed " << materialsToDelete.size() << " unused duplicate material" << suffix << ".";
        SO_LOG_INFO(oss.str().c_str());
    }
    else
    {
        SO_LOG_INFO("No unused duplicate materials to remove.");
    }
    return true;
}


OperationResult OptimizeMaterialsOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|OptimizeMaterialsOperation|ExecuteAnalysis");

    // Scoped Resolver
    ArResolverScopedCache parentResolverScopedCache;

    // Map of material to its instanced boundary path
    std::map<UsdPrim, SdfPath> materialsToBoundaries;
    _findInstancedParents(getUsdStage(), materialsToBoundaries);

    // Any materials that are part of a specializes arc
    std::set<SdfPath> specializeSources;
    std::set<SdfPath> specializeTargets;
    _findSpecializeTargets(getUsdStage(), specializeTargets, &specializeSources);

    struct DuplicateInfo
    {
        std::map<size_t, std::vector<UsdPrim>> materials;
        std::mutex mutex;

        void add(size_t hash, const UsdPrim& prim)
        {
            std::lock_guard<std::mutex> lock(mutex);
            materials[hash].push_back(prim);
        }
    };

    DuplicateInfo duplicates;
    DuplicateInfo duplicatePrimvars;

    // Callback for materials that match other than primvar values
    auto hashedPrimvarsFn = [&duplicatePrimvars](const UsdPrim& prim, const size_t hash)
    { duplicatePrimvars.add(hash, prim); };

    // Per-thread hashCaches.
    tbb::enumerable_thread_specific<HashCache> hashCache;
    tbb::enumerable_thread_specific<HashCache> hashCachePrimvars;

    // Callback when a material is hashed
    auto hashedFn = [&](const UsdPrim& prim, const size_t hash)
    {
        // As in other parts of the code, anything that is the target of a specializes (i.e. is actually
        // being specialized in the scene) cannot be deduplicated.
        if (specializeTargets.find(prim.GetPrimPath()) != specializeTargets.end())
        {
            return;
        }

        duplicates.add(hash, prim);

        if (m_analysisCheckPrimvars)
        {
            // We're in the callback for a prim (material) being hashed. At this point we can call _hashMaterial
            // again, for checking deduplication with primvars, and as an optimization we can avoid the expensive
            // composition arc query as that's been handled in order for this callback to trigger in the first place.
            // Note: separate cache from the main call (since the hashing is different this time)
            constexpr bool checkArcs = false;
            _hashMaterial(prim, materialsToBoundaries, &hashCachePrimvars.local(), includeAttr, hashedPrimvarsFn, checkArcs);
        }
    };

    std::vector<UsdPrim> iterPrims = { getUsdStage()->GetPseudoRoot() };
    tbbcompat::parallelForEach(
        iterPrims.begin(),
        iterPrims.end(),
        [&](const UsdPrim& prim, tbbcompat::Feeder<UsdPrim>& feeder)
        {
            // Queue all children for traversal via parallel_for_each
            // Skip children of Materials though - we don't need to check shaders etc.
            if (!prim.IsA<UsdShadeMaterial>())
            {
                const auto& children = prim.GetAllChildren();
                for (const auto& child : children)
                {
                    feeder.add(child);
                }
            }

            // Skip Pseudo Root
            if (prim.GetPath().IsAbsoluteRootPath())
            {
                return;
            }

            // Skip inactive prims
            if (!prim.IsActive())
            {
                return;
            }

            // Thread-specific child resolver cache
            ArResolverScopedCache resolverScopedCache(&parentResolverScopedCache);

            // Hash any materials that are found to collect duplicates.
            if (prim.IsA<UsdShadeMaterial>())
            {
                // If this material specializes another, it can't be considered a duplicate.
                if (specializeSources.find(prim.GetPrimPath()) != specializeSources.end())
                {
                    return;
                }

                // Default material hash
                constexpr bool checkArcs = true;
                _hashMaterial(prim, materialsToBoundaries, &hashCache.local(), nullptr, hashedFn, checkArcs);
            }
        });

    // Prepare result
    JsObject analysisResult;

    JsArray resultDuplicates;
    for (const auto& [hash, materials] : duplicates.materials)
    {
        if (materials.size() >= 2)
        {
            resultDuplicates.emplace_back(_toJson(materials));
        }
    }

    analysisResult["duplicates"] = resultDuplicates;

    // Optional collection, as it adds double the hashing.
    if (m_analysisCheckPrimvars)
    {
        JsArray resultDuplicatePrimvars;
        for (const auto& [hash, materials] : duplicatePrimvars.materials)
        {
            if (materials.size() >= 2)
            {
                resultDuplicatePrimvars.emplace_back(_toJson(materials));
            }
        }

        analysisResult["duplicatePrimvars"] = resultDuplicatePrimvars;
    }

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    if (getContext()->verbose)
    {
        SO_LOG_INFO("Analysis Result: %s", result.output);
    }

    return result;
}


} // namespace omni::scene::optimizer
