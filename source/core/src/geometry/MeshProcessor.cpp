// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/MeshProcessor.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"
#include "omni/scene.optimizer/core/Log.h"

// USD
#include <pxr/base/work/utils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdSkel/root.h>

// TBB
#include <tbb/parallel_for.h>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (ShadowAPI)
    (MaterialBindingAPI)
);
// LCOV_EXCL_STOP
// clang-format on


// Less-than operator for GfVec3f for std::map
struct CmpVec3f
{
    bool operator()(const GfVec3f& a, const GfVec3f& b) const
    {
        return std::tie(a[0], a[1], a[2]) < std::tie(b[0], b[1], b[2]);
    }
};


// simple structure to record the material subsets to help with splitting
struct SubsetMap
{
    // Material and metadata
    std::vector<std::pair<SdfPath, UsdMetadataValueMap>> materials;

    // Index from original face index in to materials
    std::vector<int> indices;
};


// Traverse from one or more starting prims to find static mesh prims.
static std::vector<UsdPrim> _findStaticMeshes(const std::vector<UsdPrim>& startPrims)
{
    // The API schemas we support. Some things we don't want to split, this is a
    // list of the schemas we know we can split without causing issues.
    static std::set<TfToken> s_supportedSchemas = { _tokens->ShadowAPI, _tokens->MaterialBindingAPI };

    std::vector<UsdPrim> meshPrims;

    // Track the prims considered so that we do not double handle prims when we have overlapping start prims.
    std::set<UsdPrim> consideredPrims;

    static const Usd_PrimFlagsPredicate predicate = UsdPrimIsActive && UsdPrimIsLoaded;
    for (const auto& startPrim : startPrims)
    {
        auto primRange = UsdPrimRange(startPrim, predicate);
        for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
        {
            const auto& prim = (*iter);

            // If this prim has already been traversed then skip it and do not traverse it's children.
            const auto& result = consideredPrims.insert(prim);
            if (!result.second)
            {
                iter.PruneChildren();
                continue;
            }

            // Skip the pseudo root.
            if (prim.GetPath().IsAbsoluteRootPath())
            {
                continue;
            }

            // Do not traverse past prims that have "over" or "class" specifiers.
            if (prim.GetSpecifier() != SdfSpecifierDef)
            {
                iter.PruneChildren();
                continue;
            }

            // Certain prim types we can prune to avoid even recursing into them.
            if (prim.IsA<UsdGeomCamera>() || prim.IsA<UsdShadeMaterial>() || prim.IsA<UsdSkelRoot>())
            {
                iter.PruneChildren();
                continue;
            }

            // Stash the prim if it is a Mesh, but do not traverse it's children.
            if (prim.IsA<UsdGeomMesh>())
            {
                // If this prim has any applied schemas, validate that they are supported. Otherwise
                // we are best to not try and split this one.
                const TfTokenVector& schemas = prim.GetPrimTypeInfo().GetAppliedAPISchemas();

                if (std::any_of(schemas.cbegin(),
                                schemas.cend(),
                                [&](const TfToken& schema) { return s_supportedSchemas.count(schema) == 0; }))
                {
                    SO_LOG_INFO("Skipping prim with unsupported schema: %s", prim.GetPrimPath().GetAsString().c_str());
                    continue;
                }

                meshPrims.push_back(prim);
                iter.PruneChildren();
                continue;
            }
        }
    }
    return meshPrims;
}


static SdfPath _getSubsetMaterialPathAndMetadata(const UsdGeomSubset& subset, UsdMetadataValueMap& metadata)
{
    const auto& relationship = subset.GetPrim().GetRelationship(UsdShadeTokens->materialBinding);
    if (relationship)
    {
        SdfPathVector targets;
        if (relationship.GetTargets(&targets) && !targets.empty())
        {
            metadata = relationship.GetAllAuthoredMetadata();
            return targets.front();
        }
    }
    return SdfPath{};
}


