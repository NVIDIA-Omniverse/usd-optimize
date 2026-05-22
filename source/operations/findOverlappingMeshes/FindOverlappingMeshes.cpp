// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "FindOverlappingMeshes.h"

// Scene Optimizer
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/MeshToolsCommon.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/usd/usdGeom/metrics.h>

// TBB
#include <tbb/parallel_for_each.h>

PXR_NAMESPACE_USING_DIRECTIVE

using namespace MeshTools;

#define PARALLELIZE_FIND_OVERLAPPING_MESHES 1

namespace omni::scene::optimizer
{

void FindOverlappingMeshes::clearBookkeeping()
{
    m_unitsPerMeter = 1.0f;
    m_meshPaths.clear();
    for (ClashMeshDescriptor& desc : m_meshDescriptors)
    {
        delete[] desc.vertices;
        delete[] desc.indices;
        delete[] desc.faceSizes;
    }
    m_meshDescriptors.clear();
    m_overlapPairs.clear();
    m_meshPathToIdMap.clear();
}

void FindOverlappingMeshes::clear()
{
    if (m_clashDetector)
    {
        m_clashDetector->freeResults();
    }
    clearBookkeeping();
}

void FindOverlappingMeshes::shutdown()
{
    std::lock_guard<std::mutex> lock(m_processMutex);
    clear();
    // Destruct the ClashDetector now, while the CUDA driver is still alive.
    // After this returns, the singleton's ~FindOverlappingMeshes (running
    // later, during static destruction, possibly after the driver has torn
    // down) has nothing CUDA-touching left to do.
    m_clashDetector.reset();
}

FindOverlappingMeshes::~FindOverlappingMeshes()
{
    // shutdown() should already have run via the core's shutdown-callback
    // queue; if it didn't, drop the bookkeeping but don't touch the
    // (possibly-already-shutdown) CUDA driver.
    std::lock_guard<std::mutex> lock(m_processMutex);
    clearBookkeeping();
}

size_t FindOverlappingMeshes::processStage(const ClashDetectorParameters& parameters,
                                           UsdStageWeakPtr stage,
                                           const std::vector<UsdPrim>& primsToProcess)
{
    size_t overlapCount = 0;
    {
        std::lock_guard<std::mutex> lock(m_processMutex);
        clear();

        auto metersPerUnit = UsdGeomGetStageMetersPerUnit(stage);
        m_unitsPerMeter = metersPerUnit > 0 ? float(1.0 / metersPerUnit) : 1.0f;

        GetStageMeshDescriptors(m_meshDescriptors, m_meshPaths, stage, primsToProcess);
        if (m_meshDescriptors.size() != m_meshPaths.size())
        {
            SO_LOG_ERROR("FindOverlappingMeshes::processStage: mesh descriptors and paths are not the same size.");
            return 0;
        }

        for (int meshId = 0; meshId < (int)m_meshPaths.size(); ++meshId)
        {
            m_meshPathToIdMap[m_meshPaths[meshId]] = meshId;
        }

        m_clashDetectorParameters = parameters;
        m_clashDetectorParameters.samplingPointDistance *= m_unitsPerMeter;
        m_clashDetectorParameters.searchDistance *= m_unitsPerMeter;
        m_clashDetectorParameters.minimumClashDepth *= m_unitsPerMeter;

        if (!m_clashDetector)
        {
            m_clashDetector = std::make_unique<MeshTools::ClashDetector>();
        }
        m_clashDetector->detectClashes(m_clashDetectorParameters, m_meshDescriptors.data(), int(m_meshDescriptors.size()));

        m_overlapPairs.reserve(m_clashDetector->getNumMeshClashes());
        const ClashMeshPair* detectorPairPtr = m_clashDetector->getClashMeshPairs();
#if PARALLELIZE_FIND_OVERLAPPING_MESHES
        tbb::parallel_for(
            tbb::blocked_range<size_t>(0, m_clashDetector->getNumMeshClashes()),
            [&](const tbb::blocked_range<size_t>& r)
            {
                for (size_t clashPairIndex = r.begin(); clashPairIndex != r.end(); ++clashPairIndex)
                {
                    const ClashMeshPair& detectorPair = detectorPairPtr[clashPairIndex];
                    if (detectorPair.mesh0 >= 0 && detectorPair.mesh1 >= 0 && detectorPair.mesh0 != detectorPair.mesh1)
                    {
                        m_overlapPairs.append(std::make_pair(detectorPair.mesh0, detectorPair.mesh1));
                    }
                }
            });
#else
        const ClashMeshPair* detectorPairEnd = detectorPairPtr + m_clashDetector->getNumMeshClashes();
        while (detectorPairPtr < detectorPairEnd)
        {
            const ClashMeshPair& detectorPair = *detectorPairPtr++;
            if (detectorPair.mesh0 >= 0 && detectorPair.mesh1 >= 0 && detectorPair.mesh0 != detectorPair.mesh1)
            {
                m_overlapPairs.append(std::make_pair(detectorPair.mesh0, detectorPair.mesh1));
            }
        }
#endif
        overlapCount = m_overlapPairs.size();
    }

    return overlapCount;
}

} // namespace omni::scene::optimizer
