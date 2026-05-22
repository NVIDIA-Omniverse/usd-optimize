// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "DeduplicateGeometry.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/MeshToolsCommon.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>
#include <omni/scene.optimizer/core/geometry/Bucket.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// TBB
#include <tbb/parallel_for.h>

// C++
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::DeduplicateGeometryOperation);

namespace omni::scene::optimizer
{


// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((copyValuesXformOp, "DeduplicateGeometryCopyValuesTransform"))
    ((compositionXformOpSuffix, "DeduplicateGeometryReferenceTransform"))
    ((compositionXformOp, "xformOp:transform:DeduplicateGeometryReferenceTransform"))
    ((primvarsNormals, "primvars:normals"))
    ((primvarsNormalsIndices, "primvars:normals:indices"))
    ((specifier, "specifier"))
    ((tempName, "__temp_name__"))
    ((typeName, "typeName"))
    ((typeXform, "Xform"))
    ((xformOpNamespace, "xformOp:"))
    ((duplicationSet, "duplicationSet"))
);
// LCOV_EXCL_STOP
// clang-format on

/// Constants
constexpr const char* s_categoryDeduplicate = "DEDUPLICATE_GEOMETRY";


DeduplicateGeometryOperation::DeduplicateGeometryOperation()
    : Operation("deduplicateGeometry",
                "Deduplicate Geometry",
                "This will replace multiple duplicate meshes in a scene to a single mesh and "
                "create references/instances to the single mesh prim. Since a referenced mesh "
                "uses less memory than the full duplicated mesh, this option can reduce system "
                "memory and vram consumption.")
{

    addArgument("meshPrimPaths",
                "Geometry to De-duplicate",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider",
                m_paths)
        .setPlaceholder("Add geometry or all will be processed");

    addArgument("tolerance",
                "Tolerance",
                kDisplayTypeFloat,
                "Acceptable point position change during deduplication. The value is a stage unit in worldspace",
                m_tolerance);

    addArgument("duplicateMethod",
                "Method",
                kDisplayTypeEnum,
                "Method used to conform meshes that are duplicates",
                m_duplicateMethod)
        .setEnumValues<DuplicateOption>({
            { DuplicateOption::eCopyValues, "Copy Values" },
            { DuplicateOption::eReference, "Reference" },
            { DuplicateOption::eInstanceableReference, "Instanceable Reference" },
            { DuplicateOption::eSetAttribute, "Set Attribute" },
        });

    addArgument("ignoreAttributes",
                "Ignore Attributes",
                kDisplayTypeAttributeList,
                "Optional list of attributes to ignore. This list can be explicit attributes, or if ending with "
                "a ':' can ignore namespaces.",
                m_ignoreAttributes)
        .setPlaceholder("Attributes/namespaces to ignore")
        .setVisibleIf("duplicateMethod == 1 or duplicateMethod == 2");

    addArgument("fuzzy",
                "Fuzzy mode",
                kDisplayTypeBool,
                "When enabled, uses shape comparison to find duplicates that differ in "
                "tessellation or have baked-in point offsets",
                m_fuzzy);

    addGroup("fuzzyEnabled",
             addArgument("allowScaling",
                         "Allow Scaling",
                         kDisplayTypeBool,
                         "When enabled, fuzzy comparison will factor out uniform scaling",
                         m_allowScaling))
        .setVisibleIf("fuzzy == True");

    addArgument("considerDeepTransforms",
                "Consider Deep Transforms",
                kDisplayTypeBool,
                "Look for duplicates where the points values have been uniformly transformed",
                m_considerDeepTransforms);

    // Maintained for compatibility, no longer exposed in UI.
    addArgument("useGpu",
                "Use GPU",
                kDisplayTypeBool,
                "When enabled, mesh comparison is performed on the GPU. The GPU mode is only available in fuzzy mode",
                m_useGpu)
        .setVisible(false);

    // Debug option
    // Currently used for the fuzzy duplicate AV checker
    addArgument("fuzzyOnly",
                "Fuzzy Only",
                kDisplayTypeBool,
                "When looking for fuzzy prims, ignore duplicate groups that have identical topology",
                m_fuzzyOnly)
        .setVisible(false);
}


DeduplicateGeometryOperation::~DeduplicateGeometryOperation() = default;


std::string DeduplicateGeometryOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion DeduplicateGeometryOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string DeduplicateGeometryOperation::getCategory() const
{
    return s_categoryDeduplicate;
}


std::string DeduplicateGeometryOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


bool DeduplicateGeometryOperation::getSupportsAnalysis() const
{
    return true;
}


