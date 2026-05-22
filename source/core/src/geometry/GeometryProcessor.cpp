// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/GeometryProcessor.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"
#include "omni/scene.optimizer/core/Log.h"

// USD
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/plane.h>
#include <pxr/usd/usdGeom/pointInstancer.h>
#include <pxr/usd/usdGeom/sphere.h>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


GeometryProcessor::GeometryProcessor(const UsdStageWeakPtr& stage, const std::vector<std::string>& inputPaths)
    : m_stage(stage)
    , m_inputPaths(inputPaths)
{
}


GeometryProcessor::~GeometryProcessor()
{
}


void GeometryProcessor::setComputeRtxMeshCount(bool state)
{
    m_computeRtxMeshCount = state;
}


void GeometryProcessor::execute()
{
    UsdGeomXformCache xformCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    // build the list of prims to traverse
    std::vector<UsdPrim> primsToTraverse;
    if (m_inputPaths.empty())
    {
        // use root if no input paths specified
        primsToTraverse.push_back(m_stage->GetPseudoRoot());
    }
    else
    {
        for (const std::string& pathStr : m_inputPaths)
        {
            SdfPath path(pathStr);
            UsdPrim prim = m_stage->GetPrimAtPath(path);
            if (prim)
            {
                primsToTraverse.push_back(prim);
            }
            else
            {
                SO_LOG_WARN("GeometryProcessor: Input path '%s' does not resolve to a valid prim",
                            pathStr.c_str()); // LCOV_EXCL_LINE
            }
        }
    }

    // traverse the prims in the stage
    for (const UsdPrim& prim : primsToTraverse)
    {
        m_rtxAccelStructCount += traversePrims(prim, xformCache, bindingsCache, collQueryCache);
    }
}


void GeometryProcessor::clear()
{
    m_visitedPrototypes.clear();
    m_visitedPrims.clear();
    m_rtxDuplicateHashes.clear();
    m_badExtentsGPrims.clear();
    m_degenerateGPrims.clear();
    m_invisibleGPrims.clear();
    m_cameras.clear();
    m_rtxMeshPrims.clear();
    m_rtxAccelStructCount = 0;
}


const std::vector<SdfPath>& GeometryProcessor::getRtxMeshPrims() const
{
    return m_rtxMeshPrims;
}


size_t GeometryProcessor::getRtxAccelStructCount() const
{
    return m_rtxAccelStructCount;
}


size_t GeometryProcessor::getRtxMeshCount() const
{
    return m_rtxMeshPrims.size();
}


size_t GeometryProcessor::getRtxUniqueMeshCount() const
{
    return m_rtxDuplicateHashes.size();
}


