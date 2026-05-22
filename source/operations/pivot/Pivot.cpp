// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "Pivot.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/ComputeExtents.h>
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/TransformUtils.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usdGeom/bboxCache.h>
#include <pxr/usd/usdGeom/xformCommonAPI.h>

// TBB
#include <tbb/parallel_for.h>

// C++
#include <iostream>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::PivotOperation);

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (Xform)
);
// LCOV_EXCL_STOP
// clang-format on


namespace omni::scene::optimizer
{

template <class ValueType>
inline bool setXformOpValue(UsdGeomXformOp& xformOp, const ValueType& value, const UsdTimeCode& timeCode)
{
    typedef GfVec3f FloatType;
    typedef GfVec3f HalfType;
    typedef GfVec3d DoubleType;

    bool result = false;

    switch (xformOp.GetPrecision())
    {
    // LCOV_EXCL_START
    case UsdGeomXformOp::PrecisionHalf:
        result = xformOp.Set(HalfType(FloatType(value)), timeCode);
        break;
    // LCOV_EXCL_STOP
    case UsdGeomXformOp::PrecisionFloat:
        result = xformOp.Set(FloatType(value), timeCode);
        break;
    case UsdGeomXformOp::PrecisionDouble:
        result = xformOp.Set(DoubleType(value), timeCode);
        break;
    }

    return result;
}


static bool _isXform(const UsdPrim& prim)
{
    return prim.GetTypeName() == _tokens->Xform;
}


/// Struct to store pivot information
struct PivotData
{
    GfVec3d translation;
    GfVec3f centroid;
    UsdGeomXformCommonAPI::RotationOrder rotOrder;
    GfVec3f rotation;
    GfVec3f scale;
    bool resetsXformStack;
    bool valid = false;
};


void PivotOperation::pivot(const std::vector<UsdPrim>& prims) const
{

    // TODO: deal with times
    constexpr UsdTimeCode time = UsdTimeCode::Default();

    std::vector<PivotData> pivots;
    pivots.resize(prims.size());

    std::atomic<size_t> processed = 0;

    auto pivotFn = [&](const tbb::blocked_range<size_t>& range)
    {
        UsdGeomXformCache xformCache;
        UsdGeomBBoxCache bboxCache(time, { UsdGeomTokens->default_ });

        for (size_t i = range.begin(); i < range.end(); ++i)
        {

            const auto& prim = prims[i];

            // We won't touch instanced proxies.
            if (prim.IsInstanceProxy())
            {
                continue;
            }

            UsdGeomMesh mesh(prim);

            // If we are applying to meshes + xforms, check it is one or the other.
            // In ApplyTo::eMeshes, the prims were already filtered with meshesOnly
            // when resolving the paths.
            if (m_applyTo == ApplyTo::eMeshesAndXforms)
            {
                if (!_isXform(prim) && !mesh)
                {
                    continue;
                }
            }

            // By default we don't overwrite existing pivots
            if (!m_overwrite && _containsOrderedXformOpsSuffix(prim, UsdGeomTokens->pivot))
            {
                if (getContext()->verbose)
                {
                    SO_LOG_VERBOSE("%s: Skipping due to existing authored pivot",
                                   prim.GetPrimPath().GetAsString().c_str());
                }

                continue;
            }

            // For now, we do not touch time varying prims.
            if (_mightBeTimeVarying(prim))
            {
                if (getContext()->verbose)
                {
                    SO_LOG_VERBOSE("%s: Skipping due to time samples", prim.GetPrimPath().GetAsString().c_str());
                }

                continue;
            }

            const auto& xformable = UsdGeomXformable(prim);
            if (!xformable)
            {
                continue;
            }

            const auto& xformCommonApi = UsdGeomXformCommonAPI(prim);
            if (!xformCommonApi)
            {
                continue;
            }

            GfVec3f centroid;

            if (mesh)
            {
                // Compute centroid, that is, new pivot.
                if (m_method == Method::eWeighted)
                {
                    VtVec3fArray points;
                    if (!mesh.GetPointsAttr().Get(&points, time) || points.empty())
                    {
                        if (getContext()->verbose)
                        {
                            SO_LOG_WARN("%s contains no points, skipping", prim.GetPrimPath().GetAsString().c_str());
                        }
                        continue;
                    }

                    centroid = _computeCentroid(points);
                }
                else
                {
                    auto bbox = bboxCache.ComputeLocalBound(prim);

                    // Check there is a valid bound, as the function above can return an empty bound
                    // if e.g. there are no points.
                    if (bbox.GetRange().IsEmpty())
                    {
                        continue;
                    }

                    // Use the range midpoint, rather than ComputeCentroid (which will be transformed).
                    centroid = static_cast<GfVec3f>(bbox.GetRange().GetMidpoint());
                }
            }
            else
            {
                // For xforms, work out the overall centroid of any mesh descendants.
                std::string expression = prim.GetPrimPath().GetAsString() + "//";
                constexpr bool meshesOnly = true;
                constexpr bool reverse = false; // don't care what order they are in
                Usd_PrimFlagsPredicate predicate = UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate);
                std::vector<UsdPrim> meshPrims =
                    _resolveExpressionsToPrims(prim, { expression }, meshesOnly, reverse, predicate);

                if (meshPrims.empty())
                {
                    SO_LOG_WARN("%s contains no meshes, skipping", prim.GetPrimPath().GetAsString().c_str());
                    continue;
                }

                VtVec3fArray centroids;
                VtVec3fArray points;
                GfBBox3d relativeBBox;

                for (const auto& meshPrim : meshPrims)
                {
                    if (m_method == Method::eWeighted)
                    {
                        bool _resetXFormStack;
                        GfMatrix4d relativeXform = xformCache.ComputeRelativeTransform(meshPrim, prim, &_resetXFormStack);

                        UsdGeomMesh childMesh(meshPrim);

                        if (!childMesh.GetPointsAttr().Get(&points, time) || points.empty())
                        {
                            if (getContext()->verbose)
                            {
                                SO_LOG_WARN("%s contains no points, skipping",
                                            meshPrim.GetPrimPath().GetAsString().c_str());
                            }
                            continue;
                        }

                        GfVec3f childCentroid = _computeCentroid(points);

                        GfVec3f relativeCentroid(relativeXform.Transform(childCentroid));
                        centroids.push_back(relativeCentroid);
                    }
                    else
                    {
                        GfBBox3d childBBox = bboxCache.ComputeRelativeBound(meshPrim, prim);
                        relativeBBox = GfBBox3d::Combine(relativeBBox, childBBox);
                    }
                }

                if (m_method == Method::eWeighted)
                {
                    // An xform with meshes underneath it, none of which have points
                    if (centroids.empty())
                    {
                        SO_LOG_WARN("%s contains no geometry, skipping", prim.GetPrimPath().GetAsString().c_str());
                        continue;
                    }

                    // Compute the centroid of the centroids.
                    centroid = _computeCentroid(centroids);
                }
                else
                {
                    // Center of BBox
                    centroid = static_cast<GfVec3f>(relativeBBox.ComputeCentroid());
                }
            }

            auto& pivotData = pivots[i];

            // Get the local transformation matrix.
            GfMatrix4d localMatrix;
            GfVec3f pivot;
            xformable.GetLocalTransformation(&localMatrix, &pivotData.resetsXformStack);

            // Read the existing data
            xformCommonApi.GetXformVectorsByAccumulation(&pivotData.translation,
                                                         &pivotData.rotation,
                                                         &pivotData.scale,
                                                         &pivot,
                                                         &pivotData.rotOrder,
                                                         time);

            // If the prim has an existing authored pivot, then adjust that out prior to correcting for the
            // new pivot
            if (_containsOrderedXformOpsSuffix(prim, UsdGeomTokens->pivot))
            {
                pivotData.translation = (pivotData.translation + localMatrix.Transform(-pivot) + pivot) / 2;
            }

            // Compute corrected translation.
            pivotData.translation = 2 * pivotData.translation - localMatrix.Transform(-centroid) - centroid;

            // Record the new pivot, and mark as valid.
            pivotData.centroid = centroid;
            pivotData.valid = true;
        }
    };