/// Finds any material subsets on a prim and returns the data about them in the SubsetMap.
static void _findMaterialSubsets(const UsdPrim& prim, size_t faceVertexCounts, SubsetMap& subsetMap)
{
    UsdGeomImageable imageable(prim);
    const std::vector<UsdGeomSubset>& subsets = UsdGeomSubset::GetAllGeomSubsets(imageable);

    if (subsets.empty())
    {
        return;
    }

    // Assumes that "most" faces will be part of a subset. In which case we can use a vector
    // for faster lookups than a map.
    subsetMap.indices.resize(faceVertexCounts, -1);

    for (const auto& subset : subsets)
    {
        UsdMetadataValueMap metadata;
        SdfPath materialPath = _getSubsetMaterialPathAndMetadata(subset, metadata);

        if (!materialPath.IsEmpty())
        {
            VtIntArray subsetFaceIndices;
            subset.GetIndicesAttr().Get(&subsetFaceIndices);

            auto& it = subsetMap.materials.emplace_back();
            it.first = materialPath;
            it.second = std::move(metadata);

            // Index is the last one, that we just appended to the vector
            int index = static_cast<int>(subsetMap.materials.size()) - 1;

            for (const auto subsetFaceIndex : subsetFaceIndices)
            {
                subsetMap.indices[subsetFaceIndex] = index;
            }
        }
    }
}


void _findDisjointMeshes(const MeshData& meshData, DisjointSet& disjointSet, bool splitCollocatedPoints)
{
    // Map of hashed point to face vertex index, if welding is enabled
    std::map<GfVec3f, int, CmpVec3f> pointsToFirstIndex;

    auto _fvc = meshData.baseMesh.getFaceVertexCounts().cdata();
    auto _fvi = meshData.baseMesh.getFaceVertexIndices().cdata();
    auto _points = meshData.baseMesh.getPoints().cdata();

    // Union the verts of each face into the set. After this step the set can now describe the disjoint meshes.
    int vertexOffset = 0;
    for (size_t faceIndex = 0; faceIndex < meshData.baseMesh.getFaceVertexCounts().size(); ++faceIndex)
    {
        // Union each pair of vertices within the face
        int vertexCount = _fvc[faceIndex];

        for (int vertexIndex = 0; vertexIndex < vertexCount - 1; ++vertexIndex)
        {
            disjointSet.unionSet(_fvi[vertexOffset + vertexIndex], _fvi[vertexOffset + vertexIndex + 1]);

            // If split collocated points is disabled, de-duplicate points.
            if (!splitCollocatedPoints)
            {
                // Insert the point
                int pointIndex = _fvi[vertexOffset + vertexIndex];

                const auto& result = pointsToFirstIndex.insert({ _points[pointIndex], pointIndex });

                // If the insert fails, it means this point has been seen before. In this case we can simply union
                // the current index with the original index. This "welds" the points for the purpose of splitting
                // meshes, without touching the original data.
                if (!result.second)
                {
                    disjointSet.unionSet(pointIndex, result.first->second);
                }
            }
        }

        // Bump the vertex offset by the vertex count for this face so we have a rolling index.
        vertexOffset += vertexCount;
    }
}


template <typename T>
static void _collectDisjointMeshes(MeshData& meshData, const T& getMeshId, SubsetMap& subsetMap)
{
    int firstFaceVertexIndex = 0;
    auto _meshFaceCounts = meshData.baseMesh.getFaceVertexCounts().cdata();
    auto _meshFaceIndices = meshData.baseMesh.getFaceVertexIndices().cdata();

    // first pass to reserve memory - this may seem like overkill but this is a huge performance benefit, especially
    // on large scenes
    std::unordered_map<int, size_t> faceVertexCountIndicesSizeMap;
    for (size_t faceIndex = 0; faceIndex < meshData.baseMesh.getFaceVertexCounts().size(); ++faceIndex)
    {
        // Skip faces that have no vertices
        int vertexCount = _meshFaceCounts[faceIndex];
        if (vertexCount == 0)
        {
            continue;
        }

        int meshId = getMeshId(faceIndex, _meshFaceIndices[firstFaceVertexIndex]);
        faceVertexCountIndicesSizeMap[meshId]++;
        firstFaceVertexIndex += vertexCount;
    }

    // collect a mapping from meshId to the face indices
    std::unordered_map<int, std::vector<int>> faceVertexCountIndicesMap;
    std::vector<int> orderedMeshIds;
    orderedMeshIds.reserve(faceVertexCountIndicesSizeMap.size());
    for (const auto& [meshId, size] : faceVertexCountIndicesSizeMap)
    {
        faceVertexCountIndicesMap[meshId].reserve(size);
    }

    firstFaceVertexIndex = 0;
    for (size_t faceIndex = 0; faceIndex < meshData.baseMesh.getFaceVertexCounts().size(); ++faceIndex)
    {
        // Skip faces that have no vertices
        int vertexCount = _meshFaceCounts[faceIndex];
        if (vertexCount == 0)
        {
            continue;
        }

        int meshId = getMeshId(faceIndex, _meshFaceIndices[firstFaceVertexIndex]);
        std::vector<int>& faceVertexCountIndices = faceVertexCountIndicesMap[meshId];
        if (faceVertexCountIndices.empty())
        {
            orderedMeshIds.push_back(meshId);
        }
        faceVertexCountIndices.push_back(static_cast<int>(faceIndex));

        firstFaceVertexIndex += vertexCount;
    }

    // create a new virtual mesh subset for each set of face vertex count indices
    meshData.subsetMeshes.reserve(orderedMeshIds.size());
    for (int meshId : orderedMeshIds)
    {
        VirtualMesh subsetMesh = meshData.baseMesh.newSubset(std::move(faceVertexCountIndicesMap[meshId]));

        // Check if there was a subset material found for any of the faces of this mesh.
        // Note: This does not currently deal with the fact potentially different faces could have
        //       been in different subsets. We'd need to avoid grouping them earlier to deal with
        //       that.
        if (!subsetMap.indices.empty())
        {
            for (const auto& it : faceVertexCountIndicesMap[meshId])
            {
                if (subsetMap.indices[it] != -1)
                {
                    const auto& material = subsetMap.materials[subsetMap.indices[it]];
                    subsetMesh.bindMaterial(material.first, material.second);

                    // Don't need to check any more faces
                    break;
                }
            }
        }

        meshData.subsetMeshes.push_back(subsetMesh);
    }
}


