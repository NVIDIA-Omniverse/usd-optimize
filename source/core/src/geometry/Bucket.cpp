// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/Bucket.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdPhysics/tokens.h>

// TBB
#include <tbb/parallel_for.h>
#include <tbb/task_arena.h>
#include <tbb/task_group.h>

PXR_NAMESPACE_USING_DIRECTIVE


namespace omni::scene::optimizer
{


// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((materialBindingAPI, "MaterialBindingAPI"))
    ((primvarsDisplayColorIndices, "primvars:displayColor:indices"))
    ((primvarsDisplayOpacityIndices, "primvars:displayOpacity:indices"))
    ((primvarsDoNotCastShadows, "primvars:doNotCastShadows"))
    ((primvarsNormals, "primvars:normals"))
    ((primvarsNormalsIndices, "primvars:normals:indices"))
    ((primvarsSt, "primvars:st"))
    ((primvarsStIndices, "primvars:st:indices"))
    ((primvarsSt0, "primvars:st0"))
    ((primvarsSt0Indices, "primvars:st0:indices"))
    ((primvarsSt1, "primvars:st1"))
    ((primvarsSt1Indices, "primvars:st1:indices"))
    ((primvarsSt2, "primvars:st2"))
    ((primvarsSt2Indices, "primvars:st2:indices"))
    ((subsetFamilyNamespace, "subsetFamily:"))
    ((xformOpNamespace, "xformOp:"))
);
// LCOV_EXCL_STOP
// clang-format on


// Bucket limit based on vertex count of merged mesh.
constexpr size_t BUCKET_MAX_DATA_VOLUME = 10000000;


// Category strings
constexpr const char* s_reportCategoryBucket = "BUCKET";
constexpr const char* s_reportCategoryHashInfo = "BUCKET.HASHDESC";


/// Return true if the name is in the set of matching names or has an overlapping namespace.
static bool _isMatchingName(const TfToken& value, const std::set<TfToken>& names, const std::set<TfToken>& namespaces)
{
    // Return a match if the value is in the vector of full names.
    if (names.find(value) != names.end())
    {
        return true;
    }

    // Return a match if the value starts with any of the given namespaces.
    for (const auto& _namespace : namespaces)
    {
        if (TfStringStartsWith(value, _namespace))
        {
            return true;
        }
    }

    // Not a match if we got this far.
    return false;
}


/// Populate the value based attributes of the PreBucket struct.
///
/// \param preBucket Struct to populate.
/// \param names Names of attributes to collect values for.
/// \param ancestorPath Path of ancestor that defines scope in which inherited attributes should be considered.
static void _collectValueAttrInfo(PreBucket& preBucket, const std::set<TfToken>& names, const SdfPath& ancestorPath)
{
    const VirtualMesh& virtualMesh = preBucket.virtualMesh;
    const UsdPrim& prim = virtualMesh.getPrim();

    // Collect the data that makes this prim unique and cannot be merged across prims
    for (const TfToken& name : names)
    {
        VtValue attrValue;

        // Calculate the effective visibility opinion between this prim and the output prim.
        if (name == UsdGeomTokens->visibility)
        {
            // We only want to consider visibility values on prims back to the common parent
            // of this prim and the output parent as those are the opinions that need to be
            // maintained on the merged mesh.

            // Walk back up parent hierarchy till we find an invisible prim
            UsdPrim _prim = prim;
            while (_prim.GetPath() != ancestorPath)
            {
                // Only check the visibility attribute if the prim type inherits from
                // UsdGeomImageable
                UsdGeomImageable imageable(_prim);
                if (imageable)
                {
                    imageable.GetVisibilityAttr().Get(&attrValue);
                    if (attrValue == UsdGeomTokens->invisible)
                    {
                        break;
                    }
                }

                // Update parent so we eventually reach the common parent
                _prim = _prim.GetParent();
            }
        }
        // calculate the effective purpose of the prim to ensure the merged prim retains
        // purpose
        else if (name == UsdGeomTokens->purpose)
        {
            attrValue = UsdGeomImageable(prim).ComputePurpose();
        }
        // get the attribute value for any attributes that are not special cased
        else
        {
            // get the value from the VirtualMesh
            if (!virtualMesh.getValue(name, attrValue))
            {
                // TODO: Currently we still need to get the attribute from the prim if we can't find it on the
                //       VirtualMesh because the attribute may have a default value. In future we should figure out a
                //       solution for this so we don't have to use the prim here since it may cause problems with more
                //       complex cases
                prim.GetAttribute(name).Get(&attrValue);
            }
        }

        // Add the attribute name and value to the dictionary
        // Use a "/" delimiter so that attribute names such as primvars:doNotCastShadows do not become nested.
        preBucket.attributes.SetValueAtPath(name, attrValue, "/");
    }
}


