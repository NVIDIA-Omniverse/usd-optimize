// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "FlattenHierarchy.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/Log.h>

// USD
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>

// C++
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::FlattenHierarchyOperation);


namespace omni::scene::optimizer
{

constexpr const char* s_category = "FLATTEN";

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((flattenHierarchyOp, "flattenHierarchyOp"))
    (Xform)
);
// LCOV_EXCL_STOP
// clang-format on

static GfMatrix4d s_identity;

FlattenHierarchyOperation::FlattenHierarchyOperation()
    : Operation("flattenHierarchy", "Flatten Hierarchy", "Remove redundant Xforms from a stage, to reduce prim count")
{

    addArgument("paths", "Paths To Process", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_primPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("identity",
                "Identity Only",
                kDisplayTypeBool,
                "Only remove Xforms that do not contribute any transformation values to the hierarchy",
                m_identityOnly);
}


std::string FlattenHierarchyOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion FlattenHierarchyOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string FlattenHierarchyOperation::getCategory() const
{
    return s_category;
}


std::string FlattenHierarchyOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


static bool _isXform(const UsdPrim& prim)
{
    return prim.GetTypeName() == _tokens->Xform;
}


static bool _hasTimesamples(const UsdPrim& prim)
{
    return UsdGeomXformable(prim).TransformMightBeTimeVarying();
}


static bool _hasXformData(const UsdPrim& prim)
{

    UsdGeomXformable xformable(prim);

    bool resetXformStack;
    const auto& ops = xformable.GetOrderedXformOps(&resetXformStack);

    // If this prim resets the xform stack, it does something.
    if (resetXformStack)
    {
        return true;
    }

    // If there are no other xform ops, then it doesn't do anything.
    if (ops.empty())
    {
        return false;
    }

    // Check the local transform. If it is identity, then this prim
    // doesn't contribute anything.
    GfMatrix4d localTransform;
    xformable.GetLocalTransformation(&localTransform, &resetXformStack);

    if (localTransform == s_identity)
    {
        return false;
    }

    return true;
}


/// Certain data needs to be preserved when modifying the hierarchy
/// This struct holds it so we can apply it back after moving things
struct OriginalData
{
    GfMatrix4d matrix;
    TfToken visible = UsdGeomTokens->visible;
    bool resetXformStack = false;
};


/// Simplify tracking a bunch of stuff between functions
struct FlattenData
{
    SdfBatchNamespaceEdit edits;
    std::set<SdfPath> toRemove;
    UsdGeomXformCache xformCache;
    std::vector<std::pair<SdfPath, OriginalData>> originalData;

    std::set<SdfPath> references;
    std::set<SdfPath> internalReferenced;
    std::set<SdfPath> relationships;

    // Original Material Path > New Material Path
    std::map<SdfPath, SdfPath> materialMap;

    // Original Prim Path > Original Bound Material Path
    std::map<SdfPath, SdfPath> boundPrims;

    // Original Prim Path > New Prim Path
    std::map<SdfPath, SdfPath> renamedBoundPrims;

    std::set<SdfPath> seen;

    bool identityOnly = false;

    /// Analysis mode: populate ``toRemove`` for reporting but skip the
    /// SdfBatchNamespaceEdit / xform-cache / material-rebinding work that
    /// the executing path needs.
    bool analysisOnly = false;


    bool isReference(const UsdPrim& prim) const
    {
        if (references.count(prim.GetPrimPath()))
        {
            return true;
        }

        return false;
    }


    bool handleMaterials() const
    {
        if (materialMap.empty() && boundPrims.empty())
        {
            return false;
        }

        return true;
    }


    bool canRemove(const UsdPrim& prim) const
    {
        const auto& path = prim.GetPrimPath();

        if (path.IsAbsoluteRootPath())
        {
            return false;
        }

        // If identityOnly has been specified, then don't remove anything that contributes
        // to the transform hierarchy
        if (identityOnly && _hasXformData(prim))
        {
            return false;
        }

        // If this prim is referenced by something else internally, then we cannot modify it.
        // If we did, the thing referencing it would now be pointing at the old location. In
        // some cases the reference could be updated easily, in others it's more difficult.
        // For now we just don't mess with references.
        if (internalReferenced.count(path))
        {
            return false;
        }

        // Same with time samples, we don't want to remove this xform and have to rewrite them
        // elsewhere.
        if (_hasTimesamples(prim))
        {
            return false;
        }

        // If this xform has a direct material binding on it, then leave it. The prim itself may
        // end up being re-parented due to its ancestors but that's fine. We just want to ensure
        // that we don't remove anything that affects what material descendant prims end up with.
        const auto& relationship = prim.GetRelationship(UsdShadeTokens->materialBinding);
        if (relationship && relationship.HasAuthoredTargets())
        {
            // Having authored targets doesn't account for a clear, so check the actual
            // targets.
            SdfPathVector targets;
            relationship.GetTargets(&targets);

            if (!targets.empty())
            {
                return false;
            }
        }

        // Anything that is the target of a relationship can't be removed, as we'd need to update
        // the paths targeting it.
        // Likewise, anything that HAS a relationship can't - we don't want to mess with the
        // relationship hierarchy. We do however special case material bindings, so this is
        // for anything else.
        if (relationships.count(path))
        {
            return false;
        }

        return true;
    }
};


void FlattenHierarchyOperation::reparent(const SdfPath& from, const SdfPath& to, FlattenData& flattenData) const
{

    const auto& stage = getUsdStage();
    const UsdPrim& prim = stage->GetPrimAtPath(from);

    // Analysis mode only needs the chain-of-removal records (so the caller
    // can report which Xforms would be flattened); skip building the actual
    // namespace edits, xform-cache snapshots, and material rebinding bookkeeping.
    if (flattenData.analysisOnly)
    {
        SdfPath _from = from.GetPrimPath();
        while (_from != to && !_from.IsAbsoluteRootPath())
        {
            flattenData.toRemove.insert(_from);
            _from = _from.GetParentPath();
        }
        return;
    }

    // Reparent all the children of this prim to the target.
    const auto& children = prim.GetFilteredChildren(UsdPrimAllPrimsPredicate);
    for (const auto& child : children)
    {
        // Always check whether a prim already exists with this name.
        SdfPath targetPath = to.AppendChild(child.GetName());
        if (stage->GetPrimAtPath(targetPath))
        {
            flattenData.seen.insert(targetPath);
        }

        // We are reparenting this child to another parent that only has one child. We are going to remove that
        // prim, but we can't until everything is reparented. Therefore, we must be careful not to use the name
        // of that existing prim. We append a "unique" token to avoid that.
        // Note that by doing this we might end up using the name of a subsequent sibling that we then reparent,
        // so that needs to be tracked. We may also have already reparented something that would match ".._1"
        // so we also need to cover that edge case.
        int counter = 1;
        SdfPath _targetPath = targetPath;
        const std::string& name = targetPath.GetName();

        while (flattenData.seen.count(_targetPath))
        {
            _targetPath = to.AppendChild(TfToken(name + "_" + std::to_string(counter++)));
        }

        // If these variables don't match it means we had to change the name, so we will do
        // a reparent and rename.
        // Otherwise we can just reparent as we know there's no prim that exists there.
        if (_targetPath != targetPath)
        {
            flattenData.seen.insert(_targetPath);
            flattenData.edits.Add(SdfNamespaceEdit::ReparentAndRename(child.GetPrimPath(),
                                                                      to,
                                                                      _targetPath.GetNameToken(),
                                                                      SdfNamespaceEdit::AtEnd));
            if (getContext()->verbose)
            {
                SO_LOG_VERBOSE("Reparent and rename %s to %s",
                               child.GetPrimPath().GetAsString().c_str(),
                               _targetPath.GetAsString().c_str());
            }
        }
        else
        {
            flattenData.edits.Add(SdfNamespaceEdit::Reparent(child.GetPrimPath(), to, SdfNamespaceEdit::AtEnd));
            if (getContext()->verbose)
            {
                SO_LOG_VERBOSE("Reparent %s to %s", child.GetPrimPath().GetAsString().c_str(), to.GetAsString().c_str());
            }
        }

        // Now that we have figured out where to re-parent this prim, we want to check each of its children.
        // If there are any materials, or prims bound to materials, we need to track their original path vs
        // their new location. This will let us fix the materials and/or bindings later, once the edits have
        // all been applied to the stage.
        if (flattenData.handleMaterials())
        {
            auto primRange = UsdPrimRange(child);
            for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
            {
                UsdShadeMaterial material(*iter);
                if (material)
                {
                    // If this material is in our map then adjust its target location. Note that it may have already
                    // been re-parented due to a descendant, so be careful to replace the already updated entry, not
                    // the original path.
                    auto findIt = flattenData.materialMap.find(iter->GetPrimPath());
                    if (findIt != flattenData.materialMap.end())
                    {
                        findIt->second = findIt->second.ReplacePrefix(child.GetPrimPath(), _targetPath);
                    }

                    // Since this is a material we assume there are no more materials underneath it that we need
                    // to check for.
                    iter.PruneChildren();
                }
                else
                {
                    // Is this prim bound to a material?
                    // If so we need to know where it's going to end up so that we can rebind it later.
                    auto findIt = flattenData.boundPrims.find(iter->GetPrimPath());
                    if (findIt != flattenData.boundPrims.end())
                    {
                        // As above for materials, this prim may have already been re-parented due to a descendant, so
                        // we need to update the already updated path in that case. Hence we insert based on the
                        // original material path (findIt->first), and if that fails, we have an iterator to the interim
                        // path (findIt->second) that we can update.
                        auto insertIt = flattenData.renamedBoundPrims.emplace(
                            findIt->first,
                            findIt->first.ReplacePrefix(child.GetPrimPath(), _targetPath));
                        if (!insertIt.second)
                        {
                            insertIt.first->second =
                                insertIt.first->second.ReplacePrefix(child.GetPrimPath(), _targetPath);
                        }
                    }
                }
            }
        }
    }

    OriginalData originalData;

    // Now we need to cache the original world matrix.
    // Depending on where it moves, we may need to apply a new transform to compensate.
    originalData.matrix = flattenData.xformCache.GetLocalToWorldTransform(prim);

    // Also compute and cache visibility
    // Could potentially be a bit smarter and only check up to the target here, but
    // for now, this will do
    UsdGeomImageable imageable(prim);
    if (imageable)
    {
        originalData.visible = imageable.ComputeVisibility();
    }

    // Having queued a reparent, we can queue removals for the unnecessary interim xforms.
    SdfPath _from = from.GetPrimPath();
    SdfPath _originalFrom = _from;

    while (_from != to && !_from.IsAbsoluteRootPath())
    {
        flattenData.edits.Add(SdfNamespaceEdit::Remove(_from));

        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE("Removing %s", _from.GetAsString().c_str());
        }

        // If any prim being removed resets the xform stack, we want to record that.
        // We can push this flag up to the target, meaning we respect the intent of
        // the xform hierarchy.
        const UsdPrim& fromPrim = getUsdStage()->GetPrimAtPath(from);
        UsdGeomXformable xformable(fromPrim);
        if (xformable.GetResetXformStack())
        {
            originalData.resetXformStack = true;
        }

        // Track any prim that is being removed
        // This lets us avoid trying to reparent prims we would just be removing anyway.
        flattenData.toRemove.insert(_from);

        _from = _from.GetParentPath();
    }

    // Track the original data we need vs the new path.
    flattenData.originalData.emplace_back(to, originalData);

    // As we process from the deepest first, we may now be reparenting a prim that would cause the
    // path of some previous prim to be updated (by removing another interim xform from higher
    // up). We can compensate by removing the prefix here, giving us a correct list of the
    // new prim paths at the end.
    for (auto& it : flattenData.originalData)
    {
        if (it.first.HasPrefix(_originalFrom))
        {
            it.first = it.first.ReplacePrefix(_originalFrom, _from);
        }
    }
}