/// Returns true if the prim is supported by deduplicate unsing the given method
inline bool _isSupportedPrim(const UsdPrim& prim, DuplicateOption method, bool isFuzzy)
{
    // We cannot read or write from invalid prims.
    if (!prim.IsValid())
    {
        return false;
    }

    // As all methods need to edit the prims we cannot support instance proxies.
    if (prim.IsInstanceProxy())
    {
        return false;
    }

    if (isFuzzy)
    {
        // Fuzzy only supports meshes
        if (!prim.IsA<UsdGeomMesh>())
        {
            return false;
        }
    }
    else if (!prim.IsA<UsdGeomPointBased>())
    {
        // Non-fuzzy supports point-based
        return false;
    }

    // Check for Time Sampled data which we will not process
    if (_hasAuthoredTimeSamples(prim))
    {
        SO_LOG_INFO("Skipping %s because of time varying attributes", prim.GetPath().GetAsString().c_str());
        return false;
    }

    // Prims with children cannot be deduplicated as we do not consider children in checks for identical
    // geometry or the deduplication methods.
    if (prim.GetAllChildren())
    {
        return false;
    }

    // Ensure that we do not apply our deduplication several times, i.e. idempotent.
    // TODO: We should have a better method for identifying prims that are already deduplicated or do not require
    // further deduplication. For now we simply look for the xformOps that are set during deduplication. Because we only
    // look for the xformOp that will be added in this mode it is possible for a Mesh that was duplicated using
    // composition to have the copy values method run on it and vice versa.
    if (method == DuplicateOption::eCopyValues)
    {
        if (_containsOrderedXformOpsSuffix(prim, _tokens->copyValuesXformOp))
        {
            return false;
        }
    }
    else
    {
        // The composition xformOp will be on the parent due to the way we duplicate Mesh prims to support instancing.
        if (_containsOrderedXformOpsSuffix(prim.GetParent(), _tokens->compositionXformOpSuffix))
        {
            return false;
        }
    }

    return true;
}


// Convenience function that provides the transform from one set of points to another.
// It is assumed that the meshes have been identified as equal up to a deep transform.
GfMatrix4d _getTransformFromTo(const VtArray<GfVec3f>& sourcePoints, const VtArray<GfVec3f>& targetPoints)
{
    // Compute the origin to pivot matrix for each set of points
    GfMatrix4d sourceOriginToPivotMatrix = _getOriginToPivotMatrix(sourcePoints);
    GfMatrix4d targetOriginToPivotMatrix = _getOriginToPivotMatrix(targetPoints);

    // Compute the transform matrix to position the source points in the same position as the target points.
    return sourceOriginToPivotMatrix.GetInverse() * targetOriginToPivotMatrix;
}


void DeduplicateGeometryOperation::_copyPrimData(const PXR_NS::UsdPrim& sourcePrim, const PXR_NS::UsdPrim& targetPrim)
{
    // Wrap in a ChangeBlock.
    SdfChangeBlock changeBlock;

    UsdGeomPrimvarsAPI targetPrimvarsAPI(targetPrim);

    // These are handled in the main method so we don't need to copy them
    std::set<TfToken> skipAttributes = { UsdGeomTokens->faceVertexIndices,
                                         UsdGeomTokens->faceVertexCounts,
                                         UsdGeomTokens->points,
                                         UsdGeomTokens->extent };


    // Copy all array attributes and primvars from the source to the target prim
    // Record which attributes and primvars have been copied
    VtValue value;
    std::set<TfToken> copiedArrays;

    UsdGeomPrimvarsAPI sourcePrimvarsAPI(sourcePrim);

    for (const auto& primvar : sourcePrimvarsAPI.GetPrimvarsWithAuthoredValues())
    {
        const SdfValueTypeName& typeName = primvar.GetTypeName();
        if (typeName.IsArray())
        {
            auto newPrimvar =
                targetPrimvarsAPI.CreatePrimvar(primvar.GetName(), primvar.GetTypeName(), primvar.GetInterpolation());
            primvar.ComputeFlattened(&value);
            newPrimvar.Set(value);
            skipAttributes.insert(primvar.GetName());
            copiedArrays.insert(primvar.GetName());
        }
    }

    for (const auto& attribute : sourcePrim.GetAuthoredAttributes())
    {
        const TfToken& name = attribute.GetName();

        // Don't copy attributes covered by the primvars
        if (skipAttributes.find(name) != skipAttributes.end())
        {
            continue;
        }

        if (attribute.GetTypeName().IsArray())
        {
            attribute.FlattenTo(targetPrim);
            copiedArrays.insert(attribute.GetName());
        }
    }

    // Clear array attributes and primvars on the target prim
    // which have not been copied

    for (const auto& primvar : targetPrimvarsAPI.GetPrimvarsWithAuthoredValues())
    {
        if (skipAttributes.find(primvar.GetName()) != skipAttributes.end())
        {
            continue;
        }
        if (copiedArrays.find(primvar.GetName()) != copiedArrays.end())
        {
            continue; // Only clear primvars which have not been copied
        }
        const SdfValueTypeName& typeName = primvar.GetTypeName();
        if (typeName.IsArray())
        {
            primvar.GetAttr().Clear();
        }
    }

    for (const auto& attribute : targetPrim.GetAuthoredAttributes())
    {
        if (skipAttributes.find(attribute.GetName()) != skipAttributes.end())
        {
            continue;
        }
        if (copiedArrays.find(attribute.GetName()) != copiedArrays.end())
        {
            continue; // Only clear attributes which have not been copied
        }
        const SdfValueTypeName& typeName = attribute.GetTypeName();
        if (typeName.IsArray())
        {
            attribute.Clear();
        }
    }
}


/// Adjust op order for inverse pivot.
///
/// This function looks for an invert pivot op. If found, the last xform op in the order
/// is swapped with it. Thus, it makes the assumption you've just appended an xform op
/// that should appear before the invert.
static bool _adjustOpOrderForInversePivot(const std::vector<UsdGeomXformOp>& xformOps, VtTokenArray& xformOpOrder)
{

    const auto& findInverseIt =
        std::find_if(xformOps.rbegin(),
                     xformOps.rend(),
                     [](const UsdGeomXformOp& op) { return (op.HasSuffix(UsdGeomTokens->pivot) && op.IsInverseOp()); });

    // If there was an inverse pivot op, then swap the one we just added with it so it comes before it.
    if (findInverseIt != xformOps.rend())
    {
        size_t index = std::distance(findInverseIt, xformOps.rend());
        std::swap(xformOpOrder[index - 1], xformOpOrder[xformOpOrder.size() - 1]);
        return true;
    }

    return false;
}