/// Populate the authored state based attributes of the PreBucket struct.
///
/// \param preBucket Struct to populate.
/// \param names Names of attributes to check for authored values.
static void _collectAuthoredAttrInfo(PreBucket& preBucket, const std::set<TfToken>& names)
{
    // Names commonly used for primvars that have on logically inheritable value.
    static const std::set<TfToken> nonInheritablePrimvarNames = { _tokens->primvarsSt,
                                                                  _tokens->primvarsSt0,
                                                                  _tokens->primvarsSt1,
                                                                  _tokens->primvarsSt2 };

    const VirtualMesh& virtualMesh = preBucket.virtualMesh;
    const UsdPrim& prim = virtualMesh.getPrim();

    // For attributes we can merge that do not have logical default values we need to
    // consider their presence as part of bucket uniqueness otherwise we produce garbage
    // values during merge.
    for (const TfToken& name : names)
    {
        bool isAuthored = false;
        VtValue attrValue;
        if (virtualMesh.getValue(name, attrValue))
        {
            isAuthored = true;
        }
        else
        {
            // TODO: at some point this block should be cleaned up to only use the VirtualMesh and not use the prim

            // Normal data can be stored in an attribute named "primvars:normals" or "normals"
            // so we need to check for both when deciding if this attribute is authored
            if (name == _tokens->primvarsNormals)
            {
                const UsdAttribute& normalsAttr = UsdGeomMesh(prim).GetNormalsAttr();
                if (normalsAttr && normalsAttr.HasAuthoredValue())
                {
                    isAuthored = true;
                }
            }
            // Certain primvars do not make sense as constant interpolation and therefore would logically never inherit
            // Special case them here to avoid checking all parents for no benefit.
            else if (nonInheritablePrimvarNames.find(name) == nonInheritablePrimvarNames.end())
            {
                // Primvars can be inherited so we need to look for authored values on parent prims
                if (name.GetString().rfind("primvars:", 0) == 0)
                {
                    // Start from the parent because we've already checked the prim itself.
                    const UsdGeomPrimvar& primvar = UsdGeomPrimvarsAPI(prim.GetParent()).FindPrimvarWithInheritance(name);
                    if (primvar && primvar.HasAuthoredValue())
                    {
                        isAuthored = true;
                    }
                }
            }
        }

        // Store the presence/absence of authored values in a VtDictionary so it hashes consistently
        preBucket.authoredAttributes.SetValueAtPath(name, VtValue(isAuthored), "/");
    }
}


/// Populate bound material in the PreBucket struct.
///
/// \param preBucket Struct to populate.
/// \param bindingsCache Cache to improve performance of ComputeBoundMaterial call.
/// \param collQueryCache Cache to improve performance of ComputeBoundMaterial call.
static void _collectMaterialBindingInfo(PreBucket& preBucket,
                                        UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                                        UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache)
{
    // first check if the we can retrieve the bound material directly from the virtual mesh
    if (preBucket.virtualMesh.hasExplicitMaterialBinding())
    {
        preBucket.boundMaterialPath = preBucket.virtualMesh.getBoundMaterialPath();
    }
    // otherwise resolve material binding if this is directly derived from a prim
    else if (preBucket.virtualMesh.isDerivedFromPrim())
    {
        UsdShadeMaterialBindingAPI bindingAPI(preBucket.virtualMesh.getPrim());
        UsdShadeMaterial boundMaterial = bindingAPI.ComputeBoundMaterial(&bindingsCache, &collQueryCache);
        if (boundMaterial)
        {
            preBucket.boundMaterialPath = boundMaterial.GetPath();
        }
    }
}


