// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/Stats.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/TbbCompat.h"
#include "omni/scene.optimizer/core/Utils.h"
#include "omni/scene.optimizer/core/geometry/DisjointSet.h"
#include "omni/scene.optimizer/core/geometry/MeshProcessor.h"

// USD
#include <pxr/usd/ar/resolverScopedCache.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/xform.h>

// TBB
#include <tbb/combinable.h>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


class StatArgs::Impl
{
public:
    bool disjoint = false;
    bool primvars = false;
    bool timeSamples = false;
    bool splitCollocated = false;
    UsdTimeCode timeCode = UsdTimeCode::Default();
};


StatArgs::StatArgs()
    : pImpl(std::make_unique<Impl>())
{
}


StatArgs::~StatArgs() = default;


bool StatArgs::getDisjoint() const
{
    return pImpl->disjoint;
}


void StatArgs::setDisjoint(bool value)
{
    pImpl->disjoint = value;
}


bool StatArgs::getCountPrimvars() const
{
    return pImpl->primvars;
}


void StatArgs::setCountPrimvars(bool value)
{
    pImpl->primvars = value;
}


bool StatArgs::getTimeSamples() const
{
    return pImpl->timeSamples;
}


void StatArgs::setTimeSamples(bool value)
{
    pImpl->timeSamples = value;
}


bool StatArgs::getSplitCollocated() const
{
    return pImpl->splitCollocated;
}


void StatArgs::setSplitCollocated(bool value)
{
    pImpl->splitCollocated = value;
}


UsdTimeCode StatArgs::getTimeCode() const
{
    return pImpl->timeCode;
}


void StatArgs::setTimeCode(const UsdTimeCode& value)
{
    pImpl->timeCode = value;
}


void PrimInfo::operator+=(const PrimInfo& other)
{
    count += other.count;
    disjoint += other.disjoint;
    inactive += other.inactive;
    leaf += other.leaf;
    invisible += other.invisible;
    uniqueMaterials.insert(other.uniqueMaterials.begin(), other.uniqueMaterials.end());
    unique += other.unique;
}


void PrimvarStats::operator+=(const PrimvarStats& other)
{
    count += other.count;
    valueCount += other.valueCount;

    // Assume that if a value is zero, and another is not, the non-zero value is
    // correct. In practice, they should generally always have the same value, but
    // for combining from a default empty instance this will get the populated
    // value.
    if (other.sizeOf)
    {
        sizeOf = other.sizeOf;
    }
}


StatCounters StatCounters::operator+(const StatCounters& other) const
{
    StatCounters result;
    result.prims = prims + other.prims;
    result.instanceable = instanceable + other.instanceable;
    result.instances = instances + other.instances;
    result.inactive = inactive + other.inactive;
    result.invisible = invisible + other.invisible;
    result.prototypes = prototypes + other.prototypes;
    result.timeSamples = timeSamples + other.timeSamples;
    result.vertices = vertices + other.vertices;
    result.faces = faces + other.faces;
    result.geometries = geometries + other.geometries;

    // Copy primTypes to result
    for (const auto& it : primTypes)
    {
        result.primTypes[it.first] = it.second;
    }

    // Add the other primTypes to result
    for (const auto& it : other.primTypes)
    {
        result.primTypes[it.first] += it.second;
    }

    // Copy primvar counts
    for (const auto& it : primvars)
    {
        result.primvars[it.first] += it.second;
    }

    // Copy other primvar counts
    for (const auto& it : other.primvars)
    {
        result.primvars[it.first] += it.second;
    }

    return result;
}


/// Helper function to count the number of disjoint meshes a given UsdPrim mesh contains.
///
/// Returns 0 if the prim is not a mesh, or if there are no vertices. Otherwise, returns the number of meshes that
/// don't share vertices. Ie, if all vertices are joined within a mesh then this method will return 1.
///
/// \param prim A mesh
/// \param splitCollocated Should collocated points be considered disjoint
/// \param bindingsCache Bindings cache
/// \param collQueryCache Collection query cache
/// \return The number of disjoint meshes.
static size_t _countDisjointMeshes(const UsdPrim& prim,
                                   bool splitCollocated,
                                   UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                                   UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache)
{
    // Create a VirtualMesh which we can use to compute the disjoint subsets
    UsdGeomXformCache xformCache; // not thread-safe
    MeshData meshData;
    meshData.baseMesh = VirtualMesh(prim, xformCache, bindingsCache, collQueryCache);

    // Create set, initially populated by the faceVertexIndices.
    DisjointSet disjointSet(meshData.baseMesh.getFaceVertexIndices().cdata(),
                            meshData.baseMesh.getFaceVertexIndices().size());

    // Defer to MeshProcessor to find the disjoint meshes.
    _findDisjointMeshes(meshData, disjointSet, splitCollocated);

    int faceOffset = 0;
    std::unordered_set<int> disjointMeshes;

    const auto _fvc = meshData.baseMesh.getFaceVertexCounts().cdata();
    const auto _fvi = meshData.baseMesh.getFaceVertexIndices().cdata();

    // Count disjoint meshes. All vertices within a face are joined, so we can just check one per face.
    for (int i = 0; i < static_cast<int>(meshData.baseMesh.getFaceVertexCounts().size()); ++i)
    {
        int set = disjointSet.findSet(_fvi[faceOffset]);
        disjointMeshes.insert(set);

        faceOffset += _fvc[i];
    }

    return disjointMeshes.size();
}


