// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "../TestUtils.h"

#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>

#include <doctest/doctest.h>
#include <stdexcept>
#include <usd_optimize/core/geometry/MeshPreparation.h>

using namespace usd_optimize;
PXR_NAMESPACE_USING_DIRECTIVE


TEST_CASE("Mesh preparation fills missing primvars only when allowed")
{
    // A missing primvar stays absent until automatic defaults are enabled.
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    const UsdGeomMesh first = UsdGeomMesh::Define(stage, SdfPath("/First"));
    const UsdGeomMesh second = UsdGeomMesh::Define(stage, SdfPath("/Second"));
    UsdGeomPrimvarsAPI(first)
        .CreatePrimvar(TfToken("weight"), SdfValueTypeNames->FloatArray, UsdGeomTokens->constant)
        .Set(VtFloatArray{ 2.0f });
    const UsdGeomPrimvar missingWeight = UsdGeomPrimvarsAPI(second).GetPrimvar(TfToken("weight"));
    ExecutionContext context = testutils::_getContext(stage);
    const std::vector<UsdPrim> meshes{ first.GetPrim(), second.GetPrim() };

    const MeshPreparationResult unchanged = prepareMeshes(context, "test group", meshes, MeshPreparationOptions{});
    CHECK_EQ(unchanged.filled, 0);
    CHECK_EQ(unchanged.schemaFixed, 0);
    CHECK_EQ(unchanged.notReconciled, 1);
    CHECK_FALSE(missingWeight.HasAuthoredValue());

    MeshPreparationOptions options;
    options.useDefaultPrimvarValues = true;
    const MeshPreparationResult prepared = prepareMeshes(context, "test group", meshes, options);
    CHECK_EQ(prepared.filled, 1);
    CHECK_EQ(prepared.notReconciled, 0);
    CHECK(UsdGeomPrimvarsAPI(second).GetPrimvar(TfToken("weight")).HasAuthoredValue());

    usd_optimize_execution_context_free(&context);
}


TEST_CASE("Mesh preparation reports doubleSided differences when changes are disabled")
{
    // A mixed group is reported while doubleSided changes are disabled, then reconciled when enabled.
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    const UsdGeomMesh first = UsdGeomMesh::Define(stage, SdfPath("/First"));
    const UsdGeomMesh second = UsdGeomMesh::Define(stage, SdfPath("/Second"));
    first.CreateDoubleSidedAttr().Set(true);
    ExecutionContext context = testutils::_getContext(stage);
    const std::vector<UsdPrim> meshes{ first.GetPrim(), second.GetPrim() };

    const MeshPreparationResult unchanged = prepareMeshes(context, "test group", meshes, MeshPreparationOptions{});
    CHECK_EQ(unchanged.notReconciled, 1);
    CHECK_EQ(unchanged.schemaFixed, 0);
    CHECK_FALSE(second.GetDoubleSidedAttr().HasAuthoredValue());

    MeshPreparationOptions options;
    options.makeDoubleSided = true;
    const MeshPreparationResult prepared = prepareMeshes(context, "test group", meshes, options);
    CHECK_EQ(prepared.notReconciled, 0);
    CHECK_EQ(prepared.schemaFixed, 1);
    bool doubleSided = false;
    second.GetDoubleSidedAttr().Get(&doubleSided);
    CHECK(doubleSided);

    usd_optimize_execution_context_free(&context);
}


TEST_CASE("Mesh preparation uses supplied primvar defaults without enabling automatic values")
{
    // A supplied default fills its named primvar while other missing primvars remain absent.
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    const UsdGeomMesh first = UsdGeomMesh::Define(stage, SdfPath("/First"));
    const UsdGeomMesh second = UsdGeomMesh::Define(stage, SdfPath("/Second"));
    const UsdGeomPrimvarsAPI firstPrimvars(first);
    firstPrimvars.CreatePrimvar(TfToken("weight"), SdfValueTypeNames->FloatArray, UsdGeomTokens->constant)
        .Set(VtFloatArray{ 2.0f });
    firstPrimvars.CreatePrimvar(TfToken("tint"), SdfValueTypeNames->FloatArray, UsdGeomTokens->constant)
        .Set(VtFloatArray{ 0.5f });
    ExecutionContext context = testutils::_getContext(stage);
    MeshPreparationOptions options;
    options.primvarDefaults.emplace(TfToken("weight"), "7");

    const MeshPreparationResult prepared =
        prepareMeshes(context, "test group", { first.GetPrim(), second.GetPrim() }, options);

    VtFloatArray weight;
    REQUIRE(UsdGeomPrimvarsAPI(second).GetPrimvar(TfToken("weight")).Get(&weight));
    REQUIRE_EQ(weight.size(), 1);
    CHECK_EQ(weight[0], 7.0f);
    CHECK_FALSE(UsdGeomPrimvarsAPI(second).GetPrimvar(TfToken("tint")).HasAuthoredValue());
    CHECK_EQ(prepared.filled, 1);
    CHECK_EQ(prepared.notReconciled, 1);

    usd_optimize_execution_context_free(&context);
}


TEST_CASE("Mesh preparation rejects meshes from another stage before editing")
{
    // A mixed-stage group is rejected before any doubleSided value changes.
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    const UsdStageRefPtr otherStage = UsdStage::CreateInMemory();
    const UsdGeomMesh first = UsdGeomMesh::Define(stage, SdfPath("/First"));
    const UsdGeomMesh second = UsdGeomMesh::Define(otherStage, SdfPath("/Second"));
    first.CreateDoubleSidedAttr().Set(true);
    ExecutionContext context = testutils::_getContext(stage);
    MeshPreparationOptions options;
    options.makeDoubleSided = true;

    CHECK_THROWS_WITH_AS(prepareMeshes(context, "test group", { first.GetPrim(), second.GetPrim() }, options),
                         "Cannot prepare mesh /Second: it belongs to another USD stage",
                         std::invalid_argument);
    CHECK_FALSE(second.GetDoubleSidedAttr().HasAuthoredValue());

    usd_optimize_execution_context_free(&context);
}


TEST_CASE("Mesh preparation rejects a missing context stage and non-mesh prims")
{
    // Preparation reports the invalid public argument before changing the stage.
    const UsdStageRefPtr stage = UsdStage::CreateInMemory();
    const UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/Mesh"));
    const UsdPrim notMesh = stage->DefinePrim(SdfPath("/NotMesh"), TfToken("Xform"));
    ExecutionContext missingStage;
    const MeshPreparationOptions options;

    CHECK_THROWS_WITH_AS(prepareMeshes(missingStage, "test group", { mesh.GetPrim() }, options),
                         "Cannot prepare meshes: execution context has no USD stage",
                         std::invalid_argument);
    ExecutionContext context = testutils::_getContext(stage);
    CHECK_THROWS_WITH_AS(prepareMeshes(context, "test group", { mesh.GetPrim(), notMesh }, options),
                         "Cannot prepare meshes: expected a valid Mesh prim",
                         std::invalid_argument);

    usd_optimize_execution_context_free(&context);
}