using BucketLookup = std::map<size_t, std::vector<size_t>>;


class Bucketer::Impl
{
public:
    ExecutionContext* m_context;
    std::shared_ptr<Report> m_report = nullptr;

    // The VirtualMeshes to bucket, paired with the root path they can be merged at
    std::vector<std::pair<SdfPath, VirtualMesh>> m_inputData;
    // The new VirtualMeshs that are the result of the bucketing the input data
    std::vector<VirtualMesh> m_outputData;

    std::string m_outputName;

    bool m_considerMaterials = true;
    bool m_considerPrimAttributes = false;
    bool m_allowSingleMeshes = false;
    bool m_collectMeshInfo = true;

    // Spatial Bucketing Config
    ClusterMode m_spatialMode = ClusterMode::eNone;
    double m_spatialThreshold = 0.0;
    double m_spatialMaxSize = 0.0;

    // whether to ignore data volume bucketing
    bool m_ignoreDataVolume = false;

    // Names of attributes where the value will be considered in the prim info hash.
    std::set<TfToken> m_explicitValueAttributes;
    std::set<TfToken> m_valueAttributes;

    // Names of attributes where the presence of an authored value will be considered in the prim info hash.
    std::set<TfToken> m_authoredAttributes;

    // Names of attributes that will not be considered in the prim info hash.
    std::set<TfToken> m_ignoreAttrNames;
    std::set<TfToken> m_ignoreAttrNamespaces;


    explicit Impl(ExecutionContext* context)
        : m_context(context){};

    void AddVirtualMeshes(const std::vector<VirtualMesh>& virtualMeshes, const SdfPath& parentPath)
    {
        m_inputData.reserve(m_inputData.size() + virtualMeshes.size());

        // add each virtual mesh and its parent path - ensure geometry is computed so we can bucket it correctly
        for (const VirtualMesh& virtualMesh : virtualMeshes)
        {
            // note: VirtualMeshes are reference counted - doesn't actually make a copy
            VirtualMesh addMesh = virtualMesh;
            addMesh.computeGeometry();
            m_inputData.push_back(std::make_pair(parentPath, addMesh));
        }
    }

    void Bucket(const UsdStageWeakPtr& stage)
    {
        CARB_PROFILE_ZONE(0, "SceneOptimizer|Bucketer|Bucket");

        // Sanitize attribute name sets to avoid any overlap.
        SanitizeAttributes();

        // Pre-allocate a results vector to the appropriate size, to allow for multiple
        // threads to process.
        std::vector<PreBucket> preBuckets;
        preBuckets.reserve(m_inputData.size());
        for (auto& iter : m_inputData)
        {
            preBuckets.emplace_back(iter.second);
        }

        PopulatePreBuckets(preBuckets);

        // use spatial clustering?
        if (m_spatialMode != ClusterMode::eNone)
        {
            SpatiallyClusterPrims(preBuckets);
        }

        // create the VirtualMesh buckets
        CreateVirtualMeshBuckets(preBuckets, stage);

        // If reporting is enabled then log useful stuff
        if (m_report)
        {
            ProcessReport(preBuckets);
        }

        // Remove the input data, no need to store it now.
        Clear();
    }

private:
    void SanitizeAttributes()
    {
        // If the bucketer is set to consider prim attributes, collect the names of all authored attributes on
        // VirtualMeshes that will be processed during bucket. The values of these attributes will contribute to the
        // bucket key and be available from the bucket struct.
        if (m_considerPrimAttributes)
        {
            // Get all the authored attribute names from prims.
            TfToken::HashSet allNames;
            for (const auto& iter : m_inputData)
            {
                iter.second.extendAttributeNameSet(allNames);
            }

            // Filter the attribute names based on the "ignore" and "authored" criteria so that we can add them to
            // the final value attributes list
            for (const TfToken& name : allNames)
            {
                // Skip any attribute covered by the ignore rules.
                if (_isMatchingName(name, m_ignoreAttrNames, m_ignoreAttrNamespaces))
                {
                    continue;
                }

                m_valueAttributes.insert(name);
            }
        }

        // Add explicit value attribute names to the final value attributes list if they are not covered by the
        // ignores.
        for (const auto& name : m_explicitValueAttributes)
        {
            if (!_isMatchingName(name, m_ignoreAttrNames, m_ignoreAttrNamespaces))
            {
                m_valueAttributes.insert(name);
            }
        }

        // Ignore attributes should not be in the authored attribute sets.
        for (const auto& name : m_ignoreAttrNames)
        {
            m_authoredAttributes.erase(name);
        }

        // Authored attributes should not be in the value attribute set.
        for (const auto& name : m_authoredAttributes)
        {
            m_valueAttributes.erase(name);
        }
    }