// collects the geomsubsets as virtual meshes for the given prim
static void _collectGeomSubsets(MeshData& meshData)
{
    UsdGeomImageable imageable(meshData.baseMesh.getPrim());
    const std::vector<UsdGeomSubset>& subsets = UsdGeomSubset::GetAllGeomSubsets(imageable);

    if (subsets.empty())
    {
        return;
    }

    std::unordered_set<int> subsetFaces;
    for (const auto& subset : subsets)
    {
        VtIntArray faceIndices;
        subset.GetIndicesAttr().Get(&faceIndices);
        if (!faceIndices.empty())
        {
            // get the face indices for the subset and record the faces that have been used for subsets
            std::vector<int> faceVertexCountIndices(faceIndices.begin(), faceIndices.end());
            subsetFaces.insert(faceVertexCountIndices.begin(), faceVertexCountIndices.end());

            UsdMetadataValueMap materialMetadata;
            SdfPath materialPath = _getSubsetMaterialPathAndMetadata(subset, materialMetadata);

            // create a new subset virtual mesh
            VirtualMesh subsetMesh = meshData.baseMesh.newSubset(std::move(faceVertexCountIndices));
            if (!materialPath.IsEmpty())
            {
                subsetMesh.bindMaterial(materialPath, materialMetadata);
            }
            meshData.subsetMeshes.push_back(subsetMesh);
        }
    }

    // if no virtual meshes were created then we didn't find any valid face indices. Nothing to split here
    if (meshData.subsetMeshes.empty())
    {
        return;
    }

    // If the subsets didn't cover every face, then we'll end up with a mesh to represent the
    // unassigned indices. Copy it's material, if it exists. The relationship itself would get
    // picked up by the VirtualMesh, the problem is it may not have the MaterialBindingAPI applied to it.
    // If we record the material here it will get bound and the schema
    // applied later.
    size_t faceCount = meshData.baseMesh.getFaceVertexCounts().size();
    if (subsetFaces.size() < faceCount)
    {
        // build the remaining indices
        std::vector<int> remainingIndices;
        for (int i = 0; i < static_cast<int>(faceCount); ++i)
        {
            if (subsetFaces.find(i) == subsetFaces.end())
            {
                remainingIndices.push_back(i);
            }
        }

        // create the virtual mesh for the remaining indices
        VirtualMesh subsetMesh = meshData.baseMesh.newSubset(std::move(remainingIndices));
        const SdfPath& materialPath = meshData.baseMesh.getBoundMaterialPath();
        if (!materialPath.IsEmpty())
        {
            subsetMesh.bindMaterial(materialPath);
        }
        // Insert at the front to preserve the expected processing order for subset meshes.
        meshData.subsetMeshes.insert(meshData.subsetMeshes.begin(), subsetMesh);
    }
}