size_t GeometryProcessor::traversePrims(const UsdPrim& startPrim,
                                        UsdGeomXformCache& xformCache,
                                        UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                                        UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache,
                                        bool inPointInstancer)
{
    size_t accelStructCount = 0;

    // for now the GeometryProcessor only supports the RTX mesh count analysis - so if that is not enabled just return
    if (!m_computeRtxMeshCount)
    {
        return accelStructCount; // LCOV_EXCL_LINE
    }

    // iterate from this prim
    static const Usd_PrimFlagsPredicate predicate = UsdPrimIsActive && UsdPrimIsLoaded;
    auto primRange = UsdPrimRange(startPrim, predicate);
    for (auto iter = primRange.begin(); iter != primRange.end(); ++iter)
    {
        const auto& prim = (*iter);

        // if this is not traversing inside a point instancer prototypes, do some early-out checks
        if (!inPointInstancer)
        {
            // prim already visited, undefined or abstract
            if (!m_visitedPrims.insert(prim.GetPath()).second || !prim.IsDefined() || prim.IsAbstract())
            {
                iter.PruneChildren();
                continue;
            }
        }

        // TODO: we can optimize this by tracking visibility ourselves as we traverse, its much faster than calling
        //       ComputeVisibility for each prim.
        // is this prim invisible?
        bool visible = true;
        if (UsdGeomImageable imageable = UsdGeomImageable(prim))
        {
            if (imageable.ComputeVisibility() == UsdGeomTokens->invisible)
            {
                visible = false;
            }
        }

        // is this a point instancer?
        UsdGeomPointInstancer pointInstancer(prim);
        if (pointInstancer)
        {
            // get the prototype paths
            UsdRelationship prototypeRel = pointInstancer.GetPrototypesRel();
            SdfPathVector prototypeTargets;
            prototypeRel.GetTargets(&prototypeTargets);

            // get the prototype prims
            std::vector<UsdPrim> prototypePrims;
            prototypePrims.reserve(prototypeTargets.size());
            for (const SdfPath& targetPath : prototypeTargets)
            {
                prototypePrims.push_back(prim.GetStage()->GetPrimAtPath(targetPath));
            }

            // get the indices of the prototype of each instance
            UsdAttribute protoIndicesAttr = pointInstancer.GetProtoIndicesAttr();
            VtIntArray protoIndices;
            if (protoIndicesAttr)
            {
                protoIndicesAttr.Get(&protoIndices);
            }

            // get the instance ids - if not defined or invalid, create a default set
            UsdAttribute idsAttr = pointInstancer.GetIdsAttr();
            VtInt64Array instanceIds;
            if (idsAttr)
            {
                idsAttr.Get(&instanceIds);
            }
            if (instanceIds.size() != protoIndices.size())
            {
                instanceIds.clear();
                for (size_t i = 0; i < protoIndices.size(); ++i)
                {
                    instanceIds.push_back(static_cast<int64_t>(i));
                }
            }

            // get the ids of invisible instances
            std::unordered_set<int64_t> invisibleIdsSet;
            UsdAttribute invisibleIdsAttr = pointInstancer.GetInvisibleIdsAttr();
            if (invisibleIdsAttr)
            {
                VtInt64Array invisibleIds;
                invisibleIdsAttr.Get(&invisibleIds);
                invisibleIdsSet.insert(invisibleIds.cbegin(), invisibleIds.cend());
            }

            // construct a map of prototype prims to their visible instance count, we need to initialize to zero for
            // each prototype to ensure we still traverse prototypes that have no visible instances or are not used
            // because their internal meshes still count towards the RTX mesh limit
            std::map<UsdPrim, size_t> prototypeCounts;
            for (const UsdPrim& prototype : prototypePrims)
            {
                prototypeCounts[prototype] = 0;
            }

            // now collect the visible number of each prototype
            auto protoIndicesRaw = protoIndices.cdata();
            auto instanceIdsRaw = instanceIds.cdata();
            for (size_t i = 0; i < protoIndices.size(); ++i)
            {
                int protoIndex = protoIndicesRaw[i];
                if (protoIndex < 0 || static_cast<size_t>(protoIndex) >= prototypeTargets.size())
                {
                    continue; // LCOV_EXCL_LINE
                }

                // get the instance id and check if it is invisible - if so don't increment count
                int64_t instanceId = instanceIdsRaw[i];
                if (invisibleIdsSet.find(instanceId) != invisibleIdsSet.end())
                {
                    continue;
                }

                // find the prototype count and increment it
                auto findCount = prototypeCounts.find(prototypePrims[protoIndex]);
                if (findCount != prototypeCounts.end())
                {
                    ++findCount->second;
                }
            }

            // for each prototype, traverse it to count the internal RTX meshes and keep track of the acceleration
            // structure count
            size_t instanceAccelStructCount = 0;
            for (const auto& [prototype, count] : prototypeCounts)
            {
                // accumulate total accel struct count
                instanceAccelStructCount +=
                    count * traverseInstancePrototype(prototype, xformCache, bindingsCache, collQueryCache, true);
            }

            // only increase the acceleration structure count if the instance is visible
            if (visible)
            {
                accelStructCount += instanceAccelStructCount;
            }

            // go no further under point instancers
            iter.PruneChildren();
            continue;
        }

        // if this this is an instance, traverse into the prototype
        if (prim.IsInstance())
        {
            // traverse the prototype, regardless of whether this instance is visible or not because we need to count
            // the meshes
            const size_t prototypeAccelStructCount =
                traverseInstancePrototype(prim.GetPrototype(), xformCache, bindingsCache, collQueryCache, inPointInstancer);
            // only increase the acceleration structure count if the instance is visible
            if (visible)
            {
                accelStructCount += prototypeAccelStructCount;
            }

            // go no further under instances
            iter.PruneChildren();
            continue;
        }

        // if this is a camera, record it and process no further
        UsdGeomCamera camera(prim);
        if (camera)
        {
            // ignore the built-in cameras
            if (!TfStringStartsWith(prim.GetName().GetString(), "OmniverseKit_"))
            {
                m_cameras.push_back(prim.GetPath());
            }
            iter.PruneChildren();
            continue;
        }

        // is this a geometry prim?
        UsdGeomGprim gPrim(prim);
        if (!gPrim)
        {
            // keep iterating
            continue;
        }

        // once we've reached a geometry prim, we don't need to look any further under it
        iter.PruneChildren();

        // is this a point based prim - then check the points to see if it is degenerate, these don't contribute to RTX
        // meshes
        UsdGeomPointBased pointBased(prim);
        if (pointBased)
        {
            UsdAttribute pointsAttr = pointBased.GetPointsAttr();
            VtVec3fArray points;
            pointsAttr.Get(&points);
            if (points.empty())
            {
                m_degenerateGPrims.push_back(prim.GetPath());
                continue;
            }
        }

        // only add to acceleration structures if visible
        if (visible)
        {
            ++accelStructCount;
        }
        // otherwise record as invisible geometry prim
        else
        {
            m_invisibleGPrims.push_back(prim.GetPath());
        }

        // if this isn't a mesh, check for geometry primitives
        UsdGeomMesh mesh(prim);
        if (!mesh)
        {
            // special case handling for geometry primitives, we need to count each of the same type as a duplicate
            if (UsdGeomCapsule(prim))
            {
                static const size_t capsuleHash = std::hash<std::string>()("Capsule");
                recordRtxMesh(prim.GetPath(), capsuleHash);
            }
            if (UsdGeomCone(prim))
            {
                static const size_t coneHash = std::hash<std::string>()("Cone");
                recordRtxMesh(prim.GetPath(), coneHash);
            }
            if (UsdGeomCube(prim))
            {
                static const size_t cubeHash = std::hash<std::string>()("Cube");
                recordRtxMesh(prim.GetPath(), cubeHash);
            }
            if (UsdGeomCylinder(prim))
            {
                static const size_t cylinderHash = std::hash<std::string>()("Cylinder");
                recordRtxMesh(prim.GetPath(), cylinderHash);
            }
            if (UsdGeomSphere(prim))
            {
                static const size_t sphereHash = std::hash<std::string>()("Sphere");
                recordRtxMesh(prim.GetPath(), sphereHash);
            }

            continue;
        }

        // create a VirtualMesh from the prim
        VirtualMesh virtualMesh(prim, xformCache, bindingsCache, collQueryCache);
        virtualMesh.validateAndComputeExtent();

        // does this mesh have bad extents?
        const float size = virtualMesh.getExtentMaxSize();
        if (size <= std::numeric_limits<float>::epsilon() || !std::isfinite(size))
        {
            m_badExtentsGPrims.push_back(prim.GetPath());
        }

        // compute the RTX duplicate hash for this mesh and update the map of hashes to prim paths
        recordRtxMesh(prim.GetPath(), virtualMesh.getRtxDuplicateHash());
    }

    return accelStructCount;
}


