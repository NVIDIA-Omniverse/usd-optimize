// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/Cluster.h"

// TBB
#include <tbb/parallel_for.h>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


BVHNode::BVHNode(const MeshNode* mesh, const PXR_NS::GfBBox3d& bounds)
    : mesh(mesh)
    , bounds(bounds)
    , range(bounds.ComputeAlignedRange())
{
    if (mesh)
    {
        nvertex = mesh->nvertex;
    }
}


BVH::BVH(const std::vector<MeshNodePtr>& meshes)
{
    // We want to be able to pass a vector internally and sort parts of it, without affecting
    // the original, so we make a copy. At this point we also use the raw pointers rather than
    // copying shared pointers more.
    std::vector<const MeshNode*> _meshes(meshes.size());

    // Record the original index. As we will sort chunks of this copy, we'll need to know how to
    // get back to the original. We do that on the original MeshNode, then copy, so we can maintain
    // const pointers elsewhere.
    for (size_t index = 0; index < _meshes.size(); ++index)
    {
        meshes[index]->originalIndex = index;
        _meshes[index] = meshes[index].get();
    }

    // Do the actual construction of the tree
    m_root = buildBVH(_meshes, 0, _meshes.size() - 1);
}


void BVH::findNeighbors(const MeshNode* target, double maxDistance, std::vector<const MeshNode*>& neighbors) const
{
    // Get the original target bounding box range
    GfRange3d range = target->bound.ComputeAlignedRange();

    // Expand it by the specified threshold in each direction
    // Anything leaf that intersects with this will be considered a neighbor
    range.SetMin(range.GetMin() - GfVec3d(maxDistance, maxDistance, maxDistance));
    range.SetMax(range.GetMax() + GfVec3d(maxDistance, maxDistance, maxDistance));

    // Now we have the target range, begin the actual query
    findNeighborsRecursive(m_root.get(), target, range, neighbors);
}


static GfBBox3d calculateBounds(const std::vector<const MeshNode*>& meshes, size_t start, size_t end)
{
    // Calculate the cumulative bounds of all child meshes
    GfBBox3d result = meshes[start]->bound;
    for (size_t i = start + 1; i <= end; ++i)
    {
        result = GfBBox3d::Combine(result, meshes[i]->bound);
    }

    return result;
}


static int chooseSplitAxis(const GfBBox3d& bounds)
{

    const auto& matrix = bounds.GetMatrix();
    GfVec3d min = matrix.Transform(bounds.GetRange().GetCorner(0));
    GfVec3d max = matrix.Transform(bounds.GetRange().GetCorner(7));

    // Calculate dimensions
    double width = max[0] - min[0];
    double height = max[1] - min[1];
    double depth = max[2] - min[2];

    // Choose the axis with the maximum dimension
    if (width >= height && width >= depth)
    {
        return 0; // X
    }
    else if (height >= width && height >= depth)
    {
        return 1; // Y
    }
    else
    {
        return 2; // Z
    }
}


std::unique_ptr<BVHNode> BVH::buildBVH(std::vector<const MeshNode*>& meshes, size_t start, size_t end)
{

    // If start is end then create a leaf node
    if (start == end)
    {
        return std::make_unique<BVHNode>(meshes[start], meshes[start]->bound);
    }

    // Calculate the overall bounds for the current node, which encompasses all of its children
    GfBBox3d bounds = calculateBounds(meshes, start, end);

    // Choose the axis along which to split the bounding boxes then sort this segment of meshes on it
    int splitAxis = chooseSplitAxis(bounds);

    std::sort(meshes.begin() + start,
              meshes.begin() + end + 1,
              [&](const MeshNode* a, const MeshNode* b) { return a->centroid[splitAxis] < b->centroid[splitAxis]; });

    // Calculate the split index
    size_t splitIndex = start + (end - start) / 2;

    // Create the new branch node
    auto node = std::make_unique<BVHNode>(nullptr, bounds);

    // Recursively build the left and right child nodes and assign to the new branch
    node->left = buildBVH(meshes, start, splitIndex);
    node->right = buildBVH(meshes, splitIndex + 1, end);

    // Update total vertex count
    node->nvertex = node->left->nvertex + node->right->nvertex;

    return node;
}