void DeduplicateGeometryOperation::_conformTopologyAttributeValues(const PrimVector& prims)
{
    // Use the attribute values from the first prim in the array as the desired values for all prims
    const UsdPrim& sourcePrim = prims[0];

    // Get the topology attributes from the source prim.

    VtIntArray faceVertexIndices;
    VtIntArray faceVertexCounts;

    VtIntArray targetFaceVertexIndices;
    VtIntArray targetFaceVertexCounts;

    if (m_fuzzy)
    {
        UsdGeomMesh sourceMesh(sourcePrim);
        sourceMesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);
        sourceMesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
    }

    // Get the points from the source mesh.
    VtVec3fArray points;

    UsdGeomPointBased pointBased(sourcePrim);

    // This should never occur but ...
    if (!pointBased.GetPointsAttr().Get(&points))
    {
        // Early out if there are no points as there is no work to do.
        return; // LCOV_EXCL_LINE
    }

    // Get the normals from the source mesh, and track if an authored value was found.
    VtVec3fArray normals;
    bool hasNormals = false;
    // If primvar normals are authored get the flattened value of those ...
    if (auto primvar = UsdGeomPrimvar(sourcePrim.GetAttribute(_tokens->primvarsNormals)))
    {
        primvar.ComputeFlattened(&normals);
        hasNormals = true;
    }
    // ... otherwise get the normals which cannot be indexed so are effectively flattened.
    // If there is no authored value for normals we will still get an empty array as the fallback value.
    else
    {
        hasNormals = pointBased.GetNormalsAttr().Get(&normals);
    }

    // Get the extent from the source as setting the points on the targets will invalidate their extent value.
    VtVec3fArray extent;
    bool hasExtent = pointBased.GetExtentAttr().Get(&extent);
    // TODO: Compute the extent from the points if no extent is authored.
    // This should be set on the source prim as well as the targets if it was empty before.

    // Iterate over all the target prims setting the topology attribute values from the source prim and applying the
    // required transform offset to compensate for the value changes.
    // Start at 1 to avoid handling the prim being used as the source.
    for (size_t index = 1; index < prims.size(); ++index)
    {
        const UsdPrim& targetPrim = prims[index];
        UsdGeomMesh targetMesh(targetPrim);

        // Compute the transform required to position the target prim so that the topology attribute values from the
        // source prim will result in the same worldspace topology values as the target prim currently has.
        VtVec3fArray targetPoints;
        targetMesh.GetPointsAttr().Get(&targetPoints);

        GfMatrix4d sourceToTarget;
        bool tessellationIsEqual = true;

        if (m_fuzzy)
        {
            // This transformation computation is based on PCA and neither dependent on the tessellation
            // nor assuming point to point correspondence.
            // The faceVertexCounts and faceVertexIndices are only used to compute a more accurate transform
            // by integrating over the surface rather than just using the points
            targetMesh.GetFaceVertexIndicesAttr().Get(&targetFaceVertexIndices);
            targetMesh.GetFaceVertexCountsAttr().Get(&targetFaceVertexCounts);

            sourceToTarget = _getTransformFromToFuzzy(points.AsConst(),
                                                      faceVertexIndices.AsConst(),
                                                      faceVertexCounts.AsConst(),
                                                      targetPoints.AsConst(),
                                                      targetFaceVertexIndices.AsConst(),
                                                      targetFaceVertexCounts.AsConst());

            // test whether the tessellation of the source and target prims is the same
            // if not we need to clone all primvars and attributes because they are not compatible

            tessellationIsEqual = faceVertexIndices == targetFaceVertexIndices;
        }
        else
        {
            sourceToTarget = _getTransformFromTo(points.AsConst(), targetPoints.AsConst());
        }
        // TODO: We should skip the attribute setting phase if the transform is identity.
        // An identity martix implies that the topology attribute values are already the same and no work is required.
        // (Only if not in fuzzy mode!)

        // in fuzzy mode, if the tessellation of the source and target prims are not the same
        // we needd to copy all primvars and attributes because they are not compatible

        // Points
        targetMesh.GetPointsAttr().Set(points);

        // Normals
        if (hasNormals && tessellationIsEqual)
        {
            // Regardless of the attribute where the source normals were stored or the interpolation defined there, we
            // should be able to reuse the existing authored attribute on the target.
            // We know that the number of values for normals must match between the source and target and there is no
            // scenario where there is not an authored normal on the target and `hasNormals` be true.
            if (auto primvar = UsdGeomPrimvar(targetPrim.GetAttribute(_tokens->primvarsNormals)))
            {
                primvar.Set(normals);
                // We have the flattened value so muct block the indices if the original values were indexed.
                if (primvar.IsIndexed())
                {
                    primvar.BlockIndices(); // LCOV_EXCL_LINE
                }
            }
            // ... otherwise get the normals which cannot be indexed so are effectivly flattened.
            // If there is no authored value for normals we will still get an empty array as the fallback value.
            else
            {
                targetMesh.GetNormalsAttr().Set(normals);
            }
        }

        // Extent
        if (hasExtent)
        {
            targetMesh.GetExtentAttr().Set(extent);
        }

        if (!tessellationIsEqual)
        {
            targetMesh.GetFaceVertexCountsAttr().Set(faceVertexCounts);
            targetMesh.GetFaceVertexIndicesAttr().Set(faceVertexIndices);

            // Copy all attributes and primvars from the source prim to the destination prim as well
            _copyPrimData(sourcePrim, targetPrim);
        }

        // Add the matrix converting sourceMesh to targetMesh as most local transform to XformStack.
        // By setting a unique name via our own opSuffix, this won't fail even if another TransformOp is present.
        bool xformSet = false;
        bool resetsXformStack = false;
        UsdGeomXformable xformable(targetPrim);

        std::vector<UsdGeomXformOp> xformOps = xformable.GetOrderedXformOps(&resetsXformStack);

        for (const UsdGeomXformOp& xformOp : xformOps)
        {
            if (xformOp.HasSuffix(_tokens->copyValuesXformOp))
            {
                xformOp.Set(sourceToTarget);
                xformSet = true;
            }
        }

        if (!xformSet)
        {
            // Create the transform op.
            // This adds the new op to the end of the op order.
            xformable.AddTransformOp(UsdGeomXformOp::PrecisionDouble, _tokens->copyValuesXformOp).Set(sourceToTarget);

            // If there is an existing pivot, then we need to ensure that the transform op we just added
            // actually comes before the inverse.
            VtTokenArray xformOpOrder;
            xformable.GetXformOpOrderAttr().Get(&xformOpOrder);

            // Adjust the pivot order. If this succeeds, i.e. we found an inverse pivot and changed
            // the op order, then we need to find the pivot and adjust its translate to compensate
            // for the new matrix.
            if (_adjustOpOrderForInversePivot(xformOps, xformOpOrder))
            {
                // Set the updated order.
                xformable.GetXformOpOrderAttr().Set(xformOpOrder);

                auto findPivotOp = std::find_if(xformOps.begin(),
                                                xformOps.end(),
                                                [](const UsdGeomXformOp& op)
                                                { return (op.HasSuffix(UsdGeomTokens->pivot) && !op.IsInverseOp()); });

                if (findPivotOp != xformOps.end())
                {
                    GfVec3d pivotVal;
                    if (findPivotOp->Get(&pivotVal))
                    {
                        pivotVal -= sourceToTarget.ExtractTranslation();
                        findPivotOp->Set(pivotVal);
                    }
                }
            }
        }
    }
}