    void HashInfo(PreBucket& preBucket)
    {
        // get applied schemas from the VirtualMesh
        TfTokenVector appliedSchemas;
        preBucket.virtualMesh.getAppliedSchemas(appliedSchemas, false);

        // Determine the hash key for this bucket
        size_t hash = 0;
        hash = TfHash::Combine(hash, VtHashValue(appliedSchemas));
        hash = TfHash::Combine(hash, VtHashValue(preBucket.attributes));
        hash = TfHash::Combine(hash, VtHashValue(preBucket.authoredAttributes));
        hash = TfHash::Combine(hash, VtHashValue(preBucket.boundMaterialPath));
        hash = TfHash::Combine(hash, VtHashValue(preBucket.parentPath));

        // Store the hash (which makes this bucket valid).
        preBucket.hash = hash;
    }

    void PopulatePreBuckets(std::vector<PreBucket>& preBuckets)
    {
        size_t count = m_inputData.size();

        // Thread-safe caches
        UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
        UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

        auto collectFn = [&](tbb::blocked_range<size_t> r)
        {
            // Iterate the VirtualMeshes covered by this range passed to this thread.
            for (size_t i = r.begin(); i < r.end(); ++i)
            {
                // Add the prim to the PreBucket struct so that it is available during info collection. Also store the
                // parentPath, which is part of the hash.
                PreBucket& preBucket = preBuckets[i];
                VirtualMesh& mesh = preBucket.virtualMesh;
                const SdfPath& parentPath = this->m_inputData[i].first;
                preBucket.parentPath = parentPath;

                // Omit meshes that are invalid, inactive as the have no composed state.
                if (!mesh.isValid() || !mesh.isActive())
                {
                    continue;
                }

                // validate the VirtualMeshes and compute the extent of the geometry
                if (m_collectMeshInfo)
                {
                    if (!mesh.validateAndComputeExtent())
                    {
                        if (m_report)
                        {
                            std::string msg = mesh.getSourcePath().GetAsString() + ": failed to validate mesh info";
                            m_report->log(LogLevel::eInfo, s_reportCategoryBucket, msg);
                        }
                        continue;
                    }
                }

                // Determine the common ancestor for the input and output prim.
                // This is used for computing inherited attribute values.
                const SdfPath& commonPrefix = mesh.getSourcePath().GetCommonPrefix(parentPath);

                // Populate info with attributes where the value should be considered in
                // the hash.
                _collectValueAttrInfo(preBucket, m_valueAttributes, commonPrefix);

                // Populate info with attributes where the existence should be considered
                // in the hash.
                _collectAuthoredAttrInfo(preBucket, m_authoredAttributes);

                // Populate info with material bindings.
                if (m_considerMaterials)
                {
                    _collectMaterialBindingInfo(preBucket, bindingsCache, collQueryCache);
                }

                // Hash the pre-bucket
                HashInfo(preBucket);
            }
        };

        if (m_context->singleThreaded)
        {
            collectFn(tbb::blocked_range<size_t>(0, count));
        }
        else
        {
            // 16 threads seems to give a reasonable result
            tbb::task_arena limitedArena(16);
            tbb::task_group taskGroup;

            limitedArena.execute(
                [&] { taskGroup.run([&] { tbb::parallel_for(tbb::blocked_range<size_t>(0, count), collectFn); }); });

            // Wait for task group
            limitedArena.execute([&taskGroup] { taskGroup.wait(); });
        }
    }

