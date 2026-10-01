// SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "DeduplicateGeometry.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/CudaUtils.h>
#include <usd_optimize/core/JsonUtils.h>
#include <usd_optimize/core/MeshToolsCommon.h>
#include <usd_optimize/core/RemovePrims.h>
#include <usd_optimize/core/ResolveSdfPaths.h>
#include <usd_optimize/core/Utils.h>
#include <usd_optimize/core/geometry/Bucket.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/base/gf/math.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xformCache.h>

// TBB
#include <tbb/parallel_for.h>

// C++
#include <iostream>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::DeduplicateGeometryOperation);

namespace usd_optimize
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
    : Operation("deduplicateGeometry", "Deduplicate Geometry", "Convert identical meshes into instances.")
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
            { DuplicateOption::ePointInstancer, "Point Instancer" },
        });

    Argument& pointInstancerLocationArg = addArgument("pointInstancerLocation",
                                                      "Point Instancer Location",
                                                      kDisplayTypeEnum,
                                                      "Where to author the PointInstancer for each duplicate set",
                                                      m_pointInstancerLocation);
    pointInstancerLocationArg.setEnumValues<PointInstancerLocation>({
        { PointInstancerLocation::eCommonRoot, "Common Root" },
        { PointInstancerLocation::eCustomPath, "Custom Path" },
    });

    Argument& pointInstancerParentPathArg =
        addArgument("pointInstancerParentPath",
                    "Parent Path",
                    kDisplayTypePrimPath,
                    "Prim path to author the PointInstancer under. Created as an Xform if it does not exist.",
                    m_pointInstancerParentPath)
            .setPlaceholder("/World/PointInstancers")
            .setEnableIf("pointInstancerLocation == 1")
            .setVisibleIf("pointInstancerLocation == 1");

    Argument& minimumDuplicatesArg =
        addArgument("minimumDuplicates",
                    "Minimum Duplicates",
                    kDisplayTypeInt,
                    "Minimum number of duplicates a set must contain for a PointInstancer to be created. Sets with "
                    "fewer duplicates are left untouched.",
                    m_minimumDuplicates)
            .setMin(2);

    addGroup("pointInstancerOptions", pointInstancerLocationArg, pointInstancerParentPathArg, minimumDuplicatesArg)
        .setVisibleIf("duplicateMethod == 4");

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


std::string DeduplicateGeometryOperation::getDocumentation() const
{
    return R"DOC(This replaces multiple duplicate geometric prims in a scene with a single prim plus
references/instances to it. Since a referenced prim uses less memory than a full duplicated prim, this
can reduce both system memory and VRAM. It is only effective when there are prims that are identical but
not already instanced, so it may have no effect on a scene that is already well instanced.

A fuzzy comparison mode is also available: its similarity measure is independent of tessellation and
based on relative shape deviation, with CPU and GPU implementations. The operation deduplicates
point-based geometry (meshes, basis curves, etc.); in fuzzy mode only meshes are supported.

This is **mesh-level** deduplication: it matches individual gprims, not whole sub-trees. To collapse
duplicate assemblies (entire prim hierarchies), run :doc:`Deduplicate Hierarchies<deduplicateHierarchies>`
first, then this operation to catch any remaining loose duplicate meshes.

Matching controls
-----------------

``tolerance`` (default ``0.001``, stage units) is the position tolerance for considering two meshes
equal; use ``0`` to require exact matches. ``fuzzy`` enables shape-based matching; ``allowScaling`` lets
uniformly scaled copies match; ``considerDeepTransforms`` (default ``true``) lets two meshes match when
their stored points differ by a linear transform, instead of requiring the point arrays to agree
directly. ``minimumDuplicates`` (default ``2``) sets how many copies must exist before a prototype is
created. ``ignoreAttributes`` excludes named attributes from the comparison.

``considerDeepTransforms`` is meant to find more duplicates, but some data sets report more with it
disabled; if a scene with obvious repeats yields none, try both and compare the counts.

``ignoreAttributes`` applies only to the Reference, Instanceable Reference and Point Instancer
methods; for Copy Values and Set Attribute it is a no-op. Prefer a specific attribute over a
namespace, since one ending in ``:`` also excludes everything beneath it.

Recommended pipelines
---------------------

Frequently used in memory-reduction stacks alongside ``optimizeMaterials`` and ``pruneLeaves``, and after
``fitPrimitives`` so primitive-replaced meshes can also be deduplicated.

Starting configurations
-----------------------

Exact instancing (default method):

.. code-block:: json

    [{"operation": "deduplicateGeometry", "duplicateMethod": 2, "tolerance": 0.001}]

Fuzzy (tessellation-independent) matching:

.. code-block:: json

    [{"operation": "deduplicateGeometry", "duplicateMethod": 2, "fuzzy": true, "allowScaling": true}]
)DOC";
}