PrimVectors DeduplicateGeometryOperation::_findIdenticalMeshes(const PrimVectors& equalMeshVectors)
{
    // Allocate an array to hold the results from each item in the range.
    size_t count = equalMeshVectors.size();
    std::vector<PrimVectors> parallelResults(count);

    // Sort groups by descending size so the long-running ones start first. Combined with
    // grain size 1 in the parallel_for below, this keeps TBB load-balanced when group sizes
    // are highly skewed (e.g. one topology bucket with millions of meshes plus a long tail
    // of small ones) — threads finishing small groups can steal the next-biggest one.
    std::vector<size_t> sizesOrder(count);
    for (size_t k = 0; k < count; ++k)
    {
        sizesOrder[k] = k;
    }
    std::sort(sizesOrder.begin(),
              sizesOrder.end(),
              [&](const size_t a, const size_t b) { return equalMeshVectors[a].size() > equalMeshVectors[b].size(); });

    // thread-safe caches
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    // Prepare ignore tokens
    TfTokenVector ignoreAttributes;
    TfTokenVector ignoreNamespaces;

    for (const auto& attributeName : m_ignoreAttributes)
    {
        if (TfStringEndsWith(attributeName, ":"))
        {
            ignoreNamespaces.emplace_back(TfToken(attributeName));
            SO_LOG_VERBOSE("Ignoring namespace %s", attributeName.c_str());
        }
        else
        {
            ignoreAttributes.emplace_back(TfToken(attributeName));
            SO_LOG_VERBOSE("Ignoring attribute %s", attributeName.c_str());
        }
    }

    // Define a parallel function to bucket the prims for each equal prim set in a range.
    auto bucketMeshesFn = [&](const tbb::blocked_range<size_t>& range)
    {
        // per thread xform cache
        UsdGeomXformCache xformCache;

        for (size_t k = range.begin(); k < range.end(); ++k)
        {
            const size_t i = sizesOrder[k];

            // Bucket the meshes
            Bucketer bucketer(getContext());

            // Material bindings are left on the prim that holds the composition arc and then inherited to the mesh
            // below. For that reason we do not need to consider the bound material when identifying mesh buckets.
            bucketer.SetConsiderMaterials(false);

            // Consider all attributes so that we know the composed result will be the same as the current state.
            bucketer.SetConsiderPrimAttributes(true);

            // For fuzzy deduplication, allow matching when the tesselation differs.
            if (m_fuzzy)
            {
                bucketer.AddIgnoreAttributeNames({ UsdGeomTokens->faceVertexCounts, UsdGeomTokens->faceVertexIndices });
            }

            // Custom ignore attributes/namespaces
            bucketer.AddIgnoreAttributeNames(ignoreAttributes);
            bucketer.AddIgnoreAttributeNamespaces(ignoreNamespaces);

            // Do not populate mesh info as we will not use the values anyway.
            // This also avoids buckets being split when a large number of prims are added.
            bucketer.SetCollectMeshInfo(false);

            // Ignore points, extents and normals as these a recomputed during recomposition and have already been
            // compared.
            bucketer.AddIgnoreAttributeNames({ UsdGeomTokens->points,
                                               UsdGeomTokens->extent,
                                               UsdGeomTokens->normals,
                                               _tokens->primvarsNormalsIndices,
                                               _tokens->primvarsNormals });

            // Ignore all xform related attributes as these are compensated for when recomposing.
            bucketer.AddIgnoreAttributeNames({ UsdGeomTokens->xformOpOrder });
            bucketer.AddIgnoreAttributeNamespaces({ _tokens->xformOpNamespace });

            // don't take data volume into account when bucketing
            bucketer.setIgnoreDataVolume(true);

            // For very large groups the VirtualMesh construction can be costly.
            // Thankfully this code can be parallel, and with the grain size
            // in the outer loop plays nicely.
            const PrimVector& group = equalMeshVectors[i];
            std::vector<VirtualMesh> virtualMeshes(group.size());
            {
                constexpr size_t kParallelThreshold = 1000;
                if (group.size() >= kParallelThreshold)
                {
                    tbb::parallel_for(
                        tbb::blocked_range<size_t>(0, group.size()),
                        [&](const tbb::blocked_range<size_t>& innerRange)
                        {
                            UsdGeomXformCache localXformCache;
                            for (size_t j = innerRange.begin(); j < innerRange.end(); ++j)
                            {
                                virtualMeshes[j] = VirtualMesh(group[j], localXformCache, bindingsCache, collQueryCache);
                            }
                        },
                        tbb::auto_partitioner());
                }
                else
                {
                    for (size_t j = 0; j < group.size(); ++j)
                    {
                        virtualMeshes[j] = VirtualMesh(group[j], xformCache, bindingsCache, collQueryCache);
                    }
                }
            }

            bucketer.AddVirtualMeshes(virtualMeshes, SdfPath("/"));
            bucketer.Bucket(getUsdStage());

            // process the output of the bucketer
            for (const VirtualMesh& bucket : bucketer.GetOutputData())
            {
                // Skip buckets that are not supersets
                if (!bucket.isSuperset())
                {
                    continue; // LCOV_EXCL_LINE
                }

                const std::vector<VirtualMesh>& children = bucket.getSupersetChildren();

                // skip buckets that only contain a single child
                if (children.size() <= 1)
                {
                    continue; // LCOV_EXCL_LINE
                }

                // Get the prims from the children of the superset - these have been identified as duplicates.
                PrimVector prims;
                prims.reserve(children.size());
                for (const VirtualMesh& child : children)
                {
                    if (child.isDerivedFromPrim())
                    {
                        prims.push_back(child.getPrim());
                    }
                }

                parallelResults[i].push_back(prims);
            }
        }
    };

    // Grain size 1 + auto_partitioner + the descending-size sort above lets TBB's work-stealing
    // distribute big groups across idle threads instead of bundling fixed-size chunks per thread.
    tbb::parallel_for(tbb::blocked_range<size_t>(0, count, 1), bucketMeshesFn, tbb::auto_partitioner());

    // Flatten the values produced in parallel into a single array
    PrimVectors result;
    for (const auto& identicalMeshSets : parallelResults)
    {
        for (const auto& identicalMeshSet : identicalMeshSets)
        {
            if (!identicalMeshSet.empty())
            {
                result.push_back(identicalMeshSet);
            }
        }
    }

    return result;
}