void FlattenHierarchyOperation::traversePrim(const UsdPrim& prim, const UsdPrim& target, FlattenData& flattenData) const
{
    const auto& children = prim.GetFilteredChildren(UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate));
    std::vector<UsdPrim> _children;
    _children.insert(_children.begin(), children.begin(), children.end());

    // Default to the target that was passed in, e.g. for chains of xforms that can be collapsed.
    UsdPrim _target = target;

    bool isXform = _isXform(prim);

    // If there is no currently valid target, and this is an xform, reparent things here.
    if (!_target.IsValid() && isXform)
    {
        _target = prim;
    }

    // When recursing, we can potentially use the current target for any children (e.g. when there are
    // multiple redundant xforms). However in certain conditions, we would reset to this prim.
    UsdPrim childTarget = _target;

    // Check various conditions that prevent us from removing this prim.
    if (!flattenData.canRemove(prim))
    {
        // No target here, no reparenting.
        _target = UsdPrim();
        // For any children, reset the target to themselves. If this is referenced, it's
        // fine to still tidy up things _under_ this point.
        childTarget = prim;
    }

    // If we have more than one child, then do not reparent there.
    if (_children.size() > 1)
    {
        childTarget = UsdPrim();
    }

    // Recurse through each child.
    for (const auto& child : _children)
    {
        // Skip overs
        if (child.GetSpecifier() != SdfSpecifierDef)
        {
            continue;
        }

        // Skip internal/external references.
        // For external we can't reparent from within. For internal, they'll get tidied if/when
        // we iterate the target.
        if (flattenData.isReference(child))
        {
            continue;
        }

        traversePrim(child, childTarget, flattenData);
    }

    // After processing children, if this is an xform, and there is a valid target, it means we can reparent
    // this prim to the target.
    if (isXform && _target.IsValid())
    {
        // If there are multiple redundant xforms then we don't need to reparent the interim ones.
        // As we do the deepest xform first, we would have added namespace edits to remove any of
        // those interim ones, so we can check that to avoid a reparent namespace edit.
        const auto& primPath = prim.GetPrimPath();
        const auto& targetPath = _target.GetPrimPath();
        if (!flattenData.toRemove.count(primPath) && primPath != targetPath)
        {
            reparent(primPath, targetPath, flattenData);
        }
    }
}