    void SpatiallyClusterPrims(std::vector<PreBucket>& preBuckets)
    {
        if (preBuckets.empty())
        {
            return; // LCOV_EXCL_LINE
        }

        if (m_report)
        {
            std::ostringstream oss;
            oss << "Spatially clustering buckets using threshold=" << m_spatialThreshold
                << ", maxSize=" << m_spatialMaxSize;
            m_report->log(LogLevel::eInfo, s_reportCategoryBucket, oss.str());
        }

        TfStopwatch totalTimer;
        totalTimer.Start();

        // This is unfortunate. Heavily instanced scenes will often have a large number of prototypes
        // that are all at origin. We definitely do not want to build a combined BVH with this data,
        // it would end up finding huge numbers of neighbors that can never be bucketed together (due
        // to the fact we consider parentPath as part of bucketing) and causes massive performance loss
        // in some cases.
        //
        // It's much more efficient to build a BVH per parentPath and only cluster within those. This
        // provides a significant optimization in certain cases of heavily instanced scenes. We do
        // suffer a small hit having to map/copy this data, but generally it seems to be outweighed
        // by the gains of the smaller BVH.
        std::map<SdfPath, std::vector<PreBucket*>> bucketsByPath;
        for (auto& preBucket : preBuckets)
        {
            bucketsByPath[preBucket.parentPath].push_back(&preBucket);
        }

        // Declare a couple of vectors that we can reuse in an attempt to
        // minimize memory allocations.
        std::vector<MeshNodePtr> meshes;
        std::vector<int> clusters;

        for (const auto& it : bucketsByPath)
        {
            // First prepare a vector of MeshNodes. They are just little structs that hold
            // a prim and cached bounds the BVH needs
            meshes.clear();
            meshes.reserve(it.second.size());

            for (const auto& preBucket : it.second)
            {
                MeshNodePtr& node = meshes.emplace_back(std::make_shared<MeshNode>());

                // Skip meshes that are already invalid
                if (!preBucket->hash)
                {
                    continue;
                }

                GfRange3d range;
                range.ExtendBy(preBucket->virtualMesh.getWorldExtent()[0]);
                range.ExtendBy(preBucket->virtualMesh.getWorldExtent()[1]);

                // Convert the world extent from pre-bucketing to a bbox and cache the mid-point.
                node->bound = GfBBox3d(range);
                node->centroid = range.GetMidpoint();
                node->nvertex = preBucket->virtualMesh.getFaceVertexIndices().size();
            }

            // Reset the output cluster id vector then go for it.
            clusters.clear();
            clusters.resize(meshes.size(), INVALID_CLUSTER);
            spatiallyClusterMeshes(m_spatialMode, meshes, m_spatialThreshold, m_spatialMaxSize, clusters);

            // Now that the meshes have been clustered we can go ahead and apply the cluster id to each
            // of the pre buckets. For any meshes that were not part of a cluster we will invalidate them
            // so that they do not merge at all. Anything else will get its hash updated to factor in the
            // unique cluster id.
            for (size_t index = 0; index < it.second.size(); ++index)
            {
                auto& preBucket = it.second[index];

                preBucket->spatialCluster = clusters[index];

                // If there was no cluster index (ie this mesh was not part of a cluster) then zero out the
                // hash, invalidating the prim from merging altogether.
                if (clusters[index] == INVALID_CLUSTER)
                {
                    preBucket->hash = 0;
                }
                else
                {
                    // Combine the spatial cluster id in to the hash.
                    preBucket->hash = TfHash::Combine(preBucket->hash, clusters[index]);
                }
            }
        }

        totalTimer.Stop();

        std::ostringstream oss;
        oss << "Clustered meshes in " << totalTimer.GetMilliseconds() << "ms";

        if (m_report)
        {
            m_report->log(LogLevel::eInfo, s_reportCategoryBucket, oss.str());
        }
        else
        {
            SO_LOG_INFO("Merge: %s", oss.str().c_str());
        }
    }

