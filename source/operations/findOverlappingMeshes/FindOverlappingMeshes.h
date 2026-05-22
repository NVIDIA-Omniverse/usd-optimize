// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "vis/AsyncVector.h"

// Mesh Tools
#include <MeshTools/ClashDetector.h>

// Scene Optimizer Core
#include <omni/scene.optimizer/core/UsdIncludes.h>

// C++
#include <memory>

namespace omni::scene::optimizer
{

template <typename A, typename B>
struct PairHash
{
    size_t operator()(const std::pair<A, B>& p) const
    {
        const size_t h = std::hash<A>{}(p.first);
        if constexpr (sizeof(size_t) >= 8)
        {
            return h ^ (std::hash<B>{}(p.second) + 0x517cc1b727220a95 + (h << 6) + (h >> 2));
        }
        else
        {
            return h ^ (std::hash<B>{}(p.second) + 0x9e3779b9 + (h << 6) + (h >> 2));
        }
    }
};

class FindOverlappingMeshes
{
public:
    static FindOverlappingMeshes& get()
    {
        static FindOverlappingMeshes singleton;
        return singleton;
    }

    FindOverlappingMeshes(const FindOverlappingMeshes&) = delete;
    FindOverlappingMeshes& operator=(const FindOverlappingMeshes&) = delete;
    FindOverlappingMeshes(FindOverlappingMeshes&&) = delete;
    FindOverlappingMeshes& operator=(FindOverlappingMeshes&&) = delete;

    void clear();

    /// Eagerly drop the underlying ClashDetector and any CUDA buffers it owns.
    /// Intended to be invoked via SceneOptimizerCore's shutdown-callback
    /// queue (which the Python binding ties to \p Py_AtExit) so the CUDA
    /// driver is still alive when \p cudaFree runs. Avoids the
    /// static-destructor-order crash where the ClashDetector destructor
    /// runs after the driver has shut down.
    void shutdown();

    size_t processStage(const MeshTools::ClashDetectorParameters& parameters,
                        PXR_NS::UsdStageWeakPtr stage,
                        const std::vector<PXR_NS::UsdPrim>& primsToProcess);

    size_t meshCount() const
    {
        std::lock_guard<std::mutex> lock(m_processMutex);
        return m_meshPaths.size();
    }

    PXR_NS::SdfPath meshPath(size_t meshId) const
    {
        std::lock_guard<std::mutex> lock(m_processMutex);
        return meshId < m_meshPaths.size() ? m_meshPaths[meshId] : PXR_NS::SdfPath();
    }

    std::vector<MeshTools::ClashMeshDescriptor> meshDescriptors() const
    {
        std::lock_guard<std::mutex> lock(m_processMutex);
        return m_meshDescriptors;
    }

    template <typename Fn>
    void overlapPairs(const Fn& fn)
    {
        std::vector<std::pair<int, int>> pairsCopy;
        {
            std::lock_guard<std::mutex> lock(m_processMutex);
            pairsCopy.reserve(m_overlapPairs.size());
            for (const auto& pair : m_overlapPairs)
            {
                pairsCopy.push_back(pair);
            }
        }

        for (const auto& pair : pairsCopy)
        {
            if (fn(pair))
            {
                break;
            }
        }
    }

private:
    class PathHash
    {
    public:
        size_t operator()(const PXR_NS::SdfPath& key) const
        {
            return key.GetHash();
        }
    };

    /// Reset everything except the CUDA-owning ClashDetector (host-side
    /// vectors, descriptors with their heap-allocated arrays, lookup
    /// maps). Shared by clear() and the destructor — the destructor must
    /// not touch the ClashDetector because the CUDA driver may already
    /// be torn down by the time it runs.
    void clearBookkeeping();

    FindOverlappingMeshes() = default;
    ~FindOverlappingMeshes();

    MeshTools::ClashDetectorParameters m_clashDetectorParameters;
    // unique_ptr so shutdown() can drop the ClashDetector (and its CUDA
    // buffers) at process shutdown, before the CUDA driver tears down.
    std::unique_ptr<MeshTools::ClashDetector> m_clashDetector;

    float m_unitsPerMeter = 1.0f;
    PXR_NS::SdfPathVector m_meshPaths;
    std::unordered_map<PXR_NS::SdfPath, int, PathHash> m_meshPathToIdMap;
    std::vector<MeshTools::ClashMeshDescriptor> m_meshDescriptors;
    AsyncVector<std::pair<int, int>> m_overlapPairs;

    mutable std::mutex m_processMutex;
};

} // namespace omni::scene::optimizer
