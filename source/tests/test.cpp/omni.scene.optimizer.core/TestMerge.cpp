// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Test Utils
#include "../TestUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Operation.h>

// USD
#include <pxr/usd/usdGeom/primvarsAPI.h>

// doctest
#include <doctest/doctest.h>

using namespace omni::scene::optimizer;
PXR_NAMESPACE_USING_DIRECTIVE


TEST_CASE("Bucketing")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Bucketing invalid meshes")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));

        // Create two meshes with no topology
        stage->DefinePrim(SdfPath("/World/Mesh1"), TfToken("Mesh"));
        stage->DefinePrim(SdfPath("/World/Mesh2"), TfToken("Mesh"));

        stage->DefinePrim(SdfPath("/World/SkelRoot"), TfToken("SkelRoot"));

        CHECK_EQ(testutils::_getPrimCount(stage), 4);

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        OperationUPtr operation = core.getOperation("merge");
        REQUIRE(operation);

        // Readable args
        std::string json = R"(
            {
                "operation": "merge",
                "considerMaterials": true,
                "materialAlbedoAsVertexColors": false,
                "originalGeomOption": 1,
                "spatialMode": 1,
                "spatialThreshold": 10.0
            }
        )";

        JsValue args = PXR_NS::JsParseString(json);
        REQUIRE(operation->execute(&context, args.GetJsObject()).success);

        // Check same number of prims. Meshes with no topology did not get merged.
        CHECK_EQ(testutils::_getPrimCount(stage), 4);
        CHECK(stage->GetPrimAtPath(SdfPath("/World/Mesh1")).IsValid());
        CHECK(stage->GetPrimAtPath(SdfPath("/World/Mesh2")).IsValid());

        // Clean up
        so_execution_context_free(&context);
    }


    SUBCASE("Merge with custom schema")
    {
        UsdStageRefPtr stage = testutils::_openStage("mergeSchema.usda");
        REQUIRE(stage);

        // Three prims - world and two meshes
        CHECK_EQ(testutils::_getPrimCount(stage), 3);

        // No merged mesh
        CHECK(!stage->GetPrimAtPath(SdfPath("/merged")).IsValid());

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        OperationUPtr operation = core.getOperation("merge");
        REQUIRE(operation);

        // Readable args
        std::string json = R"(
            {
                "operation": "merge",
                "considerMaterials": true,
                "materialAlbedoAsVertexColors": false,
                "originalGeomOption": 1,
                "rootPath": "",
                "mergePoint": 5
            }
        )";

        JsValue args = PXR_NS::JsParseString(json);
        REQUIRE(operation->execute(&context, args.GetJsObject()).success);

        // Count reduce by one (two were merged) and the merged mesh exists
        CHECK_EQ(testutils::_getPrimCount(stage), 2);
        CHECK(stage->GetPrimAtPath(SdfPath("/World/merged")).IsValid());

        so_execution_context_free(&context);
    }

    SUBCASE("Test merge with albedo")
    {

        UsdStageRefPtr stage = testutils::_openStage("mergeAlbedo.usda");
        REQUIRE(stage);

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;
        context.singleThreaded = 1;

        OperationUPtr operation = core.getOperation("merge");
        REQUIRE(operation);

        // Readable args
        std::string json = R"(
            {
                "operation": "merge",
                "considerMaterials": true,
                "materialAlbedoAsVertexColors": true,
                "originalGeomOption": 1
            }
        )";

        JsValue args = PXR_NS::JsParseString(json);
        REQUIRE(operation->execute(&context, args.GetJsObject()).success);

        UsdPrim prim = stage->GetPrimAtPath(SdfPath("/merged"));
        CHECK(prim.IsValid());

        // Get new authored opacity primvar
        UsdGeomPrimvarsAPI primvarsAPI(prim);
        UsdGeomPrimvar primvar = primvarsAPI.GetPrimvar(TfToken("displayOpacity"));

        // Assert the albedo value from a material was written as a primvar
        VtFloatArray value;
        primvar.Get(&value);
        CHECK_EQ(value.size(), 1);
        CHECK_EQ(value[0], 0.5);

        so_execution_context_free(&context);
    }

    SUBCASE("Test primvar buffer overflow fix verification")
    {
        // This test verifies that the primvar buffer overflow fix works correctly
        // It creates the exact data mismatch that previously caused the crash
        // and verifies that the merge operation now succeeds

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        stage->DefinePrim(SdfPath("/RootNode"), TfToken("Xform"));
        stage->DefinePrim(SdfPath("/RootNode/Collider"), TfToken("Xform"));

        // Create multiple problematic meshes with the exact data from crash debug output
        // We need multiple meshes to trigger the mergePrimvars function
        UsdPrim meshPrim1 =
            stage->DefinePrim(SdfPath("/RootNode/Collider/opaque__emissive__carparklights1"), TfToken("Mesh"));
        UsdPrim meshPrim2 =
            stage->DefinePrim(SdfPath("/RootNode/Collider/opaque__emissive__carparklights2"), TfToken("Mesh"));

        // Create smaller geometry data to ensure meshes get merged
        // Use the same problematic ratio but with smaller numbers
        const size_t nFaces = 4; // Small number of faces
        const size_t nPoints = 8; // Small number of points
        const size_t nIndices = 16; // 4 faces * 4 vertices per face
        const size_t problemValuesSize = 1000; // Much larger than nIndices to cause overflow

        // Create face vertex counts (all quads)
        VtIntArray faceVertexCounts(nFaces, 4);

        // Create face vertex indices for 4 quads
        VtIntArray faceVertexIndices = { 0, 1, 2, 3, 4, 5, 6, 7, 0, 1, 2, 3, 4, 5, 6, 7 };

        // Create points - make them close together so they get merged
        VtVec3fArray points;
        points.reserve(nPoints);
        for (int i = 0; i < static_cast<int>(nPoints); ++i)
        {
            const float f = static_cast<float>(i);
            points.push_back(GfVec3f(f * 0.1f, f * 0.2f, f * 0.3f));
        }

        // Create identical points for the second mesh to ensure they're merged
        // Identical meshes should definitely be merged together
        VtVec3fArray points2 = points;

        // Create the problematic displayColor primvar with too many values
        // This is the root cause of the crash - values array is much larger than expected
        VtVec3fArray displayColorValues;
        displayColorValues.reserve(problemValuesSize);
        for (int i = 0; i < static_cast<int>(problemValuesSize); ++i)
        {
            displayColorValues.push_back(GfVec3f(1.0f, 1.0f, 1.0f)); // White color
        }

        // Set up the mesh geometry for both meshes
        UsdGeomMesh mesh1(meshPrim1);
        mesh1.CreatePointsAttr().Set(points);
        mesh1.CreateFaceVertexCountsAttr().Set(faceVertexCounts);
        mesh1.CreateFaceVertexIndicesAttr().Set(faceVertexIndices);

        UsdGeomMesh mesh2(meshPrim2);
        mesh2.CreatePointsAttr().Set(points2);
        mesh2.CreateFaceVertexCountsAttr().Set(faceVertexCounts);
        mesh2.CreateFaceVertexIndicesAttr().Set(faceVertexIndices);

        // Set up the problematic primvar for both meshes
        UsdGeomPrimvarsAPI primvarsAPI1(meshPrim1);
        UsdGeomPrimvar displayColorPrimvar1 = primvarsAPI1.CreatePrimvar(TfToken("primvars:displayColor"),
                                                                         SdfValueTypeNames->Color3fArray,
                                                                         UsdGeomTokens->faceVarying);
        displayColorPrimvar1.Set(displayColorValues);

        UsdGeomPrimvarsAPI primvarsAPI2(meshPrim2);
        UsdGeomPrimvar displayColorPrimvar2 = primvarsAPI2.CreatePrimvar(TfToken("primvars:displayColor"),
                                                                         SdfValueTypeNames->Color3fArray,
                                                                         UsdGeomTokens->faceVarying);
        displayColorPrimvar2.Set(displayColorValues);

        // Verify the problematic data setup
        CHECK_EQ(faceVertexCounts.size(), nFaces);
        CHECK_EQ(faceVertexIndices.size(), nIndices);
        CHECK_EQ(points.size(), nPoints);
        CHECK_EQ(displayColorValues.size(), problemValuesSize);

        // Verify the data mismatch that causes the crash
        CHECK_GT(displayColorValues.size(), nIndices);
        CHECK_EQ(displayColorValues.size() - nIndices, problemValuesSize - nIndices);

        // Run the merge operation to test the buffer overflow fix

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        OperationUPtr operation = core.getOperation("merge");
        REQUIRE(operation);

        // Configure merge operation to process the problematic mesh
        // Use mergePoint 8 (eParentPrim) to merge at parent level
        std::string json = R"(
            {
                "operation": "merge",
                "considerMaterials": false,
                "materialAlbedoAsVertexColors": false,
                "originalGeomOption": 1,
                "spatialMode": 0,
                "spatialThreshold": 0.0,
                "mergePoint": 8
            }
        )";

        JsValue args = PXR_NS::JsParseString(json);

        // Execute the merge operation - should succeed with the buffer overflow fix
        OperationResult result = operation->execute(&context, args.GetJsObject());

        // Verify the merge operation succeeded
        CHECK(result.success);

        // Check if any merged prims were created
        if (stage->GetPrimAtPath(SdfPath("/RootNode/Collider/merged")).IsValid())
        {
            CHECK(true); // Merged prim was created successfully
        }
        else
        {
            // Look for any merged prims that might have been created
            bool foundMergedPrim = false;
            for (const auto& prim : stage->Traverse())
            {
                if (prim.GetName().GetString().find("merged") != std::string::npos)
                {
                    foundMergedPrim = true;
                    break;
                }
            }
            CHECK(foundMergedPrim);
        }

        // Verify that the original meshes are no longer present (they should be merged)
        bool mesh1Exists = stage->GetPrimAtPath(SdfPath("/RootNode/Collider/opaque__emissive__carparklights1")).IsValid();
        bool mesh2Exists = stage->GetPrimAtPath(SdfPath("/RootNode/Collider/opaque__emissive__carparklights2")).IsValid();

        // At least one of the original meshes should be gone (merged)
        bool atLeastOneMerged = !mesh1Exists || !mesh2Exists;
        CHECK(atLeastOneMerged);

        so_execution_context_free(&context);
    }
}
