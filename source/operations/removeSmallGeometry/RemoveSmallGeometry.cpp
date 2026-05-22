// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "RemoveSmallGeometry.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/JsonUtils.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/usd/usd/primRange.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::RemoveSmallGeometryOperation);


namespace omni::scene::optimizer
{


// Constants
constexpr const char* s_category = "REMOVE_SMALL_GEOMETRY";


RemoveSmallGeometryOperation::RemoveSmallGeometryOperation()
    : Operation("removeSmallGeometry",
                "Remove Small Geometry",
                "Identifies and removes small and/or degenerate geometry from a USD stage.")
{
    addArgument("paths", "Paths", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_paths)
        .setPlaceholder("Add paths or entire scene will be processed");

    Argument& removeArg = addArgument("removeMethod",
                                      "Remove Method",
                                      kDisplayTypeEnum,
                                      "Method that will be used to remove small geometry.",
                                      m_removeMethod);
    removeArg.setEnumValues<RemoveMethod>({
        { RemoveMethod::eDelete, "Delete" },
        { RemoveMethod::eDeactivate, "Deactivate" },
        { RemoveMethod::eHide, "Hide" },
    });

    Argument& detectArg = addArgument(
        "detectionMethod",
        "Detection Method",
        kDisplayTypeEnum,
        "Method that will be used for detecting small geometry.\n"
        " - World Space: Small geometry is determine by checking the maximum size side of extent bounds against an absolute world space value. \n"
        " - Percentage: Small geometry is determine by checking the maximum size side of the extent bounds against a percentage threshold of the scene's median extent size.",
        m_detectionMethod);
    detectArg.setEnumValues<DetectionMethod>({
        { DetectionMethod::eWorldSpace, "World Space" },
        { DetectionMethod::ePercentage, "Percentage" },
    });

    addArgument("threshold",
                "Threshold",
                kDisplayTypeFloat,
                "Threshold that represents the size at which extents are considered small (how this is compared depends on "
                "the detection method). Note: regardless of the detection method, a threshold of 0.0 will mean only "
                "degenerate geometry is removed.",
                m_threshold);
}


RemoveSmallGeometryOperation::~RemoveSmallGeometryOperation() = default;


std::string RemoveSmallGeometryOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion RemoveSmallGeometryOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string RemoveSmallGeometryOperation::getCategory() const
{
    return s_category;
}


std::string RemoveSmallGeometryOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


bool RemoveSmallGeometryOperation::getSupportsAnalysis() const
{
    return true;
}


OperationResult RemoveSmallGeometryOperation::executeImpl()
{
    // find small geometry
    std::vector<UsdPrim> smallGeometry = findSmallGeometry();

    {
        SdfChangeBlock changeBlock;
        _removePrims(m_removeMethod, getUsdStage(), smallGeometry);
    }

    return { true };
}


OperationResult RemoveSmallGeometryOperation::executeAnalysisImpl()
{
    // find small geometry
    std::vector<UsdPrim> smallGeometry = findSmallGeometry();

    // write the result as json
    JsObject analysisResult;
    analysisResult["smallGeometry"] = _toJson(smallGeometry);
    // if there is any small geometry - write this operation as the suggested fix
    if (!smallGeometry.empty())
    {
        JsObject opArgs;
        opArgs["removeMethod"] = _toJson(static_cast<int>(m_removeMethod));
        opArgs["detectionMethod"] = _toJson(static_cast<int>(m_detectionMethod));
        opArgs["threshold"] = _toJson(m_threshold);
        JsObject opConfig;
        opConfig["name"] = _toJson("removeSmallGeometry");
        opConfig["args"] = opArgs;
        JsArray suggestedOperations;
        suggestedOperations.push_back(opConfig);
        analysisResult["suggestedOperations"] = suggestedOperations;
    }

    // write under the analysis key
    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true, nullptr, getCStr(JsWriteToString(resultJson)) };
    return result;
}