    void CreateVirtualMeshBuckets(std::vector<PreBucket>& preBuckets, const UsdStageWeakPtr& stage)
    {
        // Store buckets in a vector so that the result can be ordered, but track the index of each bucket within that
        // vector (based on the hash key of the bucket data) so we can add similar prims to the same bucket. Each key
        // has a list of indices so we can start new buckets with the same key if we decide sufficient prims have been
        // added.
        BucketLookup bucketIndexLookup;

        // Before the bucketing happens do a quick count of hashes. This lets us skip creating buckets that would only
        // contain one mesh - which would result in "merging" one mesh. This is gated by a configurable option.
        std::map<size_t, int> hashCount;
        if (!m_allowSingleMeshes)
        {
            for (const auto& preBucket : preBuckets)
            {
                if (preBucket.hash)
                {
                    ++hashCount[preBucket.hash];
                }
            }
        }

        UsdGeomXformCache xformCache;
        size_t count = preBuckets.size();
        for (size_t i = 0; i < count; ++i)
        {
            auto& preBucket = preBuckets[i];

            // Skip meshes that were not valid to merge
            if (!preBucket.hash)
            {
                continue;
            }

            // Unless enabled, skip pre-buckets that contain only one mesh.
            if (!m_allowSingleMeshes && hashCount[preBucket.hash] < 2)
            {
                continue;
            }

            // To avoid creating single merged meshes that exceed data size limits we track the data volume as we add
            // prims to a bucket and start a new bucket if we would otherwise exceed the defined max data volume. The
            // current max is based on data volume representing the vertex count and the limit being 10 million
            // vertices.
            size_t dataVolume = preBucket.virtualMesh.getPoints().size();

            // Create a new bucket and add it to the list of bucket indices for this hash if either, the hash was not
            // already in the lookup table (meaning there will be no existing bucket), or if adding this prim to the
            // current bucket would exceed the max data volume
            BucketLookup::iterator iter = bucketIndexLookup.find(preBucket.hash);

            // Given we expect more hits than misses, take the hit on a double find in order to avoid creating
            // vectors repeatedly.
            if (iter == bucketIndexLookup.end())
            {
                iter = bucketIndexLookup.insert(std::make_pair(preBucket.hash, std::vector<size_t>())).first;
            }

            // Either no bucket, or this bucket would exceed the max size. Create and insert a new one.
            if (iter->second.empty() ||
                (!m_ignoreDataVolume &&
                 (m_outputData[iter->second.back()].getSupersetDataVolume() + dataVolume) > BUCKET_MAX_DATA_VOLUME))
            {
                // Create new index
                iter->second.emplace_back(m_outputData.size());

                // get applied schemas
                TfTokenVector appliedSchemas;
                preBucket.virtualMesh.getAppliedSchemas(appliedSchemas, true);

                // create the VirtualMesh, set the parent path, and add to the output
                VirtualMesh bucketMesh(preBucket.parentPath,
                                       m_outputName,
                                       appliedSchemas,
                                       stage,
                                       xformCache,
                                       preBucket.spatialCluster);
                m_outputData.push_back(std::move(bucketMesh));
            }

            // Add the VirutalMesh as a subset child
            m_outputData[iter->second.back()].addSupersetChild(preBucket.virtualMesh);
        }
    }


