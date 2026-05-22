// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Test Utils
#include "../TestUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/ParseJson.h>
#include <omni/scene.optimizer/core/RemovePrims.h>

// doctest
#include <doctest/doctest.h>

using namespace omni::scene::optimizer;
PXR_NAMESPACE_USING_DIRECTIVE


TEST_CASE("_deletePrims")
{
    SUBCASE("DeleteOption::ePrimOnly")
    {
        UsdStageRefPtr stage = testutils::_openStage("fourMeshesAtLevelOne.usd");
        REQUIRE(stage);

        std::string json = R"(
            [
                {
                    "operation": "deletePrims",
                    "primPaths": ["/World/group1/pPlane4"]
                }
            ]
        )";

        SdfPath primPath("/World/group1/pPlane4");

        UsdPrim prim = stage->GetPrimAtPath(primPath);
        CHECK(prim.IsValid());

        CHECK(_parseJson(stage, json));

        prim = stage->GetPrimAtPath(primPath);
        CHECK(!prim.IsValid());
    }

    SUBCASE("Delete Default Prim")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        SdfPath primPath("/TestPrim");

        UsdPrim prim = stage->DefinePrim(primPath);
        CHECK(prim.IsValid());

        stage->SetDefaultPrim(prim);

        CHECK_EQ(stage->GetDefaultPrim(), prim);

        // Delete prim
        _deletePrims(stage, { primPath.GetAsString() });

        // Verify prim deleted
        prim = stage->GetPrimAtPath(primPath);
        CHECK(!prim.IsValid());

        // Verify no default prim
        CHECK_FALSE(stage->HasDefaultPrim());
    }

    SUBCASE("Deleting invalid prim")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        UsdPrim invalidPrim;

        // Delete prim
        _deletePrims(stage, { invalidPrim });

        // Silly, but we didn't crash
        CHECK(true);
    }

    SUBCASE("Deleting an over")
    {
        UsdStageRefPtr stage = testutils::_openStage("deleteTest.usda");
        REQUIRE(stage);

        UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/Geometries"));
        CHECK(prim.IsValid());

        // Try to delete
        bool deactivate = false;
        _deletePrims(stage, { prim }, deactivate);

        // Should still exist and be active
        CHECK(prim.IsValid());
        CHECK(prim.IsActive());
    }

    SUBCASE("Delete empty list")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));
        stage->DefinePrim(SdfPath("/World/Foo"), TfToken("Xform"));
        stage->DefinePrim(SdfPath("/World/Bar"), TfToken("Xform"));

        CHECK_EQ(testutils::_getPrimCount(stage), 3);

        // Delete nothing
        std::vector<UsdPrim> prims;
        _deletePrims(stage, prims);

        // Nothing got deleted
        CHECK_EQ(testutils::_getPrimCount(stage), 3);
    }
}