std::vector<UsdPrim> RemoveSmallGeometryOperation::findSmallGeometry()
{
    UsdGeomXformCache xformCache;

    // map from prim paths to their extent sizes - we store size rather than volume since volume is cubic so its
    // unintuitive to work with
    std::map<SdfPath, double> extentsSizes;

    // a full iteration of the scene is needed to compute the median size of geometry in the scene
    static const Usd_PrimFlagsPredicate predicate = UsdPrimIsActive && UsdPrimIsLoaded;
    auto primRange = UsdPrimRange(getUsdStage()->GetPseudoRoot(), predicate);
    for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
    {
        const UsdPrim& prim = (*iter);

        // is this point based geometry? - if not just keep iterating
        UsdGeomPointBased pointBased(prim);
        if (!pointBased)
        {
            continue;
        }

        // don't look any further under geometry prims
        iter.PruneChildren();

        // get the extents, either from the attribute or compute them
        VtVec3fArray extentArray;
        UsdAttribute extentAttr = pointBased.GetExtentAttr();
        if (extentAttr.IsValid())
        {
            extentAttr.Get(&extentArray);
        }
        // do we have a valid extent? - otherwise compute it
        if (extentArray.size() != 2)
        {
            UsdAttribute pointsAttr = pointBased.GetPointsAttr();
            if (pointsAttr.IsValid())
            {
                VtVec3fArray points;
                pointsAttr.Get(&points);
                UsdGeomPointBased::ComputeExtent(points, &extentArray);
            }
        }

        // if for some reason we still don't have a valid extent, skip this prim
        if (extentArray.size() != 2)
        {
            SO_LOG_WARN("Unable to resolve extents either from attribute or points for %s",
                        prim.GetPath().GetAsString().c_str());
            continue;
        }

        // get the local to world transform so we can apply scale to the extents
        const GfMatrix4d localToWorldTransform = xformCache.GetLocalToWorldTransform(prim);
        GfMatrix4d r; // unused
        GfVec3d scale;
        GfMatrix4d u; // unused
        GfVec3d t; // unused
        GfMatrix4d p; // unused
        localToWorldTransform.Factor(&r, &scale, &u, &t, &p);

        // now compute the volume of the scaled extents
        const GfVec3d absScale(std::abs(scale[0]), std::abs(scale[1]), std::abs(scale[2]));
        GfRange3d extentRange(GfCompMult(extentArray[0], absScale), GfCompMult(extentArray[1], absScale));

        // compute the size of the extent based on the maximum side length
        const GfVec3d rangeSize = extentRange.GetSize();
        const double size = std::max(std::max(rangeSize[0], rangeSize[1]), rangeSize[2]);

        // store
        extentsSizes[prim.GetPath()] = size;
    }

    // no sizes to process, return early?
    if (extentsSizes.empty())
    {
        return {};
    }

    // compute the median extent size - this is only needed if we're using the percentage detection method
    double medianSize = 0.0;
    if (m_detectionMethod == DetectionMethod::ePercentage)
    {
        std::vector<double> sizes;
        sizes.reserve(extentsSizes.size());
        for (const auto& pair : extentsSizes)
        {
            sizes.push_back(pair.second);
        }
        std::sort(sizes.begin(), sizes.end());
        const size_t midIndex = sizes.size() / 2;
        if (sizes.size() % 2 == 0)
        {
            medianSize = (sizes[midIndex - 1] + sizes[midIndex]) / 2.0;
        }
        else
        {
            medianSize = sizes[midIndex];
        }

        // nothing meaningful to compare against, return early?
        if (medianSize < std::numeric_limits<double>::epsilon())
        {
            return {};
        }
    }

    // callback function for _resolveExpressionsToPrims that finds small geometry
    ResolveFilter resolveFilter = [&](const UsdPrim& prim, UsdPrimRange::iterator& iterator)
    {
        // is this point based geometry? - if not just keep iterating
        UsdGeomPointBased pointBased(prim);
        if (!pointBased)
        {
            return false;
        }

        // don't look any further under geometry prims
        iterator.PruneChildren();

        // look up the extents size for this prim
        auto it = extentsSizes.find(prim.GetPath());
        if (it == extentsSizes.end())
        {
            return false;
        }

        // determine size based on the detection method
        float size = 0.0;
        if (m_detectionMethod == DetectionMethod::eWorldSpace)
        {
            size = static_cast<float>(it->second);
        }
        else
        {
            size = static_cast<float>(it->second / medianSize) * 100.0f;
        }

        // if the size is below the threshold, mark it for removal
        if (size - m_threshold <= std::numeric_limits<float>::epsilon())
        {
            // if we're not in analysis mode, log that the small geometry will be removed
            if (getContext()->analysisMode == false)
            {
                SO_LOG_INFO("Removing small geometry with size %s at %s",
                            std::to_string(size).c_str(),
                            prim.GetPath().GetAsString().c_str());
            }
            return true;
        }

        return false;
    };

    // return the results of _resolveExpressionsToPrims using a custom resolveFilter
    return _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(), m_paths, false, false, predicate, resolveFilter);
}

} // namespace omni::scene::optimizer