// Use composition to ensure that meshes which can produce the same visual result are based of the same mesh prims.
// Some methods will also set the prims holding the composition arcs as instanceable so that the duplicate prims are
// instance proxies from the scene description point of view.
void DeduplicateGeometryOperation::_conformUsingComposition(const PrimVectors& duplicatePrimVectors)
{
    // Determine which prim should be used as the prototype for each set of duplicate prims.
    // Then calculate the transformation that needs to be applied to the prototype to match the existing position of
    // each instance.

    // Map of path to prototype to vector of paths to prims that will reference that prototype (and set instanceable)
    std::map<SdfPath, SdfPathVector> prototypesToReferences;
    // Map of path to prototype to vector of transforms for the prims that will reference that prototype
    std::map<SdfPath, std::vector<GfMatrix4d>> xformsForReferences;

    // TODO: This could be computed in parallel, we could also use the MeshSpec data collected earlier to compute this
    // without the need to pull points and transform matrices from meshes.
    for (auto& duplicatePrims : duplicatePrimVectors)
    {
        // Use the last prim in the array as the prototype for composing the duplicates.
        const UsdPrim& prototypePrim = duplicatePrims.back();

        size_t duplicateCount = duplicatePrims.size() - 1;

        // Collect information about the prims that will compose the prototype.
        // TODO: Store this info as a vector of SdfPath, GfMatrix pairs rather than two vectors.
        auto& targetPaths = prototypesToReferences[prototypePrim.GetPath()];
        targetPaths.reserve(duplicateCount);
        auto& targetMatrices = xformsForReferences[prototypePrim.GetPath()];
        targetMatrices.reserve(duplicateCount);

        // Get the points and topology data of the prototype needed to compute transforms.
        VtVec3fArray prototypePoints;
        VtIntArray prototypeFaceVertexIndices;
        VtIntArray prototypeFaceVertexCounts;

        UsdGeomPointBased(prototypePrim).GetPointsAttr().Get(&prototypePoints);

        // For fuzzy mode, we need face topology for OBB-based transform computation.
        // Note: All prims are guaranteed to be UsdGeomMesh in fuzzy mode (filtered by _isSupportedPrim).
        if (m_fuzzy)
        {
            UsdGeomMesh prototypeMeshGeom(prototypePrim);
            prototypeMeshGeom.GetFaceVertexIndicesAttr().Get(&prototypeFaceVertexIndices);
            prototypeMeshGeom.GetFaceVertexCountsAttr().Get(&prototypeFaceVertexCounts);
        }

        for (size_t duplicateIndex = 0; duplicateIndex < duplicateCount; ++duplicateIndex)
        {
            const auto& prim = duplicatePrims[duplicateIndex];

            VtVec3fArray points;
            UsdGeomPointBased(prim).GetPointsAttr().Get(&points);

            GfMatrix4d transformFromTo;

            if (m_fuzzy)
            {
                // Use OBB-based fuzzy transform computation for tolerance-aware matching
                VtIntArray faceVertexIndices;
                VtIntArray faceVertexCounts;

                UsdGeomMesh meshGeom(prim);
                meshGeom.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);
                meshGeom.GetFaceVertexCountsAttr().Get(&faceVertexCounts);

                transformFromTo = _getTransformFromToFuzzy(prototypePoints.AsConst(),
                                                           prototypeFaceVertexIndices.AsConst(),
                                                           prototypeFaceVertexCounts.AsConst(),
                                                           points.AsConst(),
                                                           faceVertexIndices.AsConst(),
                                                           faceVertexCounts.AsConst());
            }
            else
            {
                // Use point-based transform for exact matching
                transformFromTo = _getTransformFromTo(prototypePoints.AsConst(), points.AsConst());
            }

            targetMatrices.push_back(transformFromTo);
            targetPaths.push_back(prim.GetPath());
        }
    }

    // In order for the Meshes to become instance proxies when instanceable is true we need the composition to
    // occur on the parent prim of the Mesh. Because our prototype is the Mesh prim itself we need to flatten
    // the properties onto a child prim and repurpose the current prim as an Xform.
    SdfPathVector prototypePaths;
    for (const auto& iter : prototypesToReferences)
    {
        prototypePaths.push_back(iter.first);
    }

    _batchedSplitIntoXformAndChild(getUsdStage(), prototypePaths);

    // Record the number of references for reporting.
    size_t numReferences = 0;

    {
        // DANGER DANGER DANGER
        // Be very careful how edits are made while this block is in place. We are now responsible for tracking the
        // changes we make to layers as API read calls will be out of date.
        SdfChangeBlock _changeBlock;

        // Due to crashes encountered when changing prim types we duplicate the prim to a new path and type, make
        // any required edits, then swap the prims at the Sdf layer level. This avoids the crashes from OM-88653
        const SdfLayerHandle& editLayer = getUsdStage()->GetEditTarget().GetLayer();
        SdfBatchNamespaceEdit removeEdits;

        // Renames are applied in chunks below — SdfLayer::Apply(SdfBatchNamespaceEdit)
        // is roughly O(N^2) in batch size, so a single batch of millions of edits
        // is intractable.
        std::vector<SdfNamespaceEdit> renameOps;

        for (const auto& iter : prototypesToReferences)
        {
            // Handle the prototype prim
            const SdfPath& prototypePath = iter.first;

            // Handle the target prims

            // Get the paths of prims that should use the source prim in composition and the transform matrix that needs
            // to be applied to them to maintain their current visual result.
            const auto& paths = iter.second;
            const auto& xforms = xformsForReferences.at(prototypePath);

            numReferences += paths.size();

            // Define the reference prims with composition arcs and transforms.
            for (size_t i = 0; i < paths.size(); ++i)
            {
                const SdfPath& path = paths[i];
                const GfMatrix4d& xform = xforms[i];

                const UsdPrim& prim = getUsdStage()->GetPrimAtPath(path);

                // Convert the existing Mesh prim into an Xform retaining only properties that can be inherited.
                const TfToken& tempName = TfToken("__temp_name__" + path.GetName());
                const SdfPath& tempPath = path.GetParentPath().AppendChild(tempName);
                SdfPrimSpecHandle tempSpec = SdfCreatePrimInLayer(editLayer, tempPath);
                tempSpec->SetTypeName("Xform");
                tempSpec->SetSpecifier(SdfSpecifierDef);

                // Construct an xformOpOrder value based on the existing one with our custom transform added to the end.
                VtTokenArray xformOpOrder;
                UsdGeomXformable xformable(prim);
                xformable.GetXformOpOrderAttr().Get(&xformOpOrder);

                // Get the actual ops so we can identify pivot pairs.
                bool reset = false;
                std::vector<UsdGeomXformOp> xformOps = xformable.GetOrderedXformOps(&reset);

                // create a new op order with our new xform op inserted at the start of the chain
                VtTokenArray newXformOpOrder;
                newXformOpOrder.reserve(xformOpOrder.size() + 1);
                newXformOpOrder.push_back(_tokens->compositionXformOp);
                for (const TfToken& token : xformOpOrder)
                {
                    newXformOpOrder.push_back(token);
                }

                // Author a transform xformOp and set its value.
                const auto& xformOpSpec =
                    SdfAttributeSpec::New(tempSpec, _tokens->compositionXformOp, SdfValueTypeNames->Matrix4d);
                xformOpSpec->SetInfo(SdfFieldKeys->Default, VtValue(xform));

                // Author an xformOpOrder and set its value.
                const auto& xformOpOrderSpec = SdfAttributeSpec::New(tempSpec,
                                                                     UsdGeomTokens->xformOpOrder,
                                                                     SdfValueTypeNames->TokenArray,
                                                                     SdfVariabilityUniform);
                xformOpOrderSpec->SetInfo(SdfFieldKeys->Default, VtValue(newXformOpOrder));

                // get any ops that are pivots
                const std::vector<UsdGeomXformOp> pivotOps = _getPivotXformOps(xformOps);

                // Copy authored properties from the Mesh to the new Xform.
                for (const auto& property : prim.GetAuthoredProperties())
                {
                    const TfToken& propertyName = property.GetName();

                    // Skip properties that we have manually authored already.
                    if (propertyName == UsdGeomTokens->xformOpOrder || propertyName == _tokens->compositionXformOp)
                    {
                        continue;
                    }

                    // Inheritable properties should go on the Xform and non-inheritable should be discarded as they
                    // will be on the child prim that comes from composition.
                    if (_isInheritableProperty(property))
                    {
                        // Pivots have a slightly special case. They are inheritable, but we need to adjust their
                        // value to compensate for the matrix we applied.
                        const auto pivotIt = std::find_if(pivotOps.begin(),
                                                          pivotOps.end(),
                                                          [&propertyName](const UsdGeomXformOp& op)
                                                          { return op.GetOpName() == propertyName; });
                        if (pivotIt != pivotOps.end())
                        {
                            // pivots can either be vec3f or vec3d so attempt to get it as a vec3d first, then fallback
                            // to casting from a vec3f if that fails.
                            GfVec3d pivotVal;
                            bool gotPivotVal = false;
                            if (pivotIt->Get(&pivotVal))
                            {
                                gotPivotVal = true;
                            }
                            else
                            {
                                GfVec3f pivotValF;
                                if (pivotIt->Get(&pivotValF))
                                {
                                    gotPivotVal = true;
                                    // cast to doubles
                                    pivotVal[0] = static_cast<double>(pivotValF[0]);
                                    pivotVal[1] = static_cast<double>(pivotValF[1]);
                                    pivotVal[2] = static_cast<double>(pivotValF[2]);
                                }
                            }

                            // If we got the pivot as a vec3 then transform, otherwise just fall through and flatten
                            // the property as normal.
                            if (gotPivotVal)
                            {
                                // transform the pivot by the new matrix.
                                pivotVal = xform.GetInverse().Transform(pivotVal);

                                // Preserve original type: convert back to vec3f if needed
                                VtValue value;
                                if (pivotIt->GetTypeName() == SdfValueTypeNames->Float3)
                                {
                                    value = VtValue(GfVec3f(pivotVal[0], pivotVal[1], pivotVal[2]));
                                }
                                else
                                {
                                    value = VtValue(pivotVal);
                                }

                                // flatten
                                _flattenPropertyToPrimSpecWithValue(property, tempSpec, value);
                                continue;
                            }
                        }

                        _flattenPropertyToPrimSpec(property, tempSpec);
                    }
                }

                // Block any composition arcs other than the one being used.
                tempSpec->GetPayloadList().ClearEditsAndMakeExplicit();
                tempSpec->GetInheritPathList().ClearEditsAndMakeExplicit();
                tempSpec->GetSpecializesList().ClearEditsAndMakeExplicit();

                // Setup composition based on the method that has been specified.
                const SdfReference reference("", prototypePath);
                switch (m_duplicateMethod)
                {
                case DuplicateOption::eReference:
                    tempSpec->GetReferenceList().GetExplicitItems().push_back(reference);
                    tempSpec->SetInstanceable(false);
                    break;

                case DuplicateOption::eInstanceableReference:
                    tempSpec->GetReferenceList().GetExplicitItems().push_back(reference);
                    tempSpec->SetInstanceable(true);
                    break;

                case DuplicateOption::eSetAttribute:
                    // nothing to do here
                    break;

                // LCOV_EXCL_START
                case DuplicateOption::eCopyValues:
                default:
                    SO_LOG_WARN("Invalid deduplicate option: %i", static_cast<int>(m_duplicateMethod));
                }
                // LCOV_EXCL_STOP

                // Queue the original path to be removed from the current layer if it is specified there
                // Queue the temp path to be renamed to that of the original
                if (editLayer->HasSpec(path))
                {
                    removeEdits.Add(SdfNamespaceEdit::Remove(path));
                }
                renameOps.push_back(SdfNamespaceEdit::Rename(tempPath, path.GetNameToken()));
            }
        }

        // Apply the remove edits first otherwise renames will fail as the layer has prim specs with those names.
        editLayer->Apply(removeEdits);

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

    SO_LOG_INFO("Replaced %zu meshes with references", numReferences);
}


