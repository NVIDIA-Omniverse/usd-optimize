// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "FindOverlappingMeshesOperation.h"

#include "vis/UnorderedIndexSet.h"

// Scene Optimizer
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/Log.h>
#include <omni/scene.optimizer/core/Utils.h>

// Scene Optimizer Core
#include <omni/scene.optimizer/core/UsdIncludes.h>

#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

/// Constants
constexpr const char* s_categoryFindOverlappingMeshes = "FIND_OVERLAPPING_MESHES";

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::FindOverlappingMeshesOperation);

namespace omni::scene::optimizer
{

/// @brief Utility for detecting islands in overlap groups.
struct IslandDetector
{
    // If the nodes are not currently in the same island, they will be after this function is called,
    // and the function returns true.  Otherwise the function returns false.
    bool addPair(size_t node0, size_t node1)
    {
        size_t island0 = getNodeIsland(node0);
        size_t island1 = getNodeIsland(node1);

        if (island0 == invalid)
        {
            if (island1 == invalid)
            {
                // Neither node is in an island yet.  Create an island and put them both in it.
                const size_t island = createIsland();
                m_islands[island].insert(node0);
                m_nodeIsland[node0] = island;
                m_islands[island].insert(node1);
                m_nodeIsland[node1] = island;
            }
            else
            {
                // node0 not in an island but node1 is.  Put node0 in node1's island
                m_islands[island1].insert(node0);
                m_nodeIsland[node0] = island1;
            }
        }
        else
        {
            if (island1 == invalid)
            {
                // node1 not in an island but node0 is.  Put node1 in node0's island
                m_islands[island0].insert(node1);
                m_nodeIsland[node1] = island0;
            }
            else
            {
                // Both nodes are in islands.  If they are not in the same island, the islands need to be merged.
                if (island0 != island1)
                {
                    // Ensure island0 size >= island1 size, so we minimize what we have to move
                    if (m_islands[island0].size() < m_islands[island1].size())
                    {
                        std::swap(island0, island1);
                    }

                    // Move the nodes in island1
                    for (size_t node : m_islands[island1])
                    {
                        m_islands[island0].insert(node);
                        m_nodeIsland[node] = island0;
                    }

                    // Clear island1 and remove from used islands
                    m_islands[island1].clear();
                    m_usedIslands.erase(island1);
                }
                else
                {
                    // The nodes are in the same island, so there's nothing to do.
                    return false;
                }
            }
        }

        return true;
    }

    size_t getIslandCount() const
    {
        return m_usedIslands.size();
    }

    const std::set<size_t>& getIsland(size_t islandIndex) const
    {
        return islandIndex < m_usedIslands.size() ? m_islands[m_usedIslands[islandIndex]] : emptySet;
    }

    size_t getNodeCount() const
    {
        return m_nodeIsland.size();
    }

private:
    size_t getNodeIsland(size_t node)
    {
        auto it = m_nodeIsland.find(node);
        return it == m_nodeIsland.end() ? invalid : it->second;
    }

    size_t createIsland()
    {
        const size_t island = m_usedIslands.insertUnique();

        if (m_usedIslands.capacity() > m_islands.size())
        {
            m_islands.resize(m_usedIslands.capacity());
        }

        return island;
    }

    static constexpr size_t invalid = std::numeric_limits<size_t>().max();
    static inline const std::set<size_t> emptySet;