MeshProcessor::MeshProcessor(const std::vector<UsdPrim>& prims,
                             SplitMeshesSplitOn splitOn,
                             bool splitCollocatedPoints,
                             bool computeMedianExtentVolume,
                             bool computeMeshVolumes)
    : m_inputPrims(prims)
    , m_splitOn(splitOn)
    , m_splitCollocatedPoints(splitCollocatedPoints)
    , m_computeMedianExtentVolume(computeMedianExtentVolume)
    , m_computeMeshVolumes(computeMeshVolumes)
{
}


MeshProcessor::~MeshProcessor()
{
}


float MeshProcessor::getMedianExtentVolume() const
{
    return m_medianExtentVolume;
}


const std::vector<MeshData>& MeshProcessor::getOutputMeshData() const
{
    return m_outputMeshData;
}


void MeshProcessor::execute()
{
    std::vector<UsdPrim> meshPrims = _findStaticMeshes(m_inputPrims);

    // resize the output mesh data
    m_outputMeshData.clear();
    m_outputMeshData.resize(meshPrims.size());

    // May need to query materials
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    // process mesh prims in parallel
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, meshPrims.size()),
        [&](const tbb::blocked_range<size_t>& r)
        {
            // per-thread xform cache
            UsdGeomXformCache xformCache;

            for (size_t i = r.begin(); i < r.end(); ++i)
            {
                MeshData& meshData = m_outputMeshData[i];

                // Ensure that we only operate on Mesh prims.
                // TODO: Validate that we do not have time-sampled topology
                UsdGeomMesh mesh(meshPrims[i]);
                if (!mesh)
                {
                    continue;
                }
                meshData.valid = true;

                // create a VirtualMesh which we can use to compute any extent/volume/disjoint subsets
                meshData.baseMesh = VirtualMesh(meshPrims[i], xformCache, bindingsCache, collQueryCache);

                // calculate the extent volume of the base mesh
                meshData.baseMesh.validateAndComputeExtent();
                meshData.extentVolume = meshData.baseMesh.getExtentVolume();

                // calculate the volume of the mesh geometry if required
                if (m_computeMeshVolumes)
                {
                    meshData.geometryVolume = meshData.baseMesh.getGeometryVolume();
                }

                // find disjoint mesh subsets
                if (m_splitOn == SplitMeshesSplitOn::eVertices)
                {
                    // First check whether there are any subsets with material bindings.
                    // Need to record them before we collectDisjoint.
                    SubsetMap subsetMap;
                    _findMaterialSubsets(meshPrims[i], meshData.baseMesh.getFaceVertexCounts().size(), subsetMap);

                    // Create set, initially populated by the faceVertexIndices.
                    const VtIntArray& faceVertexIndices = meshData.baseMesh.getFaceVertexIndices();
                    DisjointSet disjointSet(faceVertexIndices.cdata(), faceVertexIndices.size());

                    // Process the mesh to find any disjoint sets.
                    _findDisjointMeshes(meshData, disjointSet, m_splitCollocatedPoints);

                    // finally collect
                    _collectDisjointMeshes(
                        meshData,
                        [&](size_t faceIndex, int firstFaceVertexIndex)
                        { return disjointSet.findSet(firstFaceVertexIndex); },
                        subsetMap);
                }
                else if (m_splitOn == SplitMeshesSplitOn::eGeomSubsets)
                {
                    _collectGeomSubsets(meshData);
                }
            }
        });

    // if required, sort the extent volumes, and compute the median
    m_medianExtentVolume = 0.0f;
    if (m_computeMedianExtentVolume)
    {
        // collect all the extent volumes so we can compute the median
        std::vector<float> extentVolumes;
        extentVolumes.reserve(m_outputMeshData.size());
        for (const MeshData& meshData : m_outputMeshData)
        {
            if (meshData.valid)
            {
                extentVolumes.push_back(meshData.extentVolume);
            }
        }

        if (!extentVolumes.empty())
        {
            // compute the median extent volume
            const size_t midIndex = extentVolumes.size() / 2;
            std::nth_element(extentVolumes.begin(), extentVolumes.begin() + midIndex, extentVolumes.end());
            m_medianExtentVolume = extentVolumes[midIndex];
        }
    }
}


void MeshProcessor::clear()
{
    m_inputPrims.clear();
    m_outputMeshData.clear();
}


} // namespace omni::scene::optimizer