void DeduplicateGeometryOperation::computeEqualGeometrySets(std::vector<UsdPrim>& resolvedPrims, PrimVectors& primVectors)
{
    // Resolve prims
    constexpr bool meshesOnly = false;
    constexpr bool reverse = true;
    const Usd_PrimFlagsPredicate& predicate = UsdPrimAllPrimsPredicate;

    auto callback = [&](const UsdPrim& prim, UsdPrimRange::iterator&) -> bool
    { return _isSupportedPrim(prim, m_duplicateMethod, m_fuzzy); };

    resolvedPrims =
        _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(), m_paths, meshesOnly, reverse, predicate, callback);

    // At this point, check that we have found something to process. If not, log a note and finish.
    if (resolvedPrims.empty())
    {
        SO_LOG_INFO("Found no prims to process");
        return;
    }

    // Compute sets of meshes that can be treated as duplicates.
    constexpr bool ignoreNormals = false;

    if (m_useGpu && !isCudaAvailable())
    {
        SO_LOG_WARN("GPU requested but CUDA is not available. Falling back to CPU.");
        m_useGpu = false;
    }

    primVectors = m_fuzzy ? _computeEqualMeshPrimsFuzzy(resolvedPrims, m_tolerance, m_allowScaling, m_useGpu) :
                            _computeEqualMeshPrims(resolvedPrims, m_considerDeepTransforms, m_tolerance, ignoreNormals);

    // If we are using composition to deduplicate we need to ensure that all attributes on the prims are
    // equal not just the topology attributes. By comparing attribute values on the prims within the
    // equal prim sets we can divide the sets into subsets of prims.
    switch (m_duplicateMethod)
    {
    case DuplicateOption::eReference:
    case DuplicateOption::eInstanceableReference:
        primVectors = _findIdenticalMeshes(primVectors);
        break;
    default:
        break;
    }

    // If "fuzzyOnly" is enabled it means we want to ignore any groups of duplicates
    // where their topology is identical. That is, we only consider groups where at least
    // one of the prims is "actually" a fuzzy match.
    if (m_fuzzy && m_fuzzyOnly)
    {
        for (auto& primVector : primVectors)
        {
            // Re-run the non-fuzzy check on these to see which of
            // these prims have identical topology.
            PrimVectors identicalMeshes =
                _computeEqualMeshPrims(primVector, m_considerDeepTransforms, m_tolerance, ignoreNormals);

            // Having run the standard deduplicate, check the result. If we get the exact same result
            // back - one group with all the same prims - then all of them have identical topology.
            // Any other scenario means duplicates with actual different topology, which is what we
            // want to report.
            if (!identicalMeshes.empty() && identicalMeshes.front().size() == primVector.size())
            {
                primVector.clear();
            }
        }

        // Remove any empty vectors
        primVectors.erase(std::remove_if(primVectors.begin(),
                                         primVectors.end(),
                                         [](const PrimVector& primVector) { return primVector.empty(); }),
                          primVectors.end());
    }

    // Report the number of equal mesh sets found.
    std::string suffix = primVectors.size() == 1 ? "" : "s";
    SO_LOG_INFO("Found %lu set%s of equal meshes", primVectors.size(), suffix.c_str());
}