    std::vector<std::set<size_t>> m_islands;
    std::unordered_map<size_t, size_t> m_nodeIsland;
    UnorderedIndexSet<size_t> m_usedIslands;
};

/// @brief Checks if the intersection of two sorted containers is empty.
/// @tparam SortedContainerA Type of first container.
/// @tparam SortedContainerB Type of second container.
/// @param A First container.
/// @param B Second container.
/// @return True if intersection is empty.
template <typename SortedContainerA, typename SortedContainerB>
inline static bool intersectionIsEmpty(const SortedContainerA& A, const SortedContainerB& B)
{
    auto ia = A.begin();
    auto ib = B.begin();

    if (ia == A.end() || ib == B.end())
    {
        return true;
    }

    auto a = *ia;
    auto b = *ib;

    while (a != b)
    {
        if (a < b)
        {
            if (++ia == A.end())
            {
                return true;
            }
            a = *ia;
        }
        else
        {
            if (++ib == B.end())
            {
                return true;
            }
            b = *ib;
        }
    }

    return false;
}

FindOverlappingMeshesOperation::FindOverlappingMeshesOperation()
    : OmniOperation("findOverlappingMeshes",
                    "Find Overlapping Meshes",
                    "This operation finds interfering geometry in a stage.")
    , m_meshOverlapService(FindOverlappingMeshes::get())
{
    // Drop the clashdetector's CUDA buffers at process shutdown, while the
    // CUDA driver is still alive. Without this, the singleton's static
    // destructor runs after the driver has torn down and cudaFree throws
    // (driver-shutting-down error 4) from inside the MeshTools clashdetector
    // destructor — terminating the process. The Python binding drives this
    // via Py_AtExit; non-Python embedders are expected to call
    // SceneOptimizerCore::runShutdownCallbacks() themselves before exit.
    SceneOptimizerCore::getInstance().registerShutdownCallback([]() { FindOverlappingMeshes::get().shutdown(); });

    addArgument("paths", "Meshes to test", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument(
        "reportIslands",
        "Report islands",
        kDisplayTypeBool,
        "If set, overlapping meshes will be grouped into islands.  Otherwise they will be grouped into overlap pairs.",
        m_reportIslands);

    // addArgument(
    //     "reportFullGroups",
    //     "Report full groups",
    //     kDisplayTypeBool,
    //     "If set, all meshes in overlap groups will be reported, even if they are not included in \"Meshes to
    //     test.\"", m_reportFullGroups);

    addArgument(
        "fullStageReport",
        "Always list overlaps",
        kDisplayTypeBool,
        "If set, individual overlaps will be reported even when 'paths' is empty (processing the full stage).  Otherwise overlaps will only be reported when 'paths' is not empty.",
        m_fullStageReport);

    // addArgument(
    //     "visualization",
    //     "Visualization",
    //     kDisplayTypeBool,
    //     "If set, overlapping meshes are visualized in the main viewport.",
    //     m_visualization);

    addArgument("useGpu",
                "Use GPU",
                kDisplayTypeBool,
                "If set, mesh overlap detection is performed on GPU.",
                m_detectorParameters.useGpu);

    addArgument("useParallelCpu",
                "Use Parallel CPU",
                kDisplayTypeBool,
                "If set and useGpu is False, the CPU implementation uses multiple threads (TBB) for "
                "point-clash and face-intersection passes.  Ignored when useGpu is True.",
                m_detectorParameters.useParallelCpuClashDetection);

    // addArgument("gpuId",
    //             "GPU ID",
    //             kDisplayTypeInt,
    //             "The ID of the GPU to use for overlapping mesh detection.",
    //             m_detectorParameters.gpuId);

    // addArgument(
    //     "computeIntersections",
    //     "Compute intersections",
    //     kDisplayTypeBool,
    //     "If set, intersections between the meshes will be computed.",
    //     m_detectorParameters.computeIntersections);

    // addArgument(
    //     "computeIntersectionsDouble",
    //     "Compute intersections dbl",
    //     kDisplayTypeBool,
    //     "If set, intersections between the meshes will be computed using double precision.",
    //     m_detectorParameters.computeIntersectionsDoublePrecision);

    // addArgument(
    //     "computeClashes",
    //     "Compute clashes",
    //     kDisplayTypeBool,
    //     "If set, point clashes between the meshes will be computed.",
    //     m_detectorParameters.computeClashes);

    // addArgument(
    //     "computeClashesDouble",
    //     "Compute clashes dbl",
    //     kDisplayTypeBool,
    //     "If set, point clashes between the meshes will be computed using double precision.",
    //     m_detectorParameters.computeClashesDoublePrecision);

    // addArgument("minCutDistance",
    //     "Minimum cut distance",
    //     kDisplayTypeFloat,
    //     "The minimum relative distance of an edge cut to the edge endpoints.",
    //     m_detectorParameters.minCutDistance);

    // addArgument("samplingPointDistance",
    //     "Sampling point distance",
    //     kDisplayTypeFloat,
    //     "The distance between sampling points on the faces of the meshes.",
    //     m_detectorParameters.samplingPointDistance)
    //     .setMin(0.0f);

    // addArgument("searchDistance",
    //     "Search distance",
    //     kDisplayTypeFloat,
    //     "The maximum distance to search for clashes.",
    //     m_detectorParameters.searchDistance)
    //     .setMin(0.0f);

    // addArgument("minimumClashDepth",
    //     "Minumum clash depth",
    //     kDisplayTypeFloat,
    //     "The minimal depth to consider a clash valid.  Negative values mean that clashes with a small gap are also
    //     reported.", m_detectorParameters.minimumClashDepth) .setMin(0.0f);

    // addArgument(
    //     "maximumNumberOfVerticesPerTile",
    //     "Maximum vertices per tile",
    //     kDisplayTypeInt,
    //     "Enables tiling of large scenes that do not fit into GPU memory.  If this value is zero, no tiling is
    //     performed.", m_detectorParameters.maximumNumberOfVerticesPerTile) .setMin(0);
}

FindOverlappingMeshesOperation::~FindOverlappingMeshesOperation()
{
}

std::string FindOverlappingMeshesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}

SOPluginVersion FindOverlappingMeshesOperation::getVersion() const
{
    return { 1, 0, 0 };
}

std::string FindOverlappingMeshesOperation::getCategory() const
{
    return s_categoryFindOverlappingMeshes;
}

std::string FindOverlappingMeshesOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}

