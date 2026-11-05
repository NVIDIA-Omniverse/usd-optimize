// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Usd Optimize Core
// Include this first so VS2019 debug builds initialize oneTBB before the USD mesh schema.
#include <usd_optimize/core/geometry/Bucket.h>

// USD
#include <pxr/usd/usdGeom/mesh.h>

// doctest
#include <doctest/doctest.h>

// Standard library
#include <utility>

using namespace usd_optimize;
PXR_NAMESPACE_USING_DIRECTIVE


namespace
{

const TfToken s_detailLevel("optimize:detailLevel");

// Create two source meshes with the detail levels used by the compatibility examples.
std::vector<VirtualMesh> makeMeshesWithDetailLevels(const UsdStageRefPtr& stage, int firstLevel, int secondLevel)
{
    // Both source meshes share the parent that the tests use as their bucket destination.
    stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));

    // VirtualMesh needs these caches to capture each prim's transform and material bindings.
    UsdGeomXformCache xformCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
    std::vector<VirtualMesh> meshes;
    const std::pair<const char*, int> meshLevels[] = { { "First", firstLevel }, { "Second", secondLevel } };
    for (const auto& [name, detailLevel] : meshLevels)
    {
        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/World").AppendChild(TfToken(name)));
        // Identical, valid triangles let Bucketer process both meshes and isolate the detail-level comparison.
        mesh.CreatePointsAttr().Set(VtVec3fArray{ GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(0, 1, 0) });
        mesh.CreateFaceVertexCountsAttr().Set(VtIntArray{ 3 });
        mesh.CreateFaceVertexIndicesAttr().Set(VtIntArray{ 0, 1, 2 });
        // Author the example property before VirtualMesh captures attributes for Bucketer's raw-value comparison.
        // The provider later reads it from the source prim and maps its value to a tier.
        mesh.GetPrim().CreateAttribute(s_detailLevel, SdfValueTypeNames->Int).Set(detailLevel);
        meshes.emplace_back(mesh.GetPrim(), xformCache, bindingsCache, collQueryCache);
    }
    return meshes;
}

// An operation can treat different authored detail levels as the same compatibility tier.
// Read the source prim without editing the stage because Bucketer may call providers in parallel.
VtDictionary getDetailTierCompatibility(const VirtualMesh& mesh)
{
    int detailLevel = 0;
    UsdAttribute attribute = mesh.getPrim().GetAttribute(s_detailLevel);
    if (attribute)
    {
        attribute.Get(&detailLevel);
    }
    return { { "highDetail", VtValue(detailLevel >= 2) } };
}

// Return no extra restrictions when the operation has no compatibility values to add.
VtDictionary getNoAdditionalCompatibility(const VirtualMesh&)
{
    return {};
}

} // namespace


TEST_CASE("Bucketer separates different compatibility values")
{
    // With raw levels ignored, only the provider's different detail tiers keep these meshes separate.
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    std::vector<VirtualMesh> meshes = makeMeshesWithDetailLevels(stage, 1, 2);
    ExecutionContext context;

    Bucketer withoutProvider(&context);
    withoutProvider.SetAllowSingleMeshes(true);
    withoutProvider.SetConsiderPrimAttributes(true);
    withoutProvider.AddIgnoreAttributeNames({ s_detailLevel });
    withoutProvider.AddVirtualMeshes(meshes, SdfPath("/World"));
    withoutProvider.Bucket(stage);
    REQUIRE_EQ(withoutProvider.GetOutputData().size(), 1);
    CHECK_EQ(withoutProvider.GetOutputData()[0].getSupersetChildren().size(), 2);

    Bucketer withProvider(&context);
    withProvider.SetAllowSingleMeshes(true);
    withProvider.SetConsiderPrimAttributes(true);
    withProvider.AddIgnoreAttributeNames({ s_detailLevel });
    withProvider.SetCompatibilityProvider(getDetailTierCompatibility);

    withProvider.AddVirtualMeshes(meshes, SdfPath("/World"));
    withProvider.Bucket(stage);

    REQUIRE_EQ(withProvider.GetOutputData().size(), 2);
    CHECK_EQ(withProvider.GetOutputData()[0].getSupersetChildren().size(), 1);
    CHECK_EQ(withProvider.GetOutputData()[1].getSupersetChildren().size(), 1);
}


TEST_CASE("Bucketer groups resolved values only when raw properties are ignored")
{
    // Detail levels 2 and 3 share a tier but share a bucket only when their raw values are ignored.
    UsdStageRefPtr stage = UsdStage::CreateInMemory();
    std::vector<VirtualMesh> meshes = makeMeshesWithDetailLevels(stage, 2, 3);
    for (bool ignoreRawDetailLevel : { false, true })
    {
        ExecutionContext context;
        context.singleThreaded = 1;
        Bucketer bucketer(&context);
        bucketer.SetAllowSingleMeshes(true);
        bucketer.SetConsiderPrimAttributes(true);
        bucketer.SetCompatibilityProvider(getDetailTierCompatibility);
        if (ignoreRawDetailLevel)
        {
            bucketer.AddIgnoreAttributeNames({ s_detailLevel });
        }

        bucketer.AddVirtualMeshes(meshes, SdfPath("/World"));
        bucketer.Bucket(stage);

        const auto& buckets = bucketer.GetOutputData();
        if (ignoreRawDetailLevel)
        {
            REQUIRE_EQ(buckets.size(), 1);
            CHECK_EQ(buckets[0].getSupersetChildren().size(), 2);
        }
        else
        {
            REQUIRE_EQ(buckets.size(), 2);
            CHECK_EQ(buckets[0].getSupersetChildren().size(), 1);
            CHECK_EQ(buckets[1].getSupersetChildren().size(), 1);
        }
    }
}


TEST_CASE("Bucketer keeps normal grouping without compatibility values")
{
    // An unset provider or an empty result leaves normal grouping unchanged.
    for (bool setEmptyProvider : { false, true })
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        std::vector<VirtualMesh> meshes = makeMeshesWithDetailLevels(stage, 2, 2);
        ExecutionContext context;
        Bucketer bucketer(&context);
        bucketer.SetConsiderPrimAttributes(true);
        if (setEmptyProvider)
        {
            bucketer.SetCompatibilityProvider(getNoAdditionalCompatibility);
        }

        bucketer.AddVirtualMeshes(meshes, SdfPath("/World"));
        bucketer.Bucket(stage);

        REQUIRE_EQ(bucketer.GetOutputData().size(), 1);
        CHECK_EQ(bucketer.GetOutputData()[0].getSupersetChildren().size(), 2);
    }
}