std::string DeduplicateGeometryOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion DeduplicateGeometryOperation::getVersion() const
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
        USD_OPTIMIZE_LOG_VERBOSE("Skipping %s because of time varying attributes", prim.GetPath().GetAsString().c_str());
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
    else if (method == DuplicateOption::ePointInstancer)
    {
        // Skip meshes that are already serving as prototypes for a PointInstancer. After this mode runs, the
        // surviving prototype mesh lives directly under a PointInstancer; we don't want to deduplicate those again.
        const UsdPrim parent = prim.GetParent();
        if (parent && parent.IsA<UsdGeomPointInstancer>())
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


// Captures a prototype mesh's point/topology data (and, for the exact path, its precomputed origin-to-pivot basis) so
// the "deep transform" that maps prototype-local points onto each duplicate's local points can be computed without
// re-reading or re-deriving the prototype side per duplicate. Duplicate detection matches meshes up to such a
// transform, so both _conformUsingComposition and _createPointInstancers need this for every duplicate in a set.
class _DeepTransformSolver
{
public:
    _DeepTransformSolver(const UsdPrim& prototypePrim, bool fuzzy)
        : m_fuzzy(fuzzy)
    {
        VtVec3fArray points;
        UsdGeomPointBased(prototypePrim).GetPointsAttr().Get(&points);

        if (m_fuzzy)
        {
            // All prims are guaranteed to be UsdGeomMesh in fuzzy mode (filtered by _isSupportedPrim). The solver
            // caches the prototype's OBB so it is built once for the whole set rather than once per duplicate.
            VtIntArray faceVertexIndices;
            VtIntArray faceVertexCounts;
            UsdGeomMesh mesh(prototypePrim);
            mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);
            mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
            m_fuzzySolver = std::make_unique<FuzzyTransformSolver>(points, faceVertexIndices, faceVertexCounts);
        }
        else
        {
            // The prototype is the source for every duplicate, so hoist its origin-to-pivot inverse here.
            m_originToPivotInverse = _getOriginToPivotMatrix(points.AsConst()).GetInverse();
        }
    }

    // Transform that maps the prototype's local points onto the given duplicate's local points.
    GfMatrix4d computeTransformTo(const UsdPrim& duplicatePrim) const
    {
        VtVec3fArray points;
        UsdGeomPointBased(duplicatePrim).GetPointsAttr().Get(&points);

        if (m_fuzzy)
        {
            VtIntArray faceVertexIndices;
            VtIntArray faceVertexCounts;
            UsdGeomMesh mesh(duplicatePrim);
            mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);
            mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts);
            return m_fuzzySolver->computeTransformTo(points, faceVertexIndices, faceVertexCounts);
        }

        // Equivalent to _getTransformFromTo(prototypePoints, points) but reuses the hoisted prototype basis.
        return m_originToPivotInverse * _getOriginToPivotMatrix(points.AsConst());
    }