void BVH::findNeighborsRecursive(const BVHNode* node,
                                 const MeshNode* target,
                                 const GfRange3d& targetRange,
                                 std::vector<const MeshNode*>& neighbors) const
{

    // Test if this node intersects the target range.
    // If not, we can ignore this branch.
    GfRange3d intersection = GfRange3d::GetIntersection(node->range, targetRange);
    if (intersection.IsEmpty())
    {
        return;
    }

    // If this is a leaf node then it intersects with the target range, and is therefore
    // a neighbor.
    if (node->isLeaf())
    {
        // Don't add itself as a neighbor
        if (node->mesh != target)
        {
            neighbors.emplace_back(node->mesh);
        }
    }
    else
    {
        // Carry on and check both branches
        findNeighborsRecursive(node->left.get(), target, targetRange, neighbors);
        findNeighborsRecursive(node->right.get(), target, targetRange, neighbors);
    }
}


BVHNode* BVH::getRoot() const
{
    return m_root.get();
}


/// Cluster meshes based on a maximum vertex count.
///
/// This function uses the BVH which tracks vertex count. It's a very simple iteration through
/// the tree until a branch is found that has fewer vertices than the specified maximum. As
/// the BVH is already created based on bounds, we can simply find a branch with the appropriate
/// number of vertices and cluster any leaf nodes underneath it, knowing that they will be spatially
/// similar.
///
/// This is a recursive function. At the point the \p node vertex count is less than or equal
/// to \p maxSize, \p stamp will be set to true and \p clusterId will be incremented. All leaf
/// nodes found underneath \p node will then be "stamped" with the current cluster id.
///
/// \param node The current node to process
/// \param maxSize The maximum number of vertices to cluster
/// \param clusterId Unique cluster ID
/// \param clusters The output clusters, one per mesh
/// \param stamp Whether we are within a cluster
static void clusterByVertexCount(BVHNode* node, size_t maxSize, int& clusterId, std::vector<int>& clusters, bool stamp)
{
    // If stamp is false (have not yet found a small enough section) then check the vertex count.
    if (!stamp && node->nvertex <= maxSize)
    {
        // stamp was off, so this means we have just encountered a branch of the BVH that
        // is within the target vertex count. Bump the cluster id as this is now a cluster
        // and enable stamp for this branch.
        ++clusterId;
        stamp = true;
    }

    if (node->isLeaf())
    {
        // If stamp is on we want to apply this cluster ID to every leaf mesh we find.
        if (stamp)
        {
            clusters[node->mesh->originalIndex] = clusterId;
        }
    }
    else
    {
        // Not a leaf so recurse both ways
        clusterByVertexCount(node->left.get(), maxSize, clusterId, clusters, stamp);
        clusterByVertexCount(node->right.get(), maxSize, clusterId, clusters, stamp);
    }
}