    // Calculate all pivots
    tbb::parallel_for(tbb::blocked_range<size_t>(0, prims.size()), pivotFn);

    // Finds or adds the requested UsdGeomXformOp
    auto findOrCreate = [](const UsdGeomXformable& xformable,
                           const UsdGeomXformOp::Type& xformOpType,
                           const TfToken& opSuffix,
                           UsdGeomXformOp& outXformOp)
    {
        outXformOp = xformable.GetXformOp(xformOpType, opSuffix);
        if (outXformOp)
        {
            return true;
        }

        // Not found, so create.
        auto opAttr = xformable.GetPrim().CreateAttribute(
            UsdGeomXformOp::GetOpName(xformOpType, opSuffix),
            UsdGeomXformOp::GetValueTypeName(xformOpType, UsdGeomXformOp::PrecisionDouble),
            false);
        outXformOp = UsdGeomXformOp(opAttr);
        return static_cast<bool>(outXformOp);
    };

    // We may be applying pivots to prims that don't exist in the current layer yet, for example
    // if we would need an Over to author on to a reference. In order to use a ChangeBlock to set
    // all the xform data, we need to ensure those prim specs exist first.
    const auto& layer = getUsdStage()->GetEditTarget().GetLayer();
    {
        SdfChangeBlock _changeBlock;
        for (size_t idx = 0; idx < prims.size(); ++idx)
        {
            if (!pivots[idx].valid)
            {
                continue;
            }

            // Create primSpec in the edit target, if it doesn't already exist.
            if (!SdfJustCreatePrimInLayer(layer, prims[idx].GetPrimPath()))
            {
                SO_LOG_WARN("Failed to create PrimSpec at %s", prims[idx].GetPrimPath().GetAsString().c_str());
                pivots[idx].valid = false;
            }
        }
    }