OperationResult DeduplicateGeometryOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|DeduplicateGeometry|Analysis");

    // Compute duplicate geometry
    std::vector<UsdPrim> resolvedPrims;
    PrimVectors equalSets;
    computeEqualGeometrySets(resolvedPrims, equalSets);

    // Sort the results by the first prim path in each vector. A prim can only appear in one set, and we should not
    // have empty sets. This enforces a stable order for calling code.
    std::sort(equalSets.begin(),
              equalSets.end(),
              [](const PrimVector& a, const PrimVector& b) { return a.front() < b.front(); });

    // Convert results to JSON payload
    JsObject resultJson;
    resultJson["analysis"] = _toJson(equalSets);

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    SO_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult DeduplicateGeometryOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|DeduplicateGeometry|Execute");

    if (getContext()->generateReport)
    {
        SO_LOG_INFO("Running deduplicate, deep=%d, fuzzy=%d",
                    static_cast<int>(m_considerDeepTransforms),
                    static_cast<int>(m_fuzzy));
    }

    // Compute duplicate geometry
    std::vector<UsdPrim> prims;
    PrimVectors equalMeshVectors;
    computeEqualGeometrySets(prims, equalMeshVectors);

    if (equalMeshVectors.empty())
    {
        return { true };
    }

    // For reporting, log the duplicates.
    if (getContext()->generateReport && !getContext()->analysisMode)
    {
        for (const auto& primVector : equalMeshVectors)
        {
            SO_LOG_VERBOSE("Duplicates (%lu):", primVector.size());
            for (const auto& prim : primVector)
            {
                SO_LOG_VERBOSE("%s", prim.GetPrimPath().GetAsString().c_str());
            }
        }
    }

    // Based on the duplication method deduplicate the equal meshes.
    if (m_duplicateMethod == DuplicateOption::eSetAttribute)
    {
        // initialize set attribute to 0 (no belonging to a set) for all prims
        for (auto& prim : prims)
        {
            UsdAttribute attr = prim.CreateAttribute(_tokens->duplicationSet, SdfValueTypeNames->Int);
            attr.Set(0);
        }

        for (size_t setNr = 0; setNr < equalMeshVectors.size(); ++setNr)
        {
            auto& equalMeshes = equalMeshVectors[setNr];

            for (auto& setPrim : equalMeshes)
            {
                setPrim.GetAttribute(_tokens->duplicationSet).Set(static_cast<int>(setNr + 1));
            }
        }
    }
    else if (m_duplicateMethod == DuplicateOption::eCopyValues)
    {
        for (const auto& equalMeshes : equalMeshVectors)
        {
            _conformTopologyAttributeValues(equalMeshes);
        }
        return { true };
    }
    else
    {
        _conformUsingComposition(equalMeshVectors);
    }

    return { true };
}

} // namespace omni::scene::optimizer