    void ProcessReport(const std::vector<PreBucket>& preBuckets) const
    {
        std::string suffix = m_outputData.size() == 1 ? "" : "s";
        m_report->log(LogLevel::eInfo,
                      s_reportCategoryBucket,
                      "Calculated " + std::to_string(m_outputData.size()) + " bucket" + suffix);

        // Output detailed info about what composed the hash for reporting and debugging.
        std::set<size_t> seen;

        constexpr const char* sAuthored = "Authored";
        constexpr const char* sNone = "None";

        // Dump out a text representation of the data for each hash. This is very verbose, but intended to help
        // debug why certain things do/don't get merged together.
        for (const auto& preBucket : preBuckets)
        {
            if (!preBucket.hash)
            {
                continue;
            }

            // Log the hash for each prim, prior to the unique hash check.
            std::string out =
                "Hashed " + preBucket.virtualMesh.getSourcePath().GetAsString() + " = " + std::to_string(preBucket.hash);
            m_report->log(LogLevel::eInfo, s_reportCategoryBucket, out);

            // Only need to include each unique hash once
            auto insertIt = seen.insert(preBucket.hash);
            if (!insertIt.second)
            {
                continue;
            }

            std::ostringstream oss;

            oss << "Hash: " << preBucket.hash << "\n";
            TfTokenVector appliedSchemas;
            preBucket.virtualMesh.getAppliedSchemas(appliedSchemas, false);
            for (const auto& schema : appliedSchemas)
            {
                oss << "Schema: " << schema << "\n";
            }

            for (const auto& attribute : preBucket.attributes)
            {
                oss << "Attr: " << attribute.first << "=" << std::boolalpha << attribute.second << std::noboolalpha
                    << "\n";
            }

            for (const auto& attribute : preBucket.authoredAttributes)
            {
                oss << "Authored Attr: " << attribute.first << "=" << (attribute.second.Get<bool>() ? sAuthored : sNone)
                    << "\n";
            }

            oss << "Bound Material: " << preBucket.boundMaterialPath.GetAsString() << "\n";
            oss << "Parent Path: " << preBucket.parentPath.GetAsString() << "\n";

            if (m_spatialMode != ClusterMode::eNone)
            {
                oss << "Spatial Cluster: " << preBucket.spatialCluster;
            }

            m_report->log(LogLevel::eInfo, s_reportCategoryHashInfo, oss.str(), true);
        }
    }

    void Clear()
    {
        m_inputData.clear();
    }
};


Bucketer::Bucketer(ExecutionContext* context)
    : pImpl(new Impl(context))
{
}


Bucketer::~Bucketer()
{
    delete pImpl;
}


const std::vector<VirtualMesh>& Bucketer::GetOutputData() const
{
    return pImpl->m_outputData;
}


void Bucketer::SetReport(const std::shared_ptr<Report>& report)
{
    pImpl->m_report = report;
}


void Bucketer::SetOutputName(const std::string& outputName)
{
    pImpl->m_outputName = outputName;
}


void Bucketer::SetConsiderMaterials(bool considerMaterials)
{
    pImpl->m_considerMaterials = considerMaterials;
}


void Bucketer::SetConsiderPrimAttributes(bool value)
{
    pImpl->m_considerPrimAttributes = value;
}


void Bucketer::AddValueAttributes(const TfTokenVector& names)
{
    pImpl->m_explicitValueAttributes.insert(names.begin(), names.end());
}


void Bucketer::AddAuthoredAttributes(const TfTokenVector& names)
{
    pImpl->m_authoredAttributes.insert(names.begin(), names.end());
}


void Bucketer::AddIgnoreAttributeNames(const TfTokenVector& names)
{
    pImpl->m_ignoreAttrNames.insert(names.begin(), names.end());
}


void Bucketer::AddIgnoreAttributeNamespaces(const TfTokenVector& namespaces)
{
    pImpl->m_ignoreAttrNamespaces.insert(namespaces.begin(), namespaces.end());
}


void Bucketer::SetAllowSingleMeshes(bool allow)
{
    pImpl->m_allowSingleMeshes = allow;
}


void Bucketer::SetCollectMeshInfo(bool value)
{
    pImpl->m_collectMeshInfo = value;
}


void Bucketer::SetSpatialArgs(ClusterMode mode, double threshold, double maxSize)
{
    pImpl->m_spatialMode = mode;
    pImpl->m_spatialThreshold = threshold;
    pImpl->m_spatialMaxSize = maxSize;
}