void spatiallyClusterMeshes(ClusterMode mode,
                            const std::vector<MeshNodePtr>& nodes,
                            double epsilon,
                            double maxSize,
                            std::vector<int>& clusters)
{
    // Build the BVH
    BVH bvh(nodes);

    // Next cluster counter
    int clusterId = INVALID_CLUSTER;

    if (mode == ClusterMode::eVertexCount)
    {
        clusterByVertexCount(bvh.getRoot(), (size_t)maxSize, clusterId, clusters, false);
        return;
    }

    // perform a first pass to find all the neighbors for each mesh.
    // note: This is faster to do ahead of time since we need to do this only once for each mesh. It also avoids going
    //       into a death spiral with large spatial thresholds where each mesh is stacking recursions into the BVH to
    //       find neighbors.
    // WARNING: for some unknown reason this for loop CANNOT be parallelized on Windows, it incurs massive performance
    //          overhead. We could investigate this further, but for now I don't think the performance cost is worth it.
    std::vector<std::vector<const MeshNode*>> allNeighbors(nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        const MeshNodePtr& mesh = nodes[i];
        std::vector<const MeshNode*>& neighbors = allNeighbors.at(i);
        bvh.findNeighbors(mesh.get(), epsilon, neighbors);
    }

    // Performance optimization: instead of a standard queue we use a vector of vectors that
    // tracks the descending index we're currently processing in each section. This preserves the
    // original clustering behaviour while avoiding data movement — we just walk a pointer over
    // the pre-computed nearest neighbors.
    std::vector<std::pair<std::vector<const MeshNode*>*, int>> neighborsQueue;
    neighborsQueue.reserve(nodes.size());

    // iterate through each mesh and cluster them - not worth multi-threading this code since we'd need a mutex around
    // assigning clusters which would be slower than just doing it in a single thread.
    for (size_t i = 0; i < nodes.size(); ++i)
    {
        // If this mesh was already assigned a cluster, skip it.
        if (clusters[i] != INVALID_CLUSTER)
        {
            continue;
        }

        std::vector<const MeshNode*>& neighbors = allNeighbors[i];
        if (neighbors.empty())
        {
            continue;
        }

        const MeshNodePtr& mesh = nodes[i];

        // initialise the queue with the neighbors of this mesh
        neighborsQueue.clear();
        int queueIndex = 0;
        neighborsQueue.emplace_back(&neighbors, static_cast<int>(neighbors.size()) - 1);

        // We might be able to cluster this mesh, but need to find out if any of the neighbors are
        // valid to include.
        int _clusterId = INVALID_CLUSTER;

        // Track the bounds for this cluster
        GfBBox3d currentBound = mesh->bound;

        // Process the neighbors. For each neighbor, check if it has already been clustered. If not,
        // check whether merging it would exceed the max size. If not, include it in this cluster and
        // then carry on checking its neighbors until we hit the max size.
        while (queueIndex >= 0)
        {
            auto& subQueue = neighborsQueue[static_cast<size_t>(queueIndex)];
            // sub queue complete? move to the next
            if (subQueue.second < 0)
            {
                neighborsQueue.pop_back();
                --queueIndex;
                continue;
            }

            while (subQueue.second >= 0)
            {
                const MeshNode* neighbor = (*subQueue.first)[static_cast<size_t>(subQueue.second)];
                --subQueue.second;

                // Skip if this neighbor is already part of a cluster.
                size_t originalIndex = neighbor->originalIndex;
                if (clusters[originalIndex] != INVALID_CLUSTER)
                {
                    continue;
                }

                // Combine the current cluster bound with the new bound, so we can check whether
                // it would exceed the max size
                GfBBox3d newBound = GfBBox3d::Combine(currentBound, neighbor->bound);
                GfRange3d range = newBound.ComputeAlignedRange();
                GfVec3d size = range.GetSize();

                // Currently checking the individual dimensions. This is to get a grid/box like
                // bound, rather than using Length(Sq).
                if (size[0] > maxSize || size[1] > maxSize || size[2] > maxSize)
                {
                    continue;
                }

                // Cool, we found something that is valid to cluster. Now we can make sure we have a
                // new cluster id and assign it to the original mesh, along with this one.
                if (_clusterId == INVALID_CLUSTER)
                {
                    _clusterId = ++clusterId;
                    clusters[i] = _clusterId;
                }

                // Update current total bound and assign clusterId to neighbor
                currentBound = newBound;
                clusters[originalIndex] = clusterId;

                std::vector<const MeshNode*>& nextNeighbors = allNeighbors[originalIndex];
                if (!nextNeighbors.empty())
                {
                    neighborsQueue.push_back(std::make_pair(&nextNeighbors, static_cast<int>(nextNeighbors.size()) - 1));
                    queueIndex = static_cast<int>(neighborsQueue.size() - 1);
                    break;
                }
            }
        }
    }
}


} // namespace omni::scene::optimizer