/// In certain situations we have enough information to _assume_ the number of
/// primvar values. For example, for a uniform primvar we know it should be
/// the number of faces. This may not be 100% accurate for badly authored
/// data, but, it will match what would theoretically be given to the renderer
/// and considered valid.
///
/// Basically this means we can assume the number and avoid retrieving the
/// data to count it.
static bool _guessPrimvarValueCount(const UsdGeomPrimvar& primvar,
                                    const size_t points,
                                    const size_t faces,
                                    const size_t totalVertices,
                                    PrimvarStats& stats)
{
    const TfToken& interpolation = primvar.GetInterpolation();

    if (interpolation == UsdGeomTokens->constant)
    {
        stats.valueCount += 1;
        return true;
    }

    if (faces && interpolation == UsdGeomTokens->uniform)
    {
        stats.valueCount += faces;
        return true;
    }

    if (points && (interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying))
    {
        stats.valueCount += points;
        return true;
    }

    if (totalVertices && interpolation == UsdGeomTokens->faceVarying)
    {
        stats.valueCount += totalVertices;
        return true;
    }

    return false;
}


StatCountersUPtr _collectSceneStats(const UsdStageWeakPtr& usdStage, const StatArgs& args)
{
    // Add a scoped asset resolver cache to improve performance querying asset path types,
    // in particular when counting unique materials that may have a source asset.
    ArResolverScopedCache parentResolverScopedCache;

    // Bindings caches (thread-safe)
    // Used just for the virtual mesh constructor when counting disjoint meshes, although
    // they won't actually be used.
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    // Object to be fed to parallel_do
    struct ParallelTask
    {
        UsdPrim prim;
        TfToken visibility;
    };

    const Usd_PrimFlagsPredicate predicate = UsdTraverseInstanceProxies(UsdPrimAllPrimsPredicate);

    // We don't count the pseudo root, so start with its children. This lets us avoid
    // checking for the pseudo root every iteration.
    std::vector<ParallelTask> prims;
    for (const auto& prim : usdStage->GetPseudoRoot().GetFilteredChildren(predicate))
    {
        prims.emplace_back(ParallelTask{ prim, UsdGeomTokens->inherited });
    }

    // Cache args to avoid repeated pimpl function pointer dereference
    const auto& timeCode = args.getTimeCode();

    const bool doTimeSamples = args.getTimeSamples();
    const bool doPrimvars = args.getCountPrimvars();
    const bool doDisjoint = args.getDisjoint();
    const bool doCollocated = args.getSplitCollocated();

    tbb::combinable<StatCounters> counters;

    tbbcompat::parallelForEach(
        prims.begin(),
        prims.end(),
        [&](const ParallelTask& task, tbbcompat::Feeder<ParallelTask>& feeder)
        {
            const UsdPrim& prim = task.prim;
            TfToken visibility = task.visibility;

            // First check visibility, before feeding the children.
            // For large scenes it is substantially faster to manually manage the vis
            // as we iterate than to use ComputeVisibility.
            UsdGeomImageable imageable(prim);
            if (imageable && visibility != UsdGeomTokens->invisible)
            {
                TfToken localVis;
                imageable.GetVisibilityAttr().Get(&localVis, timeCode);
                if (localVis == UsdGeomTokens->invisible)
                {
                    visibility = localVis;
                }
            }

            const auto& children = prim.GetFilteredChildren(predicate);

            // Queue all children for traversal via parallel_do
            for (const auto& child : children)
            {
                feeder.add({ child, visibility });
            }

            // Count prim and its info
            auto& _counters = counters.local();
            ++_counters.prims;

            if (prim.IsInstanceable())
            {
                ++_counters.instanceable;
            }

            if (prim.IsInstance())
            {
                ++_counters.instances;
            }

            std::string typeName = prim.GetTypeName().GetString();
            if (typeName.empty())
            {
                typeName = "No Type";
            }

            PrimInfo& perPrimTypeInfo = _counters.primTypes[typeName];

            // Bump overall prim type count
            ++perPrimTypeInfo.count;

            if (!prim.IsActive())
            {
                ++_counters.inactive;
                ++perPrimTypeInfo.inactive;
            }

            // If this prim is invisible then adjust the counter
            if (imageable && visibility == UsdGeomTokens->invisible)
            {
                ++_counters.invisible;
                ++perPrimTypeInfo.invisible;
            }

            // Verbose (slow) info: count number of time samples.
            if (doTimeSamples)
            {
                for (const auto& attribute : prim.GetAuthoredAttributes())
                {
                    _counters.timeSamples += attribute.GetNumTimeSamples();
                }
            }

            size_t faces = 0;
            size_t points = 0;
            size_t totalVertices = 0;

            bool defined = prim.IsDefined();

            if (prim.IsA<UsdGeomXform>() || typeName == "Scope")
            {
                // Need to check for instance proxy children to verify whether this is actually empty.
                if (children.empty())
                {
                    ++perPrimTypeInfo.leaf;
                }
            }
            else if (prim.IsA<UsdShadeMaterial>() && !prim.IsInstanceProxy())
            {
                // Thread-specific child resolver cache
                // The main attributes we actually read, and therefore might need resolving,
                // are from materials to do the hashing. The overhead of creating the cache
                // can add up, so we can localize it here.
                ArResolverScopedCache _resolverScopedCache(&parentResolverScopedCache);

                // Hash materials to figure out how many unique ones exist in a scene. However, if they are
                // an instance proxy, we know they are not unique.
                HashCache hashCache;
                perPrimTypeInfo.uniqueMaterials.insert(_hashPrim(usdStage, prim, hashCache));
            }
            else if (const auto pointBased = UsdGeomPointBased(prim))
            {

                if (defined)
                {
                    ++_counters.geometries;

                    VtVec3fArray _points;
                    pointBased.GetPointsAttr().Get(&_points, timeCode);

                    points = _points.size();
                    _counters.vertices += points;
                }

                UsdGeomMesh mesh(prim);
                if (mesh)
                {
                    if (!prim.IsInstanceProxy())
                    {
                        ++perPrimTypeInfo.unique;
                    }

                    if (defined)
                    {
                        if (doDisjoint)
                        {
                            // Count disjoint meshes
                            size_t disjoint = _countDisjointMeshes(prim, doCollocated, bindingsCache, collQueryCache);
                            if (disjoint > 1)
                            {
                                perPrimTypeInfo.disjoint += disjoint;
                            }
                        }

                        // For meshes, also count faces
                        VtIntArray faceVertexCounts;
                        mesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts, timeCode);

                        faces = faceVertexCounts.size();
                        _counters.faces += faces;

                        // Count vertices. We use the _counts_ array for this for two reasons:
                        // first, this should describe which ones are used in case there is bad
                        // data and extra in the indices array. Second, it saves us getting the
                        // indices and paying the cost of decoding that array also. We don't need
                        // the values themselves, only the totals.
                        for (const auto& it : faceVertexCounts.AsConst())
                        {
                            totalVertices += it;
                        }
                    }
                }
            }

            if (doPrimvars && defined)
            {

                UsdGeomPrimvarsAPI primvarsAPI(prim);

                for (const auto& it : primvarsAPI.GetPrimvarsWithAuthoredValues())
                {
                    PrimvarStats& primvarStats = _counters.primvars[it.GetBaseName()];

                    ++primvarStats.count;

                    auto primvarTypeName = it.GetTypeName().GetScalarType();
                    primvarStats.sizeOf = _getSizeFromSdfValueType(primvarTypeName);

                    // Try and guess the expected value count.
                    // If not, fall back to querying the values
                    if (!_guessPrimvarValueCount(it, points, faces, totalVertices, primvarStats))
                    {

                        // If indexed, count the number of indices. Otherwise,
                        // just the number of values.
                        if (it.IsIndexed())
                        {
                            VtIntArray indices;
                            it.GetIndices(&indices, timeCode);
                            primvarStats.valueCount += indices.size();
                        }
                        else
                        {
                            VtValue value;
                            it.Get(&value, timeCode);

                            if (value.IsArrayValued())
                            {
                                primvarStats.valueCount += value.GetArraySize();
                            }
                            else
                            {
                                ++primvarStats.valueCount;
                            }
                        }
                    }
                }
            }
        });

    // Combine the individual Counters together for the end result
    StatCounters result = counters.combine(std::plus<StatCounters>());

    // Count prototypes
#if PXR_VERSION >= 2011
    result.prototypes = usdStage->GetPrototypes().size();
#else
    result.prototypes = usdStage->GetMasters().size();
#endif

    return std::make_unique<StatCounters>(result);
}


} // namespace omni::scene::optimizer