void FlattenHierarchyOperation::fixOriginalData(const FlattenData& flattenData) const
{
    auto stage = getUsdStage();

    // Iterate in reverse.
    // This should be the least nested to the most nested. This is done so that we don't change
    // an xform to the correct value and and then modify a parent xform afterwards, which would
    // cause it to be offset incorrectly.
    for (auto it = flattenData.originalData.rbegin(); it != flattenData.originalData.rend(); ++it)
    {
        const auto& prim = stage->GetPrimAtPath(it->first);

        // Fresh xformCache per query, to avoid stale data as we modify transforms.
        GfMatrix4d worldMatrix = UsdGeomXformCache().GetLocalToWorldTransform(prim);
        GfMatrix4d targetMatrix = it->second.matrix * worldMatrix.GetInverse();

        // If the matrix is not identity then apply it to compensate for the hierarchy change.
        if (targetMatrix != s_identity)
        {
            bool xformSet = false;
            bool resetsXformStack = false;
            UsdGeomXformable xformable(prim);
            for (const UsdGeomXformOp& xformOp : xformable.GetOrderedXformOps(&resetsXformStack))
            {
                // If we have already authored a transform we can re-use that property.
                if (xformOp.HasSuffix(_tokens->flattenHierarchyOp))
                {
                    // This property already existed, but flattening may be happening again.
                    // Ensure we _add_ the new matrix, don't just replace it.
                    targetMatrix *= xformOp.GetOpTransform(UsdTimeCode::Default());
                    xformOp.Set(targetMatrix);
                    xformSet = true;
                    break;
                }
            }

            // If we didn't find an existing one then add a new one.
            if (!xformSet)
            {
                xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble, _tokens->flattenHierarchyOp).Set(targetMatrix);
            }
        }

        // Reset visibility if it differs from the original value
        UsdGeomImageable imageable(prim);
        if (imageable)
        {
            const TfToken& vis = imageable.ComputeVisibility();
            if (vis != it->second.visible)
            {
                imageable.GetVisibilityAttr().Set(it->second.visible);
            }
        }

        // If !resetXformStack! was specified in one of the removed xforms, then we respect that.
        // It's possible this xform is underneath animation and is not intended to move.
        if (it->second.resetXformStack)
        {
            UsdGeomXformable(prim).SetResetXformStack(true);
        }
    }
}


