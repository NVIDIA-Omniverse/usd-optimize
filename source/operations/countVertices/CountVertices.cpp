// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "CountVertices.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primRange.h>

#include <tbb/combinable.h>
#include <tbb/parallel_for_each.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::CountVerticesOperation);


namespace usd_optimize
{


CountVerticesOperation::CountVerticesOperation()
    : Operation("countVertices", "Count Vertices", "Create a report of prims with excessive vertex counts.")
{
    addArgument("high",
                "High",
                kDisplayTypeInt,
                "Consider prims with this many vertices to have a high vertex count",
                m_levelHigh);

    addArgument("veryHigh",
                "Very High",
                kDisplayTypeInt,
                "Consider prims with this many vertices to have a very high vertex count",
                m_levelVeryHigh);

    addArgument("extreme",
                "Extreme",
                kDisplayTypeInt,
                "Consider prims with this many vertices to have an extreme vertex count",
                m_levelExtreme);
}


std::string CountVerticesOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion CountVerticesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string CountVerticesOperation::getCategory() const
{
    constexpr const char* s_category = "COUNTVERTS";
    return s_category;
}


bool CountVerticesOperation::getVisible() const
{
    // Debug operation, hidden from UI
    return false;
}


bool CountVerticesOperation::getSupportsAnalysis() const
{
    return true;
}


// Helper object to work with tbb::combinable
struct Counters
{
    std::map<SdfPath, size_t> data;

    // Define operator+ to use with combine()
    Counters operator+(const Counters& other) const
    {
        Counters result;

        result.data.insert(data.begin(), data.end());
        result.data.insert(other.data.begin(), other.data.end());

        return result;
    }
};


OperationResult CountVerticesOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|CountVerts|Execute");

    if (m_levelVeryHigh <= m_levelHigh || m_levelExtreme <= m_levelVeryHigh)
    {
        USD_OPTIMIZE_LOG_WARN("Unexpected thresholds specified: %lu < %lu < %lu",
                              m_levelHigh,
                              m_levelVeryHigh,
                              m_levelExtreme);
        return { false };
    }

    tbb::combinable<Counters> counters;

    std::vector<UsdPrim> prims = { getUsdStage()->GetPseudoRoot() };

    tbb::parallel_for_each(prims.begin(),
                           prims.end(),
                           [&](const UsdPrim& prim, tbb::feeder<UsdPrim>& feeder)
                           {
                               const auto& children = prim.GetAllChildren();
                               for (const auto& child : children)
                               {
                                   feeder.add(child);
                               }

                               auto& _counters = counters.local();

                               UsdGeomMesh mesh(prim);
                               if (mesh)
                               {
                                   VtIntArray faceVertexIndices;
                                   mesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices);

                                   // Record anything greater than high - we will bucket them appropriately in the
                                   // output.
                                   const uint64_t total = faceVertexIndices.size();
                                   if (total >= m_levelHigh)
                                   {
                                       _counters.data.insert(std::make_pair(prim.GetPrimPath(), total));
                                   }
                               }
                           });

    Counters sum = counters.combine(std::plus<Counters>());

    // Filter results in to separate buckets based on their count.
    JsObject high;
    JsObject veryHigh;
    JsObject extreme;

    for (const auto& [path, count] : sum.data)
    {
        if (count >= m_levelExtreme)
        {
            extreme[path.GetAsString()] = JsValue(count);
        }
        else if (count >= m_levelVeryHigh)
        {
            veryHigh[path.GetAsString()] = JsValue(count);
        }
        else if (count >= m_levelHigh)
        {
            high[path.GetAsString()] = JsValue(count);
        }
    }

    // Since this operation is kind of always in "analysis mode", always prepare and
    // return the payload.
    JsObject lists;
    lists["high"] = high;
    lists["veryHigh"] = veryHigh;
    lists["extreme"] = extreme;

    JsObject resultJson;
    resultJson["analysis"] = lists;

    OperationResult result{ true, nullptr, getCStr(JsWriteToString(resultJson)) };

    USD_OPTIMIZE_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult CountVerticesOperation::executeAnalysisImpl()
{
    // analysis mode is the same as execute
    return executeImpl();
}


} // namespace usd_optimize
