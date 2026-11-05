// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Include core types before USD headers for the VS2019 debug oneTBB initialization order.
#include <usd_optimize/core/geometry/VirtualMesh.h>

// Test utilities
#include "../TestUtils.h"

// USD
#include <pxr/base/tf/staticTokens.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

// doctest
#include <doctest/doctest.h>

using namespace usd_optimize;
PXR_NAMESPACE_USING_DIRECTIVE

namespace
{

// clang-format off
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (rest)
    (tangents)
    (surfaceNormal)
    (displayColor)
    (customTriple)
);
// clang-format on

/// Check the authored type and flattened values without depending on output indexing choices.
void _checkPrimvar(const UsdPrim& prim, const TfToken& name, const SdfValueTypeName& type, const VtVec3fArray& expected)
{
    CAPTURE(name);
    const auto primvar = UsdGeomPrimvarsAPI(prim).GetPrimvar(name);
    REQUIRE(primvar);
    CHECK(primvar.GetTypeName() == type);
    VtVec3fArray values;
    REQUIRE(primvar.ComputeFlattened(&values));
    REQUIRE_EQ(values.size(), expected.size());
    for (size_t index = 0; index < values.size(); ++index)
    {
        CHECK(GfIsClose(values[index], expected[index], 1e-5));
    }
}

} // namespace

TEST_CASE("VirtualMesh transforms spatial primvars in the destination frame")
{
    // Spatial roles retain their meaning after merging, while nonspatial values and source data remain unchanged.
    const auto stage = testutils::_openStage("mergeSpatialPrimvars.usda");
    REQUIRE(stage);
    UsdGeomXformCache xforms;
    UsdShadeMaterialBindingAPI::BindingsCache bindings;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collections;
    std::vector<VirtualMesh> sources;
    for (const auto& path : { SdfPath("/World/Original"), SdfPath("/World/Transformed") })
    {
        const auto prim = stage->GetPrimAtPath(path);
        REQUIRE(prim);
        sources.emplace_back(prim, xforms, bindings, collections);
    }
    VirtualMesh merged(SdfPath("/World"), "Merged", {}, stage, xforms);
    for (auto& source : sources)
    {
        merged.addSupersetChild(source);
    }

    REQUIRE(merged.validateAndComputeExtent());
    auto layer = stage->GetRootLayer();
    merged.createInLayer(stage, layer);

    const auto output = stage->GetPrimAtPath(SdfPath("/World/Merged"));
    REQUIRE(output);
    _checkPrimvar(
        output,
        _tokens->rest,
        SdfValueTypeNames->Point3fArray,
        { GfVec3f(1, 1, 0), GfVec3f(0, 0, 0), GfVec3f(1, 0, 0), GfVec3f(9, 2, 0), GfVec3f(10, 0, 0), GfVec3f(10, 2, 0) });
    _checkPrimvar(
        output,
        _tokens->tangents,
        SdfValueTypeNames->Vector3fArray,
        { GfVec3f(1, 1, 0), GfVec3f(1, 0, 0), GfVec3f(1, 1, 0), GfVec3f(-1, 2, 0), GfVec3f(0, 2, 0), GfVec3f(-1, 2, 0) });
    const GfVec3f originalNormal(0.70710677f, 0.70710677f, 0);
    const GfVec3f transformedNormal(-0.8944272f, 0.4472136f, 0);
    _checkPrimvar(output, _tokens->surfaceNormal, SdfValueTypeNames->Normal3fArray, { originalNormal, transformedNormal });
    CHECK(UsdGeomPrimvarsAPI(output).GetPrimvar(_tokens->surfaceNormal).GetInterpolation() == UsdGeomTokens->uniform);
    _checkPrimvar(
        output,
        UsdGeomTokens->normals,
        SdfValueTypeNames->Normal3fArray,
        { originalNormal, originalNormal, originalNormal, transformedNormal, transformedNormal, transformedNormal });
    _checkPrimvar(output, _tokens->displayColor, SdfValueTypeNames->Color3fArray, { GfVec3f(0.2f, 0.4f, 0.6f) });
    _checkPrimvar(output, _tokens->customTriple, SdfValueTypeNames->Float3Array, { GfVec3f(1, 1, 0) });
    CHECK(UsdGeomPrimvarsAPI(output).GetPrimvar(_tokens->displayColor).GetInterpolation() == UsdGeomTokens->constant);
    CHECK(UsdGeomPrimvarsAPI(output).GetPrimvar(_tokens->customTriple).GetInterpolation() == UsdGeomTokens->constant);

    for (const auto& source : sources)
    {
        for (const auto& primvar : UsdGeomPrimvarsAPI(source.getPrim()).GetPrimvarsWithValues())
        {
            CAPTURE(primvar.GetAttr().GetPath());
            VtValue original, retained;
            REQUIRE(primvar.Get(&original));
            REQUIRE(source.getValue(primvar.GetAttr().GetName(), retained));
            CHECK(retained == original);
        }
    }
}
