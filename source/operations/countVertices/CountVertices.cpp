// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "CountVertices.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/TbbCompat.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primRange.h>

#include <tbb/combinable.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin
SO_PLUGIN_INIT(omni::scene::optimizer::CountVerticesOperation);


namespace omni::scene::optimizer
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
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion CountVerticesOperation::getVersion() const
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
    CARB_PROFILE_ZONE(0, "SceneOptimizer|CountVerts|Execute");

    if (m_levelVeryHigh <= m_levelHigh || m_levelExtreme <= m_levelVeryHigh)
    {
        SO_LOG_WARN("Unexpected thresholds specified: %lu < %lu < %lu", m_levelHigh, m_levelVeryHigh, m_levelExtreme);
        return { false };
    }

    tbb::combinable<Counters> counters;

    std::vector<UsdPrim> prims = { getUsdStage()->GetPseudoRoot() };

    tbbcompat::parallelForEach(prims.begin(),
                               prims.end(),
                               [&](const UsdPrim& prim, tbbcompat::Feeder<UsdPrim>& feeder)
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

    SO_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult CountVerticesOperation::executeAnalysisImpl()
{
    // analysis mode is the same as execute
    return executeImpl();
}


} // namespace omni::scene::optimizer