bool FindOverlappingMeshesOperation::getSupportsAnalysis() const
{
    return true;
}

ProcessedData* FindOverlappingMeshesOperation::processMesh(const UsdPrim&, tbb::task_group_context&)
{
    return nullptr;
}

void FindOverlappingMeshesOperation::executePost(const TotalStats& totalStats)
{
    generate();

    SO_LOG_INFO("%zu overlap groups found", m_overlapGroups.size());

    if (!m_fullStageReport && m_meshPrimPaths.empty() && m_overlapGroups.size() > 0)
    {
        SO_LOG_INFO("For details, specify paths or check fullStageReport.");
        return;
    }

    for (auto& group : m_overlapGroups)
    {
        std::stringstream stream;
        auto paths = group.GetArrayOf<std::string>();
        for (auto& path : paths)
        {
            stream << "\n    " << path;
        }
        SO_LOG_INFO("%s", stream.str().c_str());
    }
}

OperationResult FindOverlappingMeshesOperation::executeAnalysisImpl()
{
    generate();

    JsObject analysisResult;

    const bool suppressOverlaps = !m_fullStageReport && m_meshPrimPaths.empty() && !m_overlappingPrimPaths.empty();

    analysisResult["suppressedOverlaps"] =
        JsValue(suppressOverlaps ? static_cast<uint64_t>(m_overlappingPrimPaths.size()) : uint64_t(0));

    if (!suppressOverlaps)
    {
        JsArray overlappingMeshes;
        overlappingMeshes.reserve(m_overlappingPrimPaths.size());
        for (auto& path : m_overlappingPrimPaths)
        {
            overlappingMeshes.emplace_back(JsValue(path.GetString()));
        }
        analysisResult["overlappingMeshes"] = overlappingMeshes;
    }

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    return { true, nullptr, getCStr(JsWriteToString(resultJson)) };
}

