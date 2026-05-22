// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "GenerateScene.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>

// USD
#include <pxr/base/gf/rotation.h>

// TBB
#include <tbb/parallel_for.h>

PXR_NAMESPACE_USING_DIRECTIVE


SO_PLUGIN_INIT(omni::scene::optimizer::GenerateSceneOperation);


namespace omni::scene::optimizer
{


// Constants
constexpr const char* s_category = "GENERATE_SCENE";
constexpr size_t CLUSTER_MAX_VERTICES = 10000000; // maximum number of vertices per cluster


// Abstract base class that represents a function that can be used to displace mesh geometry
class DisplacementFunction
{
public:
    DisplacementFunction(const GfVec3f& center, float period, float power, float falloff)
        : m_center(center)
        , m_period(period)
        , m_power(power)
        , m_falloff(falloff)
    {
    }

    virtual ~DisplacementFunction()
    {
    }

    virtual GfVec3f apply(const GfVec3f& dir) = 0;

protected:
    GfVec3f m_center;
    float m_period;
    float m_power;
    float m_falloff;
};
typedef std::unique_ptr<DisplacementFunction> DisplacementFunctionUPtr;


// Displaces mesh geometry randomly
class RandomDisplacementFunction : public DisplacementFunction
{
public:
    RandomDisplacementFunction(const GfVec3f& center, float period, float power, float falloff, int seed)
        : DisplacementFunction(center, period, power, falloff)
        , m_dist(0.0, 1.0)
    {
        m_random.seed(seed);
    }

    GfVec3f apply(const GfVec3f& dir) override
    {
        float distance = (dir - m_center).GetLength();
        float power = m_power * (1.0 - (distance / m_falloff));
        if (power > 0.0)
        {
            return dir * m_dist(m_random) * power;
        }
        return GfVec3f(0.0f);
    }

private:
    std::mt19937 m_random;
    std::uniform_real_distribution<float> m_dist;
};


// Displaces mesh geometry using a sine wave
class SinDisplacementFunction : public DisplacementFunction
{
public:
    SinDisplacementFunction(const GfVec3f& center, float period, float power, float falloff)
        : DisplacementFunction(center, period, power, falloff)
    {
    }

