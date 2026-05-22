// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Test Utils
#include "../TestUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/geometry/VirtualMesh.h>

// doctest
#include <doctest/doctest.h>

using namespace omni::scene::optimizer;
PXR_NAMESPACE_USING_DIRECTIVE


TEST_CASE("Basic spatial analysis")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Analysis of basic cube")
    {
        UsdStageRefPtr stage = testutils::_openStage("simpleCube.usda");
        REQUIRE(stage);

        ExecutionContext context = testutils::_getContext(stage);
        context.analysisMode = 1;

        OperationUPtr operation = core.getOperation("sparseMeshes");
        REQUIRE(operation);

        // Execute operation; default args.
        JsObject args;
        OperationResult result = operation->execute(&context, args);
        REQUIRE(result.success);
        REQUIRE(result.output);

        // Extract output
        JsObject output = JsParseString(result.output).GetJsObject();

        // check there is an analysis section in the output
        CHECK(output.find("analysis") != output.end());

        // check there are disjointSparseMeshes and suggestedOperations sections in the analysis
        JsObject analysis = output["analysis"].GetJsObject();
        CHECK(analysis.find("disjointSparseMeshes") != analysis.end());
        CHECK(analysis.find("suggestedOperations") != analysis.end());

        // disjointSparseMeshes should be empty
        JsObject disjointSparseMeshes = analysis["disjointSparseMeshes"].GetJsObject();
        CHECK(disjointSparseMeshes.empty());

        // check suggestedOperations should be empty
        JsArray suggestedOperations = analysis["suggestedOperations"].GetJsArray();
        CHECK(suggestedOperations.empty());
    }
}


TEST_CASE("Test fake factory data")
{
    auto& core = SceneOptimizerCore::getInstance();

    UsdStageRefPtr stage = testutils::_openStage("analysis/factory-post.usda");
    REQUIRE(stage);

    // New context, ensure analysis mode is enabled
    ExecutionContext context = testutils::_getContext(stage);
    context.analysisMode = 1;

    OperationUPtr operation = core.getOperation("sparseMeshes");
    REQUIRE(operation);

    // Execute operation; default args.
    JsObject args;
    OperationResult result = operation->execute(&context, args);
    REQUIRE(result.success);
    REQUIRE(result.output);

    // Get output
    JsObject output = JsParseString(result.output).GetJsObject();
    JsObject analysis = output["analysis"].GetJsObject();

    // check the disjointSparseMeshes results
    JsObject disjointSparseMeshes = analysis["disjointSparseMeshes"].GetJsObject();
    // Wall items are ~2.9%
    float resWallMerged = disjointSparseMeshes["/World/Wall/merged"].GetReal();
    CHECK_GT(resWallMerged, 2.9);
    CHECK_LT(resWallMerged, 2.91);
    // The merged lights should be ~1.5
    float resLights = disjointSparseMeshes["/World/Lights/merged"].GetReal();
    CHECK_GT(resLights, 1.5);
    CHECK_LT(resLights, 1.6);
    // coinciding meshes should be ~25%
    float resCoinciding = disjointSparseMeshes["/World/Coinciding/merged"].GetReal();
    CHECK_GT(resCoinciding, 25.0);
    CHECK_LT(resCoinciding, 26.0);

    // check the suggestedOperations and get the multi-cluster arg for Split
    JsArray suggestedOperations = analysis["suggestedOperations"].GetJsArray();
    CHECK(suggestedOperations.size() == 1);
    JsObject splitMeshesConfig = suggestedOperations[0].GetJsObject();
    JsObject splitMeshesArgs = splitMeshesConfig["args"].GetJsObject();
    std::string multiClusterArgStr = splitMeshesArgs["multiCluster"].GetString();
    JsArray multiClusterArg = JsParseString(multiClusterArgStr).GetJsArray();
    for (const JsValue& item : multiClusterArg)
    {
        JsObject cluster = item.GetJsObject();
        JsArray paths = cluster["paths"].GetJsArray();
        const std::string firstPath = paths[0].GetString();

        // check expected parameters for each path
        if (firstPath == "/World/Lights/merged")
        {
            int spatialMode = cluster["spatialMode"].GetInt();
            CHECK(spatialMode == 0);
        }
        else if (firstPath == "/World/Coinciding/merged" || firstPath == "/World/Wall/merged")
        {
            int spatialMode = cluster["spatialMode"].GetInt();
            CHECK(spatialMode == 1);
            // check values are within range
            float spatialMaxSize = cluster["spatialMaxSize"].GetReal();
            CHECK_GT(spatialMaxSize, 59.0);
            CHECK_LT(spatialMaxSize, 61.0);
            float spatialThreshold = cluster["spatialThreshold"].GetReal();
            CHECK_GT(spatialThreshold, 39.0);
            CHECK_LT(spatialThreshold, 41.0);
        }
    }
}