// Scans the entire stage, finding internal and external references and caching them.
// We are limited in what we can easily do with references. We need to scan the full
// stage every time though, in case only a subset of the stage is being processed
// via the primPaths argument.
// Also caches relationships for similar reasons.
static void _findReferences(const UsdStageWeakPtr& stage, FlattenData& flattenData)
{

    SdfLayerHandle layer = stage->GetEditTarget().GetLayer();

    UsdPrimRange range(stage->GetPseudoRoot());
    for (auto iter = range.begin(); iter != range.end(); ++iter)
    {

        UsdPrimCompositionQuery query(*iter);
        const auto& arcs = query.GetCompositionArcs();
        for (const auto& arc : arcs)
        {
            // Ignore root
            if (arc.GetArcType() == PcpArcTypeRoot)
            {
                continue;
            }

            if (arc.GetTargetLayer()->GetIdentifier() != layer->GetIdentifier())
            {
                flattenData.references.insert(iter->GetPrimPath());
                iter.PruneChildren();
            }
            else
            {
                flattenData.references.insert(iter->GetPrimPath());

                // For internal references, also track the target and any of its parents. This lets
                // us know not to process them, as modifying the hierarchy would change what the
                // reference pointed at. For now, we don't support changing that and then modifying
                // the reference. However, only do this if the arc is not ancestral - we can potentially
                // still prune things below the reference point.
                if (!arc.IsAncestral())
                {
                    for (const auto& it : arc.GetTargetPrimPath().GetAncestorsRange())
                    {
                        flattenData.internalReferenced.insert(it);
                    }
                }
            }
        }

        // Also need to cache any relationships this prim has.
        const auto& relationships = iter->GetRelationships();

        for (const auto& relationship : relationships)
        {
            SdfPathVector targets;
            relationship.GetTargets(&targets);

            // If there are targets, cache this reference and the targets
            if (!targets.empty())
            {
                // Special-case material:binding. We can re-wire these later, so we track
                // them separately. They also won't prevent flattening the hierarchy.
                if (relationship.GetName() == UsdShadeTokens->materialBinding)
                {
                    // Track the prim and the material it is bound to
                    flattenData.boundPrims.emplace(iter->GetPrimPath(), targets.front());

                    // Track the material. To begin with it maps to itself, the value will be
                    // updated as we reparent things later.
                    flattenData.materialMap.emplace(targets.front(), targets.front());
                }
                else
                {
                    // This prim has a relationship; we do not currently support removing it
                    flattenData.relationships.insert(iter->GetPrimPath());

                    // For each target, also cache the target path and any ancestor. We do not
                    // want to change that hierarchy.
                    for (const auto& target : targets)
                    {
                        for (const auto& it : target.GetAncestorsRange())
                        {
                            flattenData.relationships.insert(it);
                        }
                    }
                }
            }
        }
    }
}