    GfVec3f apply(const GfVec3f& dir) override
    {
        float distance = (dir - m_center).GetLength();
        float power = m_power * (1.0 - (distance / m_falloff));
        if (power > 0.0)
        {
            return dir * sin(distance * m_period) * power;
        }
        return GfVec3f(0.0f);
    }
};


/// Simple structure that holds pre-generated random data used to create a random mesh
struct RandomMeshData
{
    size_t referenceMeshIndex = 0; // the index of the reference mesh to use
    GfVec3d translation = GfVec3d(0.0);
    GfRotation rotation;
    std::vector<DisplacementFunctionUPtr> displacementFunctions;
    float scale = 1.0;
};


// discovers reference meshes under the given prim
static void _discoverReferenceMeshes(const UsdPrim& prim,
                                     UsdGeomXformCache& xformCache,
                                     UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                                     UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache,
                                     std::vector<VirtualMesh>& refMeshes)
{
    // invalid prim? - stop processing
    if (!prim.IsValid())
    {
        return;
    }

    // is this a mesh? then add it to the reference meshes and stop processing
    if (UsdGeomMesh mesh = UsdGeomMesh(prim))
    {
        refMeshes.emplace_back(prim, xformCache, bindingsCache, collQueryCache);
        return;
    }

    // otherwise recurse to child prims looking for meshes
    for (const UsdPrim& child : prim.GetChildren())
    {
        _discoverReferenceMeshes(child, xformCache, bindingsCache, collQueryCache, refMeshes);
    }
}


// discovers materials under a given prim
static void _discoverMaterials(const UsdPrim& prim, std::vector<SdfPath>& materialPaths)
{
    // invalid prim? - stop processing
    if (!prim.IsValid())
    {
        return;
    }

    // is this a material? then add it to the material paths and stop processing
    if (UsdShadeMaterial material = UsdShadeMaterial(prim))
    {
        materialPaths.push_back(prim.GetPath());
        return;
    }

    // otherwise recurse to child prims looking for materials
    for (const UsdPrim& child : prim.GetChildren())
    {
        _discoverMaterials(child, materialPaths);
    }
}


// assigns a random material from the given material paths to the mesh
static void _assignRandomMaterial(VirtualMesh& mesh, const std::vector<SdfPath>& materialPaths, std::mt19937& random)
{
    // no materials to assign?
    if (materialPaths.empty())
    {
        return;
    }

    // select a random material and bind it
    std::uniform_int_distribution<size_t> dist(0, materialPaths.size() - 1);
    mesh.bindMaterial(materialPaths[dist(random)]);
}


GenerateSceneOperation::GenerateSceneOperation()
    : Operation("generateScene",
                "Generate Scene",
                "Generates a USD stage to use for benchmarking and testing using reference prims from the incoming stage.")
{
    addArgument("seed", "Seed", kDisplayTypeInt, "The seed which will be used when generating random numbers", m_seed);

    addArgument(
        "referenceMeshPaths",
        "Reference Mesh Paths",
        kDisplayTypePrimPaths,
        "Prim paths of meshes to use as reference meshes for generating random geometry. If no paths are provided "
        "random meshes will not be generated. If a path is provided that is not a mesh its children will be searched.",
        m_referenceMeshPaths);

    addArgument("generatedMeshPath",
                "Generate Mesh Path",
                kDisplayTypePrimPath,
                "The path and name to use for newly generated meshes",
                m_generatedMeshPath);

    addArgument("meshCount",
                "Mesh Count",
                kDisplayTypeInt,
                "The number of disjoint meshes that will be generated",
                m_meshCount);

    addArgument(
        "uniformLayout",
        "Uniform Layout",
        kDisplayTypeBool,
        "Whether the generated meshes will follow a uniform layout, if false generated meshes will be randomly spaced",
        m_uniformLayout);

    addArgument(
        "2DLayout",
        "2D Layout",
        kDisplayTypeBool,
        "Whether the generated meshes will be placed in a 2D layout, if false meshes will be arranged in a 3D layout",
        m_2DLayout);

    addArgument("layoutSpacing",
                "Layout Spacing",
                kDisplayTypeFloat,
                "The distance between each generated mesh, this may be uniform or a hint for random layouts.",
                m_layoutSpacing);

    addArgument("uniqueMeshPercentage",
                "Unique Mesh Percentage",
                kDisplayTypeFloatSlider,
                "The percentage of randomly generated meshes that will have unique geometry.",
                m_uniqueMeshPercent)
        .setMin(0)
        .setMax(1.0);

    addArgument("scaleUniqueMeshes",
                "Scale Unique Meshes",
                kDisplayTypeBool,
                "Whether meshes with unique geometry will have a scale factor applied to their points.",
                m_scaleUniqueMeshes);

    addArgument("clusteredPercent",
                "Clustered Percent",
                kDisplayTypeFloatSlider,
                "The percentage of randomly generated meshes that will be clustered together.",
                m_clusteredPercent)
        .setMin(0)
        .setMax(1.0);

    addArgument("numClusters",
                "Number of Clusters",
                kDisplayTypeInt,
                "The target number of clusters to group random geometry together in. Note: a higher number of clusters may "
                "be generated if the cluster max vertex counts are exceeded.",
                m_numClusters);

    addArgument(
        "materialPaths",
        "Material Paths",
        kDisplayTypePrimPaths,
        "Prim paths of materials to randomly assign to generated geometry. If no paths are provided no materials will "
        "be assigned. If a path is provided that is not a material its children will be searched.",
        m_materialPaths);
}


std::string GenerateSceneOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion GenerateSceneOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string GenerateSceneOperation::getCategory() const
{
    return s_category;
}


bool GenerateSceneOperation::getVisible() const
{
    return false;
}


OperationResult GenerateSceneOperation::executeImpl()
{
    // seed the random number generator
    m_random.seed(m_seed);

    UsdStageWeakPtr stage = getUsdStage();

    // caches
    UsdGeomXformCache xformCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;

    // collect the reference meshes
    std::vector<VirtualMesh> refMeshes;
    for (const std::string& path : m_referenceMeshPaths)
    {
        _discoverReferenceMeshes(stage->GetPrimAtPath(SdfPath(path)), xformCache, bindingsCache, collQueryCache, refMeshes);
    }

    // perform mesh generation
    generateMeshes(refMeshes, xformCache);

    OperationResult result{ true, nullptr, nullptr };
    return result;
}


void GenerateSceneOperation::generateMeshes(std::vector<VirtualMesh>& refMeshes, UsdGeomXformCache& xformCache)
{
    // no work to do?
    if (refMeshes.empty() || m_meshCount <= 0)
    {
        return;
    }

    // get the stage and edit layer
    UsdStageWeakPtr stage = getUsdStage();
    SdfLayerHandle editLayer = stage->GetEditTarget().GetLayer();

    // collect materials to assign
    std::vector<SdfPath> materialPaths;
    for (const std::string& path : m_materialPaths)
    {
        _discoverMaterials(stage->GetPrimAtPath(SdfPath(path)), materialPaths);
    }

    // resolve the parent path and name to use when generating meshes
    SdfPath generatePath(m_generatedMeshPath);
    SdfPath parentPath = generatePath.GetParentPath();
    if (parentPath.IsEmpty())
    {
        parentPath = SdfPath::AbsoluteRootPath();
    }
    else if (!parentPath.IsAbsolutePath())
    {
        parentPath = parentPath.MakeAbsolutePath(SdfPath::AbsoluteRootPath());
    }
    std::string meshName = generatePath.GetName();
    if (meshName.empty())
    {
        meshName = "mesh";
    }

    // calculate layout values
    int layoutSide;
    if (m_2DLayout)
    {
        layoutSide = static_cast<int>(ceil(sqrt(float(m_meshCount))));
    }
    else
    {
        layoutSide = static_cast<int>(ceil(std::cbrt(float(m_meshCount))));
    }
    const double layoutOffset = (static_cast<double>(layoutSide - 1) * m_layoutSpacing) * -0.5;
    const double layoutSize = static_cast<double>(layoutSide) * m_layoutSpacing;

    // construct distributions needed to generate random numbers
    std::uniform_real_distribution<double> realDist(0.0, 1.0); // generic between 0.0 and 1.0 random number
    std::uniform_real_distribution<float> negRealDist(-1.0, 1.0); // generic between -1.0 and 1.0 random number
    std::uniform_int_distribution<size_t> meshDist(0, refMeshes.size() - 1); // selects a random mesh
    std::uniform_real_distribution<double> layoutDist(layoutSize * -0.5, layoutSize * 0.5); // random mesh spacing
    std::uniform_real_distribution<double> rotAngleDist(0.0, 360.0); // random rotation angle
    std::uniform_int_distribution<size_t> displaceCountDist(1, 8); // random number of displacement functions
    std::uniform_int_distribution<size_t> displaceFuncDist(0, 1); // selects a random displacement function
    std::uniform_real_distribution<float> scaleDist(-0.5, 3.0); // random additive scaling factor

    // we need to generate all random numbers in a single threaded loop first to ensure we always get the same
    // values for the same seed
    std::vector<RandomMeshData> meshData;
    meshData.resize(m_meshCount);
    for (size_t i = 0; i < static_cast<size_t>(m_meshCount); ++i)
    {
        RandomMeshData& data = meshData[i];
        data.referenceMeshIndex = meshDist(m_random);

        // compute translation based on arguments
        if (m_uniformLayout)
        {
            data.translation[0] = layoutOffset + (static_cast<double>(i % layoutSide) * m_layoutSpacing);
            data.translation[2] = layoutOffset + (static_cast<double>((i / layoutSide) % layoutSide) * m_layoutSpacing);
            // also need a y translation if this is a 3D layout
            if (!m_2DLayout)
            {
                data.translation[1] =
                    layoutOffset + (static_cast<double>(i / (layoutSide * layoutSide)) * m_layoutSpacing);
            }
        }
        else
        {
            data.translation[0] = layoutDist(m_random);
            data.translation[2] = layoutDist(m_random);
            // also need a y translation if this is a 3D layout
            if (!m_2DLayout)
            {
                data.translation[1] = layoutDist(m_random);
            }
        }

        // generate random rotation
        data.rotation =
            GfRotation(GfVec3d(realDist(m_random), realDist(m_random), realDist(m_random)), rotAngleDist(m_random));

        // determine whether this mesh will have randomly generated unique geometry
        if (static_cast<float>(realDist(m_random)) <= m_uniqueMeshPercent)
        {
            // generate a random number of displacement functions
            for (size_t j = 0; j < displaceCountDist(m_random); ++j)
            {
                // generate random values for displacement functions
                GfVec3f center(negRealDist(m_random), negRealDist(m_random), negRealDist(m_random));
                center.Normalize();
                const float period = 15.0f + (static_cast<float>(realDist(m_random)) * 30.0f);
                const float power = 0.02f + (static_cast<float>(realDist(m_random)) * 0.125f);
                const float falloff = 0.25f + (static_cast<float>(realDist(m_random)) * 4.0f);
                // select a displacement function
                switch (displaceFuncDist(m_random))
                {
                case 0:
                    data.displacementFunctions.emplace_back(new SinDisplacementFunction(center, period, power, falloff));
                    break;
                default:
                    data.displacementFunctions.emplace_back(
                        new RandomDisplacementFunction(center, period, power, falloff, m_seed + static_cast<int>(i) + 1));
                    break;
                }
            }

            // random scale?
            if (m_scaleUniqueMeshes)
            {
                data.scale += scaleDist(m_random);
            }
        }
    }

    // for each reference mesh pre-compute the direction for each point so we don't have to do it for each
    // generated mesh
    std::vector<std::vector<GfVec3f>> refDirs;
    refDirs.resize(refMeshes.size());
    for (size_t i = 0; i < refMeshes.size(); ++i)
    {
        const VirtualMesh& refMesh = refMeshes[i];
        const VtVec3fArray& points = refMesh.getPoints();
        std::vector<GfVec3f>& dirs = refDirs[i];
        dirs.reserve(points.size());
        for (const GfVec3f& point : points)
        {
            dirs.push_back(point.GetNormalized());
        }
    }

    // generate the random meshes in a multi-threaded context
    std::vector<VirtualMesh> genMeshes;
    genMeshes.resize(m_meshCount);
    tbb::parallel_for(
        tbb::blocked_range<size_t>(0, static_cast<size_t>(m_meshCount)),
        [&](tbb::blocked_range<size_t> r)
        {
            for (size_t i = r.begin(); i < r.end(); ++i)
            {
                // get random data and the mesh to generate data for
                RandomMeshData& data = meshData[i];
                VirtualMesh& genMesh = genMeshes[i];

                // randomly select a mesh from the reference meshes
                const VirtualMesh& refMesh = refMeshes[data.referenceMeshIndex];

                // apply displacement functions and scaling to the points
                VtVec3fArray newPoints = refMesh.getPoints();
                if (!data.displacementFunctions.empty())
                {
                    const std::vector<GfVec3f>& dirs = refDirs[data.referenceMeshIndex];
                    for (size_t j = 0; j < newPoints.size(); ++j)
                    {
                        GfVec3f& point = newPoints[j];
                        const GfVec3f& dir = dirs[j];
                        for (const DisplacementFunctionUPtr& func : data.displacementFunctions)
                        {
                            point += func->apply(dir);
                        }

                        // apply scale
                        point *= data.scale;
                    }
                }

                // create a new version of the reference mesh
                genMesh = refMesh.newModifiedCopy(GfMatrix4d(data.rotation, data.translation), std::move(newPoints));
            }
        });

    size_t nameSuffix = 0;

    // perform clustering?
    std::vector<std::vector<VirtualMesh>> clusters;
    std::unordered_set<size_t> clusteredIds;
    if (m_clusteredPercent > 0.0f && m_numClusters > 0)
    {
        // random number distribution to select clusters
        std::uniform_int_distribution<size_t> selectClusterDist(0, m_numClusters - 1);

        // reserve memory
        clusters.resize(m_numClusters);
        clusteredIds.reserve(genMeshes.size());

        // set up cluster virtual meshes
        int clusterId = 0;
        for (size_t i = 0; i < static_cast<size_t>(m_numClusters); ++i)
        {
            clusters[i].emplace_back(parentPath,
                                     meshName + std::to_string(nameSuffix++),
                                     TfTokenVector(),
                                     stage,
                                     xformCache,
                                     clusterId++);
        }

        // add generated virtual meshes to clusters and track the ids of the meshes that have been clustered
        for (VirtualMesh& genMesh : genMeshes)
        {
            // determine if we should cluster this mesh
            if (static_cast<float>(realDist(m_random)) > m_clusteredPercent)
            {
                continue;
            }

            // randomly select a cluster to add the child too
            const size_t clusterIndex = selectClusterDist(m_random);
            VirtualMesh& cluster = clusters[clusterIndex].back();
            // have we hit max cluster vertex count?
            if (cluster.getSupersetDataVolume() + genMesh.getPoints().size() > CLUSTER_MAX_VERTICES)
            {
                clusters[clusterIndex].emplace_back(parentPath,
                                                    meshName + std::to_string(nameSuffix++),
                                                    TfTokenVector(),
                                                    stage,
                                                    xformCache,
                                                    clusterId++);
                cluster = clusters[clusterIndex].back();
            }
            cluster.addSupersetChild(genMesh);
            clusteredIds.insert(genMesh.getId());
        }
    }

    // now create all the generated meshes in a single change block (and assign materials as we go)
    {
        SdfChangeBlock changeBlock;

        // write clustered meshes
        for (std::vector<VirtualMesh>& clusterGroup : clusters)
        {
            for (VirtualMesh& cluster : clusterGroup)
            {
                _assignRandomMaterial(cluster, materialPaths, m_random);
                cluster.createInLayer(stage, editLayer);
            }
        }

        // write unclustered meshes
        for (VirtualMesh& genMesh : genMeshes)
        {
            if (clusteredIds.find(genMesh.getId()) != clusteredIds.end())
            {
                continue;
            }

            _assignRandomMaterial(genMesh, materialPaths, m_random);
            genMesh.setDestinationPath(parentPath.AppendPath(SdfPath(meshName + std::to_string(nameSuffix++))));
            genMesh.createInLayer(stage, editLayer);
        }
    }
}

} // namespace omni::scene::optimizer
