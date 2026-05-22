// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/OmniOperation.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"
#include "omni/scene.optimizer/core/ResolveSdfPaths.h"
#include "omni/scene.optimizer/core/TbbCompat.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"
#include "omni/scene.optimizer/core/Utils.h"

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usd/primRange.h>

// C++
#include <thread>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


OmniOperation::OmniOperation(const std::string& name, const std::string& displayName, const std::string& description)
    : Operation(name, displayName, description)
{
}


OmniOperation::~OmniOperation()
{
}


bool OmniOperation::meshesOnly()
{
    return true;
}

Usd_PrimFlagsPredicate OmniOperation::primFlagsPredicate() const
{
    return UsdPrimDefaultPredicate;
}

ResolveFilter OmniOperation::resolveFilter()
{
    return [](const UsdPrim& prim, UsdPrimRange::iterator&) -> bool { return !_hasAuthoredTimeSamples(prim); };
}

void OmniOperation::preProcessPrims(std::vector<UsdPrim>&)
{
}

OperationResult OmniOperation::executePre()
{
    return { true };
}


void OmniOperation::executePost(const TotalStats& totalStats)
{
    SO_LOG_INFO("Total vertex count: %zu -> %zu", totalStats.before.vertexCount, totalStats.after.vertexCount);
    SO_LOG_INFO("Total face count: %zu -> %zu", totalStats.before.faceCount, totalStats.after.faceCount);
}


// Output Filter for pipeline
//
// Takes care of authoring data back to the stage on the main thread. As tasks from the pipeline may
// occur on arbitrary threads, data is queued until the main thread happens to be the active thread.
// Any final data is written when the destructor flushes the queue, assuming the object is destructed
// on the main thread.
class OutputFilter
{
public:
    OutputFilter(const UsdStageWeakPtr& stage, TotalStats* stats, const std::thread::id& mainThreadId)
        : m_stats(stats)
        , m_outStage(stage)
        , m_mainThreadId(mainThreadId)
    {
    }

    ~OutputFilter()
    {
        // Flush the output queue
        writeQueued();

        // Delete anything marked for removal
        if (!m_deletePrims.empty())
        {
            _deletePrims(m_outStage, m_deletePrims, true /* deactivate */);
        }
    }

    void operator()(ProcessedData* data) const
    {
        if (data)
        {
            // OutputFilter is run in serial so even though this may be on a random
            // thread between other tasks, only one OutputFilter task should be
            // active at any time. Regardless, queue the ProcessedData object for
            // writing.
            m_outQueue.push(data);

            // If we are on the main thread, we can proceed to actually write any queued tasks.
            // Otherwise, the next invocation on the main thread (or in the dtor at the end) will
            // finish writing the queue. This ensures we only write to USD on the main thread.
            if (std::this_thread::get_id() == m_mainThreadId)
            {
                writeQueued();
            }
        }
    }

    void writeQueued() const
    {
        try
        {
            while (!m_outQueue.empty())
            {
                ProcessedData* queuedProcessed = m_outQueue.front();
                m_outQueue.pop();

                // Update stats
                if (m_stats)
                {
                    queuedProcessed->updateStats(*m_stats);
                }

                // Check whether this data should be written or deleted
                if (queuedProcessed->shouldWrite())
                {
                    if (queuedProcessed->shouldDelete())
                    {
                        m_deletePrims.push_back(queuedProcessed->usdPrim());
                    }
                    else
                    {
                        try
                        {
                            queuedProcessed->writeToUsd(queuedProcessed->usdPrim().GetPath().GetAsString(), m_outStage);
                        }
                        catch (const std::exception& e)
                        {
                            std::string errorMsg = std::string(e.what()) + " (Prim: " +
                                                   queuedProcessed->usdPrim().GetPath().GetAsString().c_str() + ")";
                            SO_LOG_ERROR(errorMsg.c_str());
                        }
                    }
                }

                delete queuedProcessed;
            }
        }
        catch (const std::exception& e)
        {
            SO_LOG_ERROR(e.what());
        }
    }

private:
    mutable std::queue<ProcessedData*> m_outQueue;
    mutable std::vector<UsdPrim> m_deletePrims;
    TotalStats* m_stats;
    UsdStageWeakPtr m_outStage;
    std::thread::id m_mainThreadId;
};


OperationResult OmniOperation::executeImpl()
{
    // Carb Profile Marker
    std::string opName = "SceneOptimizer|" + getName();
    CARB_PROFILE_ZONE(0, opName.c_str());

    // Resolve prims
    constexpr bool reverse = false;
    const Usd_PrimFlagsPredicate predicate = primFlagsPredicate();

    std::vector<UsdPrim> primsToProcess = _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(),
                                                                     m_meshPrimPaths,
                                                                     meshesOnly(),
                                                                     reverse,
                                                                     predicate,
                                                                     resolveFilter());

    // Allow derived classes a chance to do any setup they need
    OperationResult preResult = executePre();
    if (!preResult.success)
    {
        return preResult;
    }

    // Preprocess prims
    preProcessPrims(primsToProcess);

    if (primsToProcess.empty())
    {
        return { true };
    }

    size_t maxTokensInFlight =
        getContext()->singleThreaded ?
            1 :
            std::min(primsToProcess.size(), static_cast<size_t>((carb::thread::hardware_concurrency())));

    TotalStats totalStats;
    tbb::task_group_context taskGroupContext;

    // Main geometry processing task
    std::atomic<size_t> primIndex = 0;

    auto processFilter = tbb::make_filter<void, ProcessedData*>(
        tbbcompat::parallelFilterMode,
        [&](tbb::flow_control& flowControl) -> ProcessedData*
        {
            size_t currentIndex = primIndex++;

            // If everything has been processed, stop.
            if (currentIndex >= primsToProcess.size())
            {
                flowControl.stop();
                return nullptr;
            }

            const auto& prim = primsToProcess[currentIndex];

            ProcessedData* result = nullptr;

            if (!prim.IsInstanceProxy())
            {
                result = this->processMesh(prim, taskGroupContext);
            }
            else
            {
                SO_LOG_VERBOSE("Skipped prim %s because it is an instance proxy", prim.GetPath().GetAsString().c_str());
                result = new ProcessedData(prim, false /* write */);
            }

            return result;
        });

    // Write result back to stage
    auto outputFilter =
        tbb::make_filter<ProcessedData*, void>(tbbcompat::serialOutOfOrderFilterMode,
                                               OutputFilter(getUsdStage(), &totalStats, std::this_thread::get_id()));

    // Build and execute the pipeline.
    tbb::parallel_pipeline(maxTokensInFlight, processFilter & outputFilter, taskGroupContext);

    // Clear the filters. This will ensure the OutputFilter object is cleared and flushes its
    // write queue - note this _must_ be done before processing stats.
    processFilter.clear();
    outputFilter.clear();

    // Post process
    executePost(totalStats);

    return { true };
}


} // namespace omni::scene::optimizer