template <typename T>
static void _fixPaths(const T& object, const SdfPath& oldPath, const SdfPath& newPath)
{
    SdfPathVector connections;
    object.GetRawConnectedSourcePaths(&connections);

    for (const auto& path : connections)
    {
        if (path.HasPrefix(oldPath))
        {
            // Remove the existing connection and then reconnect to the new location.
            object.GetAttr().RemoveConnection(oldPath);
            object.ConnectToSource(path.ReplacePrefix(oldPath, newPath));
        }
    }
}


// Adjust the prefix on the connections of any part of a material.
static void _fixUsdShadeObject(const UsdPrim& prim, const SdfPath& oldPath, const SdfPath& newPath)
{

    UsdShadeConnectableAPI connectableApi(prim);

    for (const auto& input : connectableApi.GetInputs())
    {
        _fixPaths(input, oldPath, newPath);
    }

    for (const auto& output : connectableApi.GetOutputs())
    {
        _fixPaths(output, oldPath, newPath);
    }

    // Recurse through shading network
    for (const auto& child : prim.GetChildren())
    {
        _fixUsdShadeObject(child, oldPath, newPath);
    }
}


void FlattenHierarchyOperation::fixOriginalMaterials(const FlattenData& flattenData) const
{

    SdfChangeBlock _changeBlock;

    for (const auto& [originalPath, newPath] : flattenData.materialMap)
    {
        // Material wasn't moved, nothing to do.
        if (originalPath == newPath)
        {
            continue;
        }

        if (getContext()->verbose)
        {
            SO_LOG_VERBOSE("Fixing moved material %s", newPath.GetPrimPath().GetAsString().c_str());
        }

        // Get the material that has moved, then "fix" it.
        const UsdPrim& prim = getUsdStage()->GetPrimAtPath(newPath);
        _fixUsdShadeObject(prim, originalPath, newPath);
    }

    // Rebind any material:bindings on prims where the material has moved.
    for (const auto& [originalPrimPath, originalMaterialPath] : flattenData.boundPrims)
    {
        // Look up the original material binding
        // If that doesn't match (i.e. the material moved), then we can rebind.
        auto findIt = flattenData.materialMap.find(originalMaterialPath);
        if (findIt->second != originalMaterialPath)
        {
            // A bound prim may have itself been re-parented, so we need to do a lookup in case
            // there's a new location.
            SdfPath primPath = originalPrimPath;
            auto findRenamedIt = flattenData.renamedBoundPrims.find(primPath);
            if (findRenamedIt != flattenData.renamedBoundPrims.end())
            {
                primPath = findRenamedIt->second;
            }

            if (getContext()->verbose)
            {
                SO_LOG_VERBOSE("Rebinding %s to %s", primPath.GetAsString().c_str(), findIt->second.GetAsString().c_str());
            }

            // Get the prim, whether it has moved or not.
            const auto& prim = getUsdStage()->GetPrimAtPath(primPath);
            UsdRelationship relationship = prim.GetRelationship(UsdShadeTokens->materialBinding);

            SdfPathVector targets;
            relationship.GetTargets(&targets);

            for (auto& target : targets)
            {
                // Replace with the new path.
                if (target == findIt->first)
                {
                    target = findIt->second;
                }
            }

            // Set the new targets.
            relationship.SetTargets(targets);
        }
    }
}