size_t GeometryProcessor::traverseInstancePrototype(const UsdPrim& prototypePrim,
                                                    UsdGeomXformCache& xformCache,
                                                    UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                                                    UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache,
                                                    bool inPointInstancer)
{
    if (!prototypePrim)
    {
        return 0; // LCOV_EXCL_LINE
    }

    // if we're already in a point instancer, then nested instances are treated more like references - which means we
    // need to do a full traversal of the prototype each time (in future we could cache the RTX mesh count for nested)
    if (inPointInstancer)
    {
        return traversePrims(prototypePrim, xformCache, bindingsCache, collQueryCache, inPointInstancer);
    }

    // otherwise we need to look up if we've already visited this prototype so we don't increase the RTX mesh count
    // multiple times
    size_t instanceAccelStructCount = 0;
    auto findVisited = m_visitedPrototypes.find(prototypePrim.GetPath());

    // already visited this prototype - just retrieve the accel struct count
    if (findVisited != m_visitedPrototypes.end())
    {
        instanceAccelStructCount = findVisited->second;
    }
    // unvisted prototype - traverse it now
    else
    {
        instanceAccelStructCount = traversePrims(prototypePrim, xformCache, bindingsCache, collQueryCache, false);
        m_visitedPrototypes.insert({ prototypePrim.GetPath(), instanceAccelStructCount });
    }

    return instanceAccelStructCount;
}


void GeometryProcessor::recordRtxMesh(const SdfPath& primPath, size_t hash)
{
    m_rtxMeshPrims.push_back(primPath);
    m_rtxDuplicateHashes[hash].insert(primPath);
}


} // namespace omni::scene::optimizer