void FindOverlappingMeshesOperation::generate()
{
    ScopedTimer timer("FindOverlappingMeshesOperation::generate", "", LogLevel::eInfo);

    m_overlappingPrimPaths.clear();
    m_overlapGroups.clear();

    // Resolve meshes
    std::vector<UsdPrim> primsToProcess = _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(),
                                                                     m_meshPrimPaths,
                                                                     meshesOnly(),
                                                                     false,
                                                                     primFlagsPredicate(),
                                                                     resolveFilter());

    if (m_detectorParameters.useGpu && !isCudaAvailable())
    {
        SO_LOG_WARN("GPU requested but CUDA is not available. Falling back to CPU.");
        m_detectorParameters.useGpu = false;
    }

    size_t overlapCount = 0;
    {
        ScopedTimer timer("FindOverlappingMeshes::processStage", "", LogLevel::eInfo);
        overlapCount = m_meshOverlapService.processStage(m_detectorParameters, getUsdStage(), primsToProcess);
    }

    // // If there is a user-supplied mesh list, map those to indices in the descriptor list
    // std::set<size_t> requestedMeshes;
    // if (m_reportFullGroups && !m_meshPrimPaths.empty())
    // {
    //     // Use the same filters to find the prim subset
    //     std::vector<UsdPrim> primSubset = _resolveExpressionsToPrims(getUsdStage()->GetPseudoRoot(),
    //                                                                  m_meshPrimPaths,
    //                                                                  meshesOnly(),
    //                                                                  false,
    //                                                                  primFlagsPredicate(),
    //                                                                  resolveFilter());

    //     SdfPathSet pathSet;
    //     for (UsdPrim& prim: primSubset)
    //     {
    //         pathSet.insert(prim.GetPath());
    //     }

    //     for (size_t meshIndex = 0; meshIndex < m_meshOverlapService.meshCount(); ++meshIndex)
    //     {
    //         const SdfPath path = m_meshOverlapService.meshPath(meshIndex);
    //         if (pathSet.count(path))
    //         {
    //             requestedMeshes.insert(meshIndex);
    //         }
    //     }
    // }


    if (m_reportIslands)
    {
        // Find islands
        IslandDetector islandDetector;
        m_meshOverlapService.overlapPairs(
            [&](const std::pair<int, int>& pair)
            {
                islandDetector.addPair(size_t(pair.first), size_t(pair.second));
                return false; // Don't break early, we need to process all pairs to find the islands
            });

        m_overlappingPrimPaths.reserve(islandDetector.getNodeCount());
        m_overlapGroups.reserve(islandDetector.getIslandCount());
        for (size_t island = 0; island < islandDetector.getIslandCount(); ++island)
        {
            auto& meshes = islandDetector.getIsland(island);
            // if (!requestedMeshes.empty() && intersectionIsEmpty(requestedMeshes, meshes))
            // {
            //     continue;
            // }
            JsArray paths;
            paths.reserve(meshes.size());
            for (size_t mesh : meshes)
            {
                const SdfPath path = m_meshOverlapService.meshPath(mesh);
                paths.emplace_back(JsValue(path.GetString()));
                m_overlappingPrimPaths.push_back(path); // Islands are disjoint, so we can simply add this path
            }
            m_overlapGroups.emplace_back(paths);
        }
    }
    else
    {
        SdfPathSet overlappingPaths; // To avoid duplicates when reporting individual pairs
        m_overlappingPrimPaths.reserve(m_meshPrimPaths.size()); // Worst case, all prims are overlapping
        m_overlapGroups.reserve(overlapCount);
        m_meshOverlapService.overlapPairs(
            [&](const std::pair<int, int>& pair)
            {
                // if (!requestedMeshes.empty() &&
                //     requestedMeshes.count(pair.first) == 0 && requestedMeshes.count(pair.second) == 0)
                // {
                //     return false;
                // }
                JsArray paths;
                paths.reserve(2);
                const SdfPath path0 = m_meshOverlapService.meshPath((size_t)pair.first);
                const SdfPath path1 = m_meshOverlapService.meshPath((size_t)pair.second);
                paths.emplace_back(JsValue(path0.GetString()));
                paths.emplace_back(JsValue(path1.GetString()));
                m_overlapGroups.push_back(paths);
                if (overlappingPaths.insert(path0).second)
                {
                    m_overlappingPrimPaths.push_back(path0);
                }
                if (overlappingPaths.insert(path1).second)
                {
                    m_overlappingPrimPaths.push_back(path1);
                }
                return false; // Don't break early, we need to process all pairs
            });
    }
}

} // namespace omni::scene::optimizer