bool FlattenHierarchyOperation::getSupportsAnalysis() const
{
    return true;
}


OperationResult FlattenHierarchyOperation::executeAnalysisImpl()
{
    FlattenData flattenData;
    flattenData.identityOnly = m_identityOnly;
    flattenData.analysisOnly = true;

    s_identity.SetIdentity();

    _findReferences(getUsdStage(), flattenData);

    std::vector<UsdPrim> prims;
    if (m_primPaths.empty())
    {
        prims.push_back(getUsdStage()->GetPseudoRoot());
    }
    else
    {
        for (const auto& path : m_primPaths)
        {
            UsdPrim prim = getUsdStage()->GetPrimAtPath(SdfPath(path));
            if (prim.IsValid())
            {
                prims.push_back(prim);
            }
        }
    }

    for (const auto& prim : prims)
    {
        if (flattenData.isReference(prim))
        {
            continue;
        }
        traversePrim(prim, prim, flattenData);
    }

    // Convert the set of paths-to-remove into a sorted vector for stable
    // output (asset-validator emits one issue per finding; deterministic
    // ordering keeps tests and CI logs reproducible).
    std::vector<std::string> redundantXforms;
    redundantXforms.reserve(flattenData.toRemove.size());
    for (const auto& path : flattenData.toRemove)
    {
        redundantXforms.emplace_back(path.GetAsString());
    }
    std::sort(redundantXforms.begin(), redundantXforms.end());

    JsObject analysisResult;
    analysisResult["redundantXforms"] = _toJson(redundantXforms);

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));
    return result;
}