    std::vector<UsdGeomXformOp> XformOpOrder;
    const TfToken emptyToken;

    // Given we created any required prim specs above, we should now be able to apply all the
    // xform edits in a single change block.
    {
        SdfChangeBlock _changeBlock;

        for (size_t idx = 0; idx < prims.size(); ++idx)
        {
            const auto& pivotData = pivots[idx];

            // Skip if previously marked as invalid
            if (!pivotData.valid)
            {
                continue;
            }

            XformOpOrder.clear();
            const auto& prim = prims[idx];
            const auto& xformable = UsdGeomXformable(prim);

            UsdGeomXformOp XformOp;

            findOrCreate(xformable, UsdGeomXformOp::TypeTranslate, emptyToken, XformOp);
            setXformOpValue(XformOp, pivotData.translation, time);
            XformOpOrder.push_back(XformOp);

            findOrCreate(xformable, UsdGeomXformOp::TypeTranslate, UsdGeomTokens->pivot, XformOp);
            setXformOpValue(XformOp, pivotData.centroid, time);
            XformOpOrder.push_back(XformOp);

            UsdGeomXformOp::Type rotType = UsdGeomXformCommonAPI::ConvertRotationOrderToOpType(pivotData.rotOrder);
            findOrCreate(xformable, rotType, emptyToken, XformOp);
            setXformOpValue(XformOp, pivotData.rotation, time);
            XformOpOrder.push_back(XformOp);

            findOrCreate(xformable, UsdGeomXformOp::TypeScale, emptyToken, XformOp);
            setXformOpValue(XformOp, pivotData.scale, time);
            XformOpOrder.push_back(XformOp);

            XformOpOrder.push_back(UsdGeomXformOp(XformOpOrder[1], true));
            xformable.SetXformOpOrder(XformOpOrder);
            xformable.SetResetXformStack(pivotData.resetsXformStack);

            if (getContext()->verbose)
            {
                SO_LOG_VERBOSE("Applied pivot to %s", prim.GetPrimPath().GetAsString().c_str());
            }

            ++processed;
        }
    }

    std::string suffix = processed == 1 ? "" : "s";
    SO_LOG_INFO("Applied pivot to %d prim%s", static_cast<size_t>(processed), suffix.c_str());
}


constexpr const char* s_category = "PIVOT";


PivotOperation::PivotOperation()
    : Operation("pivot",
                "Compute Pivot",
                "Compute Pivot will place the parent transform at the center of the bounding box of the target "
                "prim, think of this as creating a center pivot in other DCC tools.")
{

    addArgument("meshPrimPaths",
                "Prims To Process",
                kDisplayTypePrimPaths,
                "Optional list of prim paths or expressions to consider",
                m_primPaths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("overwrite",
                "Overwrite Authored Pivots",
                kDisplayTypeBool,
                "If enabled, overwrite existing authored pivot attributes.",
                m_overwrite);

    addArgument("applyTo", "Apply To", kDisplayTypeEnum, "What type of prims to apply a pivot to", m_applyTo)
        .setEnumValues<ApplyTo>({ { ApplyTo::eMeshes, "Meshes" }, { ApplyTo::eMeshesAndXforms, "Meshes and Xforms" } });

    addArgument("method", "Method", kDisplayTypeEnum, "Method of determining the new pivot", m_method)
        .setEnumValues<Method>({ { Method::eWeighted, "Weighted" }, { Method::eCenter, "Bounding Box Center" } });
}


std::string PivotOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion PivotOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string PivotOperation::getCategory() const
{
    return s_category;
}


std::string PivotOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


OperationResult PivotOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|PivotOperation");

    // Adjust meshesOnly based on whether xforms should be included
    bool meshesOnly = true;
    if (m_applyTo == ApplyTo::eMeshesAndXforms)
    {
        meshesOnly = false;
    }

    // Resolve and then compute pivot on any prims
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(getUsdStage(), m_primPaths, meshesOnly);
    pivot(prims);

    // Compute extents on any meshes
    _computeExtents(prims);

    return { true };
}


} // namespace omni::scene::optimizer