TEST_CASE("Test Dicing and Clustering Analysis")
{
    auto& core = SceneOptimizerCore::getInstance();

    UsdStageRefPtr stage = testutils::_openStage("analysis/multiPrim.usda");
    REQUIRE(stage);

    // New context, ensure analysis mode is enabled
    ExecutionContext context = testutils::_getContext(stage);
    context.analysisMode = 1;

    OperationUPtr operation = core.getOperation("sparseMeshes");
    REQUIRE(operation);

    // Execute operation; default args.
    JsObject args;
    OperationResult result = operation->execute(&context, args);
    REQUIRE(result.success);
    REQUIRE(result.output);

    // Get output
    JsObject output = JsParseString(result.output).GetJsObject();
    JsObject analysis = output["analysis"].GetJsObject();

    // check some disjointSparseMeshes results
    JsObject disjointSparseMeshes = analysis["disjointSparseMeshes"].GetJsObject();
    float resMeshA = disjointSparseMeshes["/World/meshA"].GetReal();
    CHECK_GT(resMeshA, 21.0);
    CHECK_LT(resMeshA, 22.0);
    float resMeshB = disjointSparseMeshes["/World/meshB"].GetReal();
    CHECK_GT(resMeshB, 0.9);
    CHECK_LT(resMeshB, 1.0);
    float resMeshD = disjointSparseMeshes["/World/meshD"].GetReal();
    CHECK_GT(resMeshD, 18.0);
    CHECK_LT(resMeshD, 19.0);

    // check that one large sparse mesh was found
    JsObject largeSparseMeshes = analysis["largeSparseMeshes"].GetJsObject();
    float density = largeSparseMeshes["/World/Cube"].GetReal();
    CHECK_GT(density, 0.024);
    CHECK_LT(density, 0.026);

    // check the suggestedOperations
    JsArray suggestedOperations = analysis["suggestedOperations"].GetJsArray();
    CHECK(suggestedOperations.size() == 2);

    // check suggested split operation
    JsObject splitMeshesConfig = suggestedOperations[0].GetJsObject();
    JsObject splitMeshesArgs = splitMeshesConfig["args"].GetJsObject();
    std::string multiClusterArgStr = splitMeshesArgs["multiCluster"].GetString();
    JsArray multiClusterArg = JsParseString(multiClusterArgStr).GetJsArray();
    CHECK(multiClusterArg.size() == 8);

    // check suggested dice operation
    JsObject diceMeshesConfig = suggestedOperations[1].GetJsObject();
    JsObject diceMeshesArgs = diceMeshesConfig["args"].GetJsObject();
    JsArray dicePathsArg = diceMeshesArgs["paths"].GetJsArray();
    CHECK(dicePathsArg.size() == 1);
    std::string dicePath = dicePathsArg[0].GetString();
    CHECK(dicePath == "/World/Cube");
}


TEST_CASE("Test mirror xform")
{
    auto& core = SceneOptimizerCore::getInstance();

    UsdStageRefPtr stage = testutils::_openStage("analysis/mirrorXform.usda");
    REQUIRE(stage);

    // New context, ensure analysis mode is enabled
    ExecutionContext context = testutils::_getContext(stage);
    context.analysisMode = 1;

    OperationUPtr operation = core.getOperation("sparseMeshes");
    REQUIRE(operation);

    // Execute operation; default args.
    JsObject args;
    OperationResult result = operation->execute(&context, args);
    REQUIRE(result.success);
    REQUIRE(result.output);

    // Get output
    JsObject output = JsParseString(result.output).GetJsObject();
    JsObject analysis = output["analysis"].GetJsObject();

    // check some density results
    JsObject disjointSparseMeshes = analysis["disjointSparseMeshes"].GetJsObject();
    float resMesh = disjointSparseMeshes["/World/mirrored"].GetReal();
    CHECK_GT(resMesh, 33.0);
    CHECK_LT(resMesh, 35.0);

    // note: if we ever add some more direct VirtualMesh unit tests this test should probably go with them in a
    //       separate file
    // -----
    // Create a VirtualMesh from the mesh with a mirror xform and ensure we get similar volume results for both local
    // and world extents (this checks that the world space extent calculation is being done correctly)
    UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/mirrored"));
    REQUIRE(prim);
    UsdGeomXformCache xformCache;
    UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
    VirtualMesh mesh(prim, xformCache, bindingsCache, collQueryCache);
    mesh.validateAndComputeExtent();
    // compute volume using local extent
    const VtVec3fArray& localExtent = mesh.getLocalExtent();
    const GfRange3d localRange = GfRange3d(localExtent[0], localExtent[1]);
    const float localVolume = static_cast<float>(GfBBox3d(localRange).GetVolume());
    // compute volume using world extent
    const VtVec3fArray& worldExtent = mesh.getWorldExtent();
    const GfRange3d worldRange = GfRange3d(worldExtent[0], worldExtent[1]);
    const float worldVolume = static_cast<float>(GfBBox3d(worldRange).GetVolume());
    // check the volumes are similar (1.0 is a small margin of error since the volume is around 5865959.0)
    CHECK(fabs(worldVolume - localVolume) < 1.0);
    // -----
}


TEST_CASE("Large Sparse Mesh")
{

    auto& core = SceneOptimizerCore::getInstance();

    UsdStageRefPtr stage = testutils::_openStage("sparseSingleMeshes.usda");
    REQUIRE(stage);

    ExecutionContext context = testutils::_getContext(stage);
    context.analysisMode = 1;

    OperationUPtr operation = core.getOperation("sparseMeshes");
    REQUIRE(operation);

    // Execute operation; default args.
    JsObject args;
    OperationResult result = operation->execute(&context, args);
    REQUIRE(result.success);
    REQUIRE(result.output);

    // Get output
    JsObject output = JsParseString(result.output).GetJsObject();
    JsObject analysis = output["analysis"].GetJsObject();

    // check the median extent volume and size
    const float medianExtentVolume = analysis["medianExtentVolume"].GetReal();
    CHECK_GT(medianExtentVolume, 999999.0);
    CHECK_LT(medianExtentVolume, 1000001.0);
    const float medianExtentSize = analysis["medianExtentSize"].GetReal();
    CHECK_GT(medianExtentSize, 99.0);
    CHECK_LT(medianExtentSize, 101.0);

    // check that one large sparse mesh was found
    JsObject largeSparseMeshes = analysis["largeSparseMeshes"].GetJsObject();
    float density = largeSparseMeshes["/World/Cube"].GetReal();
    CHECK_GT(density, 0.024);
    CHECK_LT(density, 0.026);
}