private:
    bool m_fuzzy;
    GfMatrix4d m_originToPivotInverse{ 1.0 };
    std::unique_ptr<FuzzyTransformSolver> m_fuzzySolver;
};


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
            USD_OPTIMIZE_LOG_VERBOSE("Ignoring namespace %s", attributeName.c_str());
        }
        else
        {
            ignoreAttributes.emplace_back(TfToken(attributeName));
            USD_OPTIMIZE_LOG_VERBOSE("Ignoring attribute %s", attributeName.c_str());
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

            // For the composition methods the material binding is left on the prim that holds the composition arc and
            // inherited by the mesh below, so meshes that differ only by material can still share a prototype and we
            // do not consider the bound material when bucketing. A PointInstancer has no per-instance binding, so for
            // that method we let the bucketer split sets by resolved bound material -- each prototype is then
            // material-homogeneous. ComputeBoundMaterial also resolves inherited and collection-based bindings.
            bucketer.SetConsiderMaterials(m_duplicateMethod == DuplicateOption::ePointInstancer);

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

        // The prototype is the source for every duplicate's deep transform; the solver caches its point/topology
        // data so we don't re-read or re-derive the prototype side per duplicate.
        const _DeepTransformSolver solver(prototypePrim, m_fuzzy);

        for (size_t duplicateIndex = 0; duplicateIndex < duplicateCount; ++duplicateIndex)
        {
            const auto& prim = duplicatePrims[duplicateIndex];
            targetMatrices.push_back(solver.computeTransformTo(prim));
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

                // Place the composition transform so it runs *after* any pivot pair in execution
                // (i.e. before the first forward pivot op in the xformOpOrder list — USD applies
                // last-listed ops first). That keeps the pivot operations in prototype-local
                // space, where the geometry actually lives, so the pivot value reads the same
                // when transformed naively through the local-to-world. When there is no pivot
                // pair the composition transform is the most-local op and goes at the end.
                const std::vector<UsdGeomXformOp> pivotOps = _getPivotXformOps(xformOps, /*includeInverseOps=*/false);
                const TfToken firstPivotName = pivotOps.empty() ? TfToken() : pivotOps.front().GetOpName();

                VtTokenArray newXformOpOrder;
                newXformOpOrder.reserve(xformOpOrder.size() + 1);
                bool insertedCompositionOp = false;
                for (const TfToken& token : xformOpOrder)
                {
                    if (!insertedCompositionOp && !firstPivotName.IsEmpty() && token == firstPivotName)
                    {
                        newXformOpOrder.push_back(_tokens->compositionXformOp);
                        insertedCompositionOp = true;
                    }
                    newXformOpOrder.push_back(token);
                }
                if (!insertedCompositionOp)
                {
                    newXformOpOrder.push_back(_tokens->compositionXformOp);
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

                // Copy authored properties from the Mesh to the new Xform.
                bool hasMaterialBindingOnXform = false;
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
                        if (!property.Is<UsdAttribute>() &&
                            propertyName.GetString() == UsdShadeTokens->materialBinding.GetString())
                        {
                            hasMaterialBindingOnXform = true;
                        }

                        // Pivots need their value remapped into prototype-local space so the !pivot/pivot
                        // pair operates on the prototype's raw points (the dedupTransform runs after the
                        // pivot pair in execution). Without this, a viewer that draws the pivot widget
                        // at `pivot_local * L2W` would see it shifted by the dedupTransform's offset.
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

                            if (gotPivotVal)
                            {
                                // Map the pivot from target-local space back into prototype-local space.
                                pivotVal = xform.GetInverse().Transform(pivotVal);

                                // Preserve original type: convert back to vec3f if needed.
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

                // Move apiSchemas from the Mesh to the Xform. Inheritable properties like material bindings are
                // moved to the Xform, so their associated schemas must follow.
                {
                    VtValue apiSchemasValue;
                    SdfListOp<TfToken> xformApiSchemas;
                    if (prim.GetMetadata(UsdTokens->apiSchemas, &apiSchemasValue) && !apiSchemasValue.IsEmpty())
                    {
                        xformApiSchemas = apiSchemasValue.Get<SdfListOp<TfToken>>();
                    }

                    // Safety check: ensure MaterialBindingAPI is present if a material binding was moved to the Xform.
                    if (hasMaterialBindingOnXform)
                    {
                        _addMaterialBindingAPIToSchemas(xformApiSchemas);
                    }

                    if (!xformApiSchemas.GetPrependedItems().empty() || !xformApiSchemas.GetExplicitItems().empty() ||
                        !xformApiSchemas.GetAppendedItems().empty() || !xformApiSchemas.GetDeletedItems().empty() ||
                        !xformApiSchemas.GetOrderedItems().empty())
                    {
                        tempSpec->SetInfo(UsdTokens->apiSchemas, VtValue(xformApiSchemas));
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
                    USD_OPTIMIZE_LOG_WARN("Invalid deduplicate option: %i", static_cast<int>(m_duplicateMethod));
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

    USD_OPTIMIZE_LOG_INFO("Replaced %zu meshes with references", numReferences);
}


// _copyPrim only carries bindings authored on the mesh itself, so a prototype that inherited its material from an
// ancestor (or via a collection) would lose it once re-parented under the PointInstancer. Resolve the effective bound
// material for each purpose on the source mesh and author it directly on the prototype copy so the binding survives
// the move. A directly-authored binding that _copyPrim already carried is simply re-authored to the same value.
void _rebindResolvedMaterials(const UsdPrim& sourcePrim, const UsdPrim& targetPrim)
{
    if (!sourcePrim || !targetPrim)
    {
        return; // LCOV_EXCL_LINE
    }

    const UsdShadeMaterialBindingAPI sourceBinding(sourcePrim);
    UsdShadeMaterialBindingAPI targetBinding(targetPrim);
    bool applied = false;

    // GetMaterialPurposes() returns allPurpose first, then the purpose-specific tokens. Purpose-specific lookups fall
    // back to the all-purpose binding, so we only re-author one when it resolves to a different material.
    SdfPath allPurposePath;
    for (const TfToken& purpose : UsdShadeMaterialBindingAPI::GetMaterialPurposes())
    {
        const UsdShadeMaterial material = sourceBinding.ComputeBoundMaterial(purpose);
        if (!material)
        {
            continue;
        }

        if (purpose == UsdShadeTokens->allPurpose)
        {
            allPurposePath = material.GetPath();
        }
        else if (material.GetPath() == allPurposePath)
        {
            continue;
        }

        if (!applied)
        {
            targetBinding = UsdShadeMaterialBindingAPI::Apply(targetPrim);
            applied = true;
        }
        targetBinding.Bind(material, UsdShadeTokens->fallbackStrength, purpose);
    }
}


// Decompose an affine transform into the translate / rotate / scale triple a UsdGeomPointInstancer stores per
// instance, writing the (best-fit) triple to the out-params. Returns true only when that triple actually reproduces
// the matrix: ComputeInstanceTransformsAtTime applies scale, then rotation, then translation, so it cannot represent
// shear or a scaleOrientation (a rotation composed with non-uniform scale). Callers must not author an instance for
// which this returns false -- the placement would be visibly wrong.
bool _decomposeForPointInstancer(const GfMatrix4d& matrix, GfVec3f& position, GfQuath& orientation, GfVec3f& scale)
{
    // Factor the matrix into scaleOrientation * scale * rotation * translation (plus an unused projection part).
    // rotMat is the rotation, scaleVec the scale, translation the position; the scaleOrientation -- the orientation of
    // a non-uniform scale, i.e. shear -- is the one part a PointInstancer (scale, then rotate, then translate) cannot
    // reproduce. Factor is cheaper than GfTransform::SetMatrix, which calls Factor then redoes pivot math we never use.
    GfMatrix4d scaleOrientMat, rotMat, projMat;
    GfVec3d scaleVec, translation;
    matrix.Factor(&scaleOrientMat, &scaleVec, &rotMat, &translation, &projMat);

    const GfQuatd rotation = rotMat.ExtractRotationQuat();
    position = GfVec3f(translation);
    scale = GfVec3f(scaleVec);
    orientation = GfQuath(GfQuatf(rotation));

    // Decide representability by rebuilding the matrix exactly as ComputeInstanceTransformsAtTime will (scale, then
    // rotate, then translate) and comparing its linear (3x3) part to the original. Do NOT test scaleOrientMat against
    // identity directly: for a uniform scale (the common rigid / similarity case) the scaleOrientation is irrelevant,
    // yet Factor's Jacobi eigensolve has repeated singular values there and returns an *arbitrary* large rotation for
    // it from infinitesimal noise -- a value that differs between x86 and aarch64. The reconstruction cancels that
    // spurious orientation (for uniform scale rotMat collapses to the true rotation), so it measures only what a
    // PointInstancer genuinely cannot represent: shear / rotated non-uniform scale. Translation is exact, so comparing
    // just the 3x3 also avoids tolerance issues from large translations.
    const GfMatrix4d reconstructed = GfMatrix4d(1.0).SetScale(scaleVec) * GfMatrix4d(1.0).SetRotate(rotation) *
                                     GfMatrix4d(1.0).SetTranslate(translation);

    double maxComponent = 1.0;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            maxComponent = GfMax(maxComponent, GfAbs(matrix[i][j]));
        }
    }

    const double tolerance = 1e-5 * maxComponent;
    for (int i = 0; i < 3; ++i)
    {
        for (int j = 0; j < 3; ++j)
        {
            if (GfAbs(matrix[i][j] - reconstructed[i][j]) > tolerance)
            {
                return false;
            }
        }
    }
    return true;
}


// Replace each set of duplicate meshes with a UsdGeomPointInstancer that uses one of the meshes as its prototype.
// Each duplicate becomes a single instance whose position / orientation / scale reproduces the original worldspace
// placement of the mesh it replaces. Sets are material-homogeneous here: _findIdenticalMeshes buckets by bound
// material for this method, so meshes with different materials arrive as separate sets (separate PointInstancers).
// Duplicates whose placement needs shear or rotated non-uniform scale are left as meshes (a PointInstancer cannot
// represent them); a set with fewer than two representable duplicates is skipped entirely.
void DeduplicateGeometryOperation::_createPointInstancers(const PrimVectors& duplicatePrimVectors)
{
    const UsdStageWeakPtr stage = getUsdStage();
    const SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();

    UsdGeomXformCache xformCache;
    size_t numReplaced = 0;
    size_t numInstancers = 0;

    for (const PrimVector& duplicatePrims : duplicatePrimVectors)
    {
        // A PointInstancer is only worthwhile for a set with at least m_minimumDuplicates duplicates; smaller sets
        // are left as their original meshes.
        if (duplicatePrims.size() < static_cast<size_t>(m_minimumDuplicates))
        {
            continue;
        }

        // Resolve where the new PointInstancer should be authored. The custom-path case is pre-validated in
        // executeImpl so we can construct the SdfPath directly here.
        SdfPath parentPath;
        if (m_pointInstancerLocation == PointInstancerLocation::eCustomPath)
        {
            parentPath = SdfPath(m_pointInstancerParentPath);
        }
        else
        {
            // Compute the deepest common ancestor of the duplicate meshes' parents.
            parentPath = duplicatePrims.front().GetPath().GetParentPath();
            for (size_t i = 1; i < duplicatePrims.size(); ++i)
            {
                parentPath = parentPath.GetCommonPrefix(duplicatePrims[i].GetPath().GetParentPath());
            }
            if (parentPath.IsEmpty())
            {
                parentPath = SdfPath::AbsoluteRootPath();
            }
        }

        // Ensure the parent prim exists. In common-root mode it always does (it's an ancestor of the duplicates); in
        // custom-path mode it may be missing, so author it -- and any missing intermediate ancestors -- as Xforms via
        // the shared helper so they get a consistent specifier/type in the edit layer.
        UsdPrim parentPrim;
        if (parentPath == SdfPath::AbsoluteRootPath())
        {
            parentPrim = stage->GetPseudoRoot();
        }
        else
        {
            parentPrim = stage->GetPrimAtPath(parentPath);
            if (!parentPrim || !parentPrim.IsDefined())
            {
                SdfLayerHandle parentLayer = editLayer;
                _safeCreatePrim(stage,
                                parentPath,
                                _tokens->typeXform.GetString(),
                                _tokens->typeXform.GetString(),
                                parentLayer);
                parentPrim = stage->GetPrimAtPath(parentPath);
                if (!parentPrim)
                {
                    USD_OPTIMIZE_LOG_WARN("Failed to create PointInstancer parent at '%s'; skipping duplicate set",
                                          parentPath.GetAsString().c_str());
                    continue; // LCOV_EXCL_LINE
                }
            }
        }

        // The PointInstancer itself has identity local transform, so its local-to-world equals the parent's. We map
        // each duplicate's local-to-world into the PointInstancers local space via this inverse. XformCache returns
        // identity for the pseudo-root, so this is correct when the parent is the pseudo-root too.
        const GfMatrix4d parentWorldToLocal = xformCache.GetLocalToWorldTransform(parentPrim).GetInverse();

        // Pick the prototype source. Whichever prim we copy under the PointInstancer must itself become an instance
        // (it has an identity deep transform, so its instance transform is just its own placement) and so be deleted;
        // otherwise its mesh data would be left in the scene alongside the copy. Only a prim whose own placement a
        // PointInstancer can represent can play that role, so use the first such prim and keep its decomposed
        // transform for the pass below. If none qualifies, no leftover-free PointInstancer is possible -- skip the set.
        VtVec3fArray positions;
        VtQuathArray orientations;
        VtVec3fArray scales;
        std::vector<UsdPrim> instancedPrims;
        positions.reserve(duplicatePrims.size());
        orientations.reserve(duplicatePrims.size());
        scales.reserve(duplicatePrims.size());
        instancedPrims.reserve(duplicatePrims.size());

        UsdPrim prototypeSource;
        GfVec3f prototypePosition;
        GfQuath prototypeOrientation;
        GfVec3f prototypeScale;

        for (const UsdPrim& candidate : duplicatePrims)
        {
            const GfMatrix4d ownXform = xformCache.GetLocalToWorldTransform(candidate) * parentWorldToLocal;
            if (_decomposeForPointInstancer(ownXform, prototypePosition, prototypeOrientation, prototypeScale))
            {
                prototypeSource = candidate;
                break;
            }
        }

        if (!prototypeSource)
        {
            USD_OPTIMIZE_LOG_WARN(
                "Skipping PointInstancer for duplicate set under '%s': no duplicate's placement can be represented",
                parentPath.GetAsString().c_str());
            continue;
        }
        const TfToken prototypeName = prototypeSource.GetName();

        // The prototype is the source for every duplicate's deep transform (which maps prototype-local points onto
        // each duplicate's local points). The duplicate detection matches meshes up to such a transform, so when a
        // duplicate's local points differ from the prototype's we bake that delta into the per-instance transform.
        const _DeepTransformSolver solver(prototypeSource, m_fuzzy);

        // First pass (no stage edits yet): keep each duplicate a PointInstancer can faithfully reproduce. A duplicate
        // whose placement needs shear or a scaleOrientation is left as its original mesh rather than authored as a
        // visibly-wrong instance. Each prim is decomposed exactly once -- the prototype's transform was already
        // computed during selection above, so it is reused here rather than recomputed.
        for (const UsdPrim& dupPrim : duplicatePrims)
        {
            GfVec3f position;
            GfQuath orientation;
            GfVec3f scale;

            if (dupPrim == prototypeSource)
            {
                position = prototypePosition;
                orientation = prototypeOrientation;
                scale = prototypeScale;
            }
            else
            {
                // USD row-vector convention; most-local first. Apply the deep transform to bring prototype-local
                // points into the duplicate's local frame, then the duplicate's own local-to-world, then map back into
                // the PointInstancer's local space (PI is at identity in its parent, so this is its world-to-local).
                const GfMatrix4d deepTransform = solver.computeTransformTo(dupPrim);
                const GfMatrix4d meshLocalToWorld = xformCache.GetLocalToWorldTransform(dupPrim);
                const GfMatrix4d instanceXform = deepTransform * meshLocalToWorld * parentWorldToLocal;
                if (!_decomposeForPointInstancer(instanceXform, position, orientation, scale))
                {
                    USD_OPTIMIZE_LOG_WARN(
                        "Leaving '%s' as a mesh: its placement needs shear or rotated non-uniform scale, which a "
                        "PointInstancer cannot represent",
                        dupPrim.GetPath().GetAsString().c_str());
                    continue;
                }
            }

            positions.push_back(position);
            orientations.push_back(orientation);
            scales.push_back(scale);
            instancedPrims.push_back(dupPrim);
        }

        // Check minDuplicates again against the final number of prims
        if (instancedPrims.size() < static_cast<size_t>(m_minimumDuplicates))
        {
            continue;
        }

        // Pick a unique name for the new PointInstancer under the chosen parent. _getUniqueChildPaths accounts for
        // existing children (including deactivated ones) and matches the naming convention used across the library.
        const TfToken baseName(prototypeName.GetString() + "_Instancer");
        const SdfPath instancerPath = _getUniqueChildPaths(stage, parentPath, { baseName }).front();

        // Create the PointInstancer and copy the prototype mesh underneath it as a direct child. Copying via the
        // shared helper preserves the prototype's material binding, primvars and api schemas.
        UsdGeomPointInstancer pointInstancer = UsdGeomPointInstancer::Define(stage, instancerPath);
        if (!pointInstancer)
        {
            USD_OPTIMIZE_LOG_WARN("Failed to create PointInstancer at '%s'; skipping duplicate set",
                                  instancerPath.GetAsString().c_str());
            continue; // LCOV_EXCL_LINE
        }

        const SdfPath prototypePath = instancerPath.AppendChild(prototypeName);
        _copyPrim(prototypeSource, editLayer, prototypePath);
        const UsdPrim prototypePrim = stage->GetPrimAtPath(prototypePath);

        // The prototype sits at identity so its worldspace contribution comes from the PointInstancers parent only.
        // Strip any xform ops and the xformOpOrder that were copied from the source mesh.
        _clearXformOps(prototypePrim);

        // Re-author the source mesh's resolved material binding so an inherited or collection-based binding is not
        // lost now that the prototype no longer sits under its original ancestors.
        _rebindResolvedMaterials(prototypeSource, prototypePrim);

        // Only one prototype is authored per PointInstancer, so every protoIndex is 0.
        const VtIntArray protoIndices(instancedPrims.size(), 0);

        pointInstancer.CreatePositionsAttr().Set(positions);
        pointInstancer.CreateOrientationsAttr().Set(orientations);
        pointInstancer.CreateScalesAttr().Set(scales);
        pointInstancer.CreateProtoIndicesAttr().Set(protoIndices);
        pointInstancer.CreatePrototypesRel().AddTarget(prototypePath);

        // Remove only the duplicates we actually instanced; any left as meshes (not representable) stay in place. Use
        // the shared helper so prims defined outside the edit layer fall back to deactivation instead of being skipped.
        _deletePrims(stage, instancedPrims, /*deactivate=*/true);

        numReplaced += instancedPrims.size();
        numInstancers += 1;
    }

    std::string suffix = numInstancers == 1 ? "" : "s";
    USD_OPTIMIZE_LOG_INFO("Replaced %zu meshes with %zu PointInstancer%s", numReplaced, numInstancers, suffix.c_str());
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
        USD_OPTIMIZE_LOG_INFO("Found no prims to process");
        return;
    }

    // Compute sets of meshes that can be treated as duplicates.
    constexpr bool ignoreNormals = false;

    if (m_useGpu && !isCudaAvailable())
    {
        USD_OPTIMIZE_LOG_WARN("GPU requested but CUDA is not available. Falling back to CPU.");
        m_useGpu = false;
    }

    primVectors = m_fuzzy ? _computeEqualMeshPrimsFuzzy(resolvedPrims, m_tolerance, m_allowScaling, m_useGpu) :
                            _computeEqualMeshPrims(resolvedPrims, m_considerDeepTransforms, m_tolerance, ignoreNormals);

    // If we are using composition to deduplicate or replacing duplicates with a PointInstancer we need to ensure that
    // all attributes on the prims are equal not just the topology attributes. By comparing attribute values on the
    // prims within the equal prim sets we can divide the sets into subsets of prims.
    switch (m_duplicateMethod)
    {
    case DuplicateOption::eReference:
    case DuplicateOption::eInstanceableReference:
    case DuplicateOption::ePointInstancer:
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
    USD_OPTIMIZE_LOG_INFO("Found %lu set%s of equal meshes", primVectors.size(), suffix.c_str());
}


OperationResult DeduplicateGeometryOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|DeduplicateGeometry|Analysis");

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

    USD_OPTIMIZE_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult DeduplicateGeometryOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|DeduplicateGeometry|Execute");

    if (getContext()->generateReport)
    {
        USD_OPTIMIZE_LOG_INFO("Running deduplicate, deep=%d, fuzzy=%d",
                              static_cast<int>(m_considerDeepTransforms),
                              static_cast<int>(m_fuzzy));
    }

    // Fail fast on bad configs before spending time finding duplicates. In Create-PointInstancer mode with a custom
    // parent path, the path must be a non-empty absolute prim path -- there is no recoverable per-set fallback.
    if (m_duplicateMethod == DuplicateOption::ePointInstancer &&
        m_pointInstancerLocation == PointInstancerLocation::eCustomPath)
    {
        if (m_pointInstancerParentPath.empty())
        {
            USD_OPTIMIZE_LOG_WARN("PointInstancer parent path is empty");
            return { false };
        }
        if (!SdfPath::IsValidPathString(m_pointInstancerParentPath))
        {
            USD_OPTIMIZE_LOG_WARN("Invalid PointInstancer parent path '%s'", m_pointInstancerParentPath.c_str());
            return { false };
        }
        const SdfPath parentPath(m_pointInstancerParentPath);
        if (!parentPath.IsAbsolutePath() || !parentPath.IsAbsoluteRootOrPrimPath())
        {
            USD_OPTIMIZE_LOG_WARN("PointInstancer parent path '%s' is not an absolute prim path",
                                  m_pointInstancerParentPath.c_str());
            return { false };
        }
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
            USD_OPTIMIZE_LOG_VERBOSE("Duplicates (%lu):", primVector.size());
            for (const auto& prim : primVector)
            {
                USD_OPTIMIZE_LOG_VERBOSE("%s", prim.GetPrimPath().GetAsString().c_str());
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
    else if (m_duplicateMethod == DuplicateOption::ePointInstancer)
    {
        _createPointInstancers(equalMeshVectors);
    }
    else
    {
        _conformUsingComposition(equalMeshVectors);
    }

    return { true };
}

} // namespace usd_optimize
