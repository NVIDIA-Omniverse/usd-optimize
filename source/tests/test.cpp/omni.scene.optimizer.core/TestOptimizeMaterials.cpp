// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Test Utils
#include "../TestUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/ParseJson.h>

// USD
#include <pxr/usd/usdGeom/primvarsAPI.h>

// doctest
#include <doctest/doctest.h>

using namespace omni::scene::optimizer;
PXR_NAMESPACE_USING_DIRECTIVE


TEST_CASE("Test Optimize Materials")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test remove unbound on empty scene")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        auto operation = core.getOperation("optimizeMaterials");
        REQUIRE(operation);

        std::string json = R"(
            {
                "optimizeMaterialsMode": 2
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);
        CHECK(operation->execute(&context, args.GetJsObject()).success);
    }


    SUBCASE("Test filtering materials")
    {
        UsdStageRefPtr stage = testutils::_openStage("optimizeMaterialsFilter.usda");
        REQUIRE(stage);

        // Assert expected material count
        CHECK_EQ(testutils::_countPrimsOfType<UsdShadeMaterial>(stage), 3);

        // Run optimize materials but with a filter
        std::string json = R"(
                    [
                        {
                            "operation": "optimizeMaterials",
                            "materialPrimPaths": ["/World/Material2"],
                            "optimizeMaterialsMode": 0
                        }
                    ]
                )";

        bool result = _parseJson(stage, json);
        CHECK(result);

        // Assert material count is the same
        CHECK_EQ(testutils::_countPrimsOfType<UsdShadeMaterial>(stage), 3);

        // Run again with NO filter
        json = R"(
                    [
                        {
                            "operation": "optimizeMaterials",
                            "materialPrimPaths": [],
                            "optimizeMaterialsMode": 0
                        }
                    ]
                )";

        result = _parseJson(stage, json);
        CHECK(result);

        // Duplicate now removed
        CHECK_EQ(testutils::_countPrimsOfType<UsdShadeMaterial>(stage), 2);
    }


    SUBCASE("Test child opacity")
    {
        UsdStageRefPtr stage = testutils::_openStage("optimizeMaterialsChildOpacity.usda");
        REQUIRE(stage);

        UsdPrim mesh = stage->GetPrimAtPath(SdfPath("/World/Cube/Shape"));
        CHECK(mesh.IsValid());

        UsdGeomPrimvarsAPI primvarsAPI(mesh);

        // Check displayOpacity has an authored value
        UsdGeomPrimvar opacityPrimvar = primvarsAPI.GetPrimvar(TfToken("displayOpacity"));
        CHECK(opacityPrimvar.GetAttr().HasAuthoredValue());

        // Run convertToColor (mode=1)
        std::string json = R"(
                    [
                        {
                            "operation": "optimizeMaterials",
                            "materialPrimPaths": [],
                            "optimizeMaterialsMode": 1
                        }
                    ]
                )";

        bool result = _parseJson(stage, json);
        CHECK(result);

        // Material is bound to the xform, not the mesh
        UsdGeomPrimvarsAPI primvarsAPIxform(stage->GetPrimAtPath(SdfPath("/World/Cube")));

        // The xform should have a color authored and an opacity
        VtVec3fArray colors;
        primvarsAPIxform.GetPrimvar(TfToken("displayColor")).Get(&colors);
        CHECK_EQ(colors.size(), 1);
        CHECK_EQ(colors[0], GfVec3f(1.0, 0.0, 0.0));

        VtFloatArray opacity;
        primvarsAPIxform.GetPrimvar(TfToken("displayOpacity")).Get(&opacity);
        CHECK_EQ(opacity.size(), 1);
        CHECK_EQ(opacity[0], 0.5);

        // Display opacity should have been blocked on the mesh
        opacityPrimvar = primvarsAPI.GetPrimvar(TfToken("displayOpacity"));
        CHECK(!opacityPrimvar.GetAttr().HasAuthoredValue());
    }
}