OperationResult FlattenHierarchyOperation::executeImpl()
{
    FlattenData flattenData;
    flattenData.identityOnly = m_identityOnly;

    // Set identity on the static matrix
    s_identity.SetIdentity();

    // First process the stage to cache any references or referenced prims.
    // Also caches prims that have relationships, and materials.
    _findReferences(getUsdStage(), flattenData);

    std::vector<UsdPrim> prims;

    // If nothing was specified, traverse entire stage.
    if (m_primPaths.empty())
    {
        prims.push_back(getUsdStage()->GetPseudoRoot());
    }
    else
    {
        // We don't "resolve paths" as this is recursive, so just the explicit
        // paths is good.
        for (const auto& path : m_primPaths)
        {
            UsdPrim prim = getUsdStage()->GetPrimAtPath(SdfPath(path));
            if (prim.IsValid())
            {
                prims.push_back(prim);
            }
        }
    }

    // Verify there is actually something to do
    if (prims.empty())
    {
        SO_LOG_INFO("No prims found to process.");
        return { true };
    }

    // Process the prims
    for (const auto& prim : prims)
    {
        // Skip references.
        if (flattenData.isReference(prim))
        {
            continue;
        }

        traversePrim(prim, prim, flattenData);
    }

    SdfLayerHandle layer = getUsdStage()->GetEditTarget().GetLayer();

    // Apply the edits
    // First just try to apply. If this fails, then run CanApply so we can extract a useful error
    // message. This way we don't take the hit on CanApply + Apply every time.
    if (!layer->Apply(flattenData.edits))
    {
        SdfNamespaceEditDetailVector details;
        if (!layer->CanApply(flattenData.edits, &details))
        {
            SO_LOG_ERROR("Cannot apply changes:");

            // Include more info from the failed apply.
            for (const auto& detail : details)
            {
                std::ostringstream oss;
                oss << "Cannot modify currentPath=" << detail.edit.currentPath << ", newPath=" << detail.edit.newPath;
                oss << ", reason=" << detail.reason;
                SO_LOG_ERROR(oss.str().c_str());
            }
        }

        return { false };
    }

    // Finally, update anything that moved in the hierarchy. Transforms may have changed, visibility
    // may be different, etc. We cached the original values so that we can ensure they are correct
    // here.
    fixOriginalData(flattenData);

    // Fix any material bindings if materials have moved, and for those materials, also fix their
    // internal relationships (inputs/outputs).
    fixOriginalMaterials(flattenData);

    std::string suffix = flattenData.toRemove.size() == 1 ? "" : "s";
    SO_LOG_INFO("Removed %s Xform%s", std::to_string(flattenData.toRemove.size()).c_str(), suffix.c_str());

    return { true };
}


} // namespace omni::scene::optimizer