void Bucketer::setIgnoreDataVolume(bool state)
{
    pImpl->m_ignoreDataVolume = state;
}


void Bucketer::AddVirtualMeshes(const std::vector<VirtualMesh>& virtualMeshes, const SdfPath& parentPath)
{
    pImpl->AddVirtualMeshes(virtualMeshes, parentPath);
}


void Bucketer::Bucket(const UsdStageWeakPtr& stage)
{
    pImpl->Bucket(stage);
}


void _populateMergeBucketerAttributes(const BucketerPtr& bucketer, bool considerAllAttributes)
{
    // Value Attributes.
    // Add attributes covered by the UsdGeomMesh schema and inherited schemas.
    bucketer->AddValueAttributes(UsdGeomMesh::GetSchemaAttributeNames());
    bucketer->SetConsiderPrimAttributes(considerAllAttributes);

    // Add Nvidia specific attributes that are not part of published schemas.
    bucketer->AddValueAttributes({ _tokens->primvarsDoNotCastShadows });

    // Authored Attributes.
    // Add attributes that we can merge the values of but have no logical default, so the presence (but not value) of an
    // authored attribute needs to be considered in the bucket logic.
    bucketer->AddAuthoredAttributes({ _tokens->primvarsSt,
                                      _tokens->primvarsSt0,
                                      _tokens->primvarsSt1,
                                      _tokens->primvarsSt2,
                                      _tokens->primvarsNormals,
                                      UsdGeomTokens->primvarsDisplayColor,
                                      UsdGeomTokens->primvarsDisplayOpacity });

    // Ignore Attributes.
    // Add attributes that are explicitly handled during merge.
    bucketer->AddIgnoreAttributeNames({ UsdGeomTokens->cornerIndices,
                                        UsdGeomTokens->cornerSharpnesses,
                                        UsdGeomTokens->creaseIndices,
                                        UsdGeomTokens->creaseLengths,
                                        UsdGeomTokens->creaseSharpnesses,
                                        UsdGeomTokens->extent,
                                        UsdGeomTokens->faceVertexIndices,
                                        UsdGeomTokens->holeIndices,
                                        UsdGeomTokens->points,
                                        UsdGeomTokens->faceVertexCounts });

    // Ignore the primvar associated meta attributes that are handled by merge.
    bucketer->AddIgnoreAttributeNames({ _tokens->primvarsStIndices,
                                        _tokens->primvarsSt0Indices,
                                        _tokens->primvarsSt1Indices,
                                        _tokens->primvarsSt2Indices,
                                        _tokens->primvarsNormalsIndices,
                                        _tokens->primvarsDisplayColorIndices,
                                        _tokens->primvarsDisplayOpacityIndices });

    // Ignore all xform related attributes as these are handled by merge.
    bucketer->AddIgnoreAttributeNames({ UsdGeomTokens->xformOpOrder });
    bucketer->AddIgnoreAttributeNamespaces({ _tokens->xformOpNamespace });

    // Ignore all material binding attributes as these are handled by merge.
    bucketer->AddIgnoreAttributeNames({ UsdShadeTokens->materialBinding });
    bucketer->AddIgnoreAttributeNamespaces({ _tokens->subsetFamilyNamespace });

    // Ignore attributes from UsdPhysics that are evaluated, as they will have unique values but can be regenerated.
    bucketer->AddIgnoreAttributeNames({ UsdPhysicsTokens->physicsAngularVelocity, UsdPhysicsTokens->physicsVelocity });

    // Add attributes unhandled by merge at this point but should be skipped in bucketing.
    bucketer->AddIgnoreAttributeNames({ UsdGeomTokens->velocities, UsdGeomTokens->accelerations });

    // Add "normals" to the ignore list because we bucket based on "primvars:normals" being an authored attribute and we
    // don't want to also consider "normals".
    bucketer->AddIgnoreAttributeNames({ UsdGeomTokens->normals, _tokens->primvarsNormalsIndices });
}


} // namespace omni::scene::optimizer
