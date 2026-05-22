// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Test Utils
#include "../TestUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Coalescer.h>
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/CudaUtils.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/ParseJson.h>
#include <omni/scene.optimizer/core/Report.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/extras/Library.h>

// USD
#include <pxr/base/js/json.h>
#include <pxr/base/js/utils.h>
#include <pxr/usd/usdGeom/camera.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdLux/diskLight.h>

// doctest
#include <doctest/doctest.h>

// Standard library
#include <atomic>
#include <vector>

// TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>


using namespace omni::scene::optimizer;
PXR_NAMESPACE_USING_DIRECTIVE


TEST_CASE("ParseJson")
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory();

    SUBCASE("Empty JSON document")
    {
        // Assert a valid, but empty, document triggers a warning
        std::string json("null");
        bool result = _parseJson(stage, json);
        CHECK_FALSE(result);
    }


    SUBCASE("Invalid argument type")
    {
        // meshPrimPaths should be an array
        std::string json = R"(
            [
                {
                    "operation": "merge",
                    "meshPrimPaths": "invalid type"
                }
            ]
        )";

        // Assert the JSON fails
        bool result = _parseJson(stage, json);
        CHECK_FALSE(result);
    }


    SUBCASE("Invalid operation")
    {
        // Expected data is a list of objects
        std::string json = R"(
            [
                "not an object"
            ]
        )";

        bool result = _parseJson(stage, json);
        CHECK_FALSE(result);
    }
}


TEST_CASE("Operation Logging")
{

    auto& core = SceneOptimizerCore::getInstance();

#ifndef _MSC_VER
    SUBCASE("Test invalid report path")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        // Force an invalid file path
        std::string reportPath("/");
        context.reportPath = (char*)malloc(sizeof(char) * (reportPath.length() + 1));
        reportPath.copy(context.reportPath, reportPath.length());

        auto operation = core.getOperation("pruneLeaves");
        REQUIRE(operation);
        REQUIRE(operation->execute(&context, JsObject{}).success);

        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }

    SUBCASE("Test reporting functions")
    {
        // Get unique temp file name
        std::string reportPath = _getTempFile("scene-optimizer-unit.tests", "");

        auto report = new Report(reportPath);
        REQUIRE(report);
        CHECK(report->initialize());

        report->log(LogLevel::eDebug, "UNIT.TEST", "debug message", false);
        report->log(LogLevel::eInfo, "UNIT.TEST", "info message", false);
        report->log(LogLevel::eWarning, "UNIT.TEST", "warning message", false);
        report->log(LogLevel::eError, "UNIT.TEST", "error message", false);

        // Delete, which closes the file handle.
        delete report;

        // Read the report
        std::ifstream s(reportPath);
        std::stringstream buffer;
        buffer << s.rdbuf();
        s.close();

        // Read into string
        std::string reportData = buffer.str();

        // Report should contain data, and the message markers
        CHECK_GT(reportData.length(), 0);
        CHECK_NE(reportData.find("DEBUG|"), std::string::npos);
        CHECK_NE(reportData.find("INFO|"), std::string::npos);
        CHECK_NE(reportData.find("WARNING|"), std::string::npos);
        CHECK_NE(reportData.find("ERROR|"), std::string::npos);
    }
#endif

    SUBCASE("Test error logging")
    {
        // Create context with no stage
        ExecutionContext context;
        context.generateReport = 1;
        context.verbose = 1;

        auto operation = core.getOperation("pruneLeaves");
        REQUIRE(operation);

        // Invalid mesh path on the stage, should log an error
        std::string json = R"(
            {
                "paths": ["/World/Mesh"],
                "attributes": ["no attribute"]
            }
        )";

        JsValue args = PXR_NS::JsParseString(json);
        CHECK_FALSE(operation->execute(&context, args.GetJsObject()).success);

        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }
}


TEST_CASE("Analysis Mode Supported")
{

    auto& core = SceneOptimizerCore::getInstance();

    // create a context with analysis mode enabled
    ExecutionContext context;
    context.analysisMode = 1;

    // the editStageMetrics operation does not support analysis mode
    auto operation = core.getOperation("editStageMetrics");
    REQUIRE(operation);

    // operation should have failed to run
    CHECK_FALSE(operation->execute(&context, JsObject{}).success);

    // Free the context to tidy up reportPath
    so_execution_context_free(&context);
}


TEST_CASE("Test Optimize Time Samples")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test attribute name filtering")
    {

        UsdStageRefPtr stage = testutils::_openStage("optimizeTimeSamples.usda");
        REQUIRE(stage);

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        // Assert initial state
        UsdPrim prim = stage->GetPrimAtPath(SdfPath("/CubeLinear"));
        UsdAttribute attr = prim.GetAttribute(TfToken("xformOp:transform"));
        CHECK_EQ(attr.GetNumTimeSamples(), 120);

        // Assert prim might have time varying attribute(s)
        CHECK(_mightBeTimeVarying(prim));

        auto operation = core.getOperation("optimizeTimeSamples");
        REQUIRE(operation);

        // Readable args
        std::string json = R"(
            {
                "paths": ["/CubeLinear"],
                "attributes": ["no attribute"]
            }
        )";

        JsValue args = PXR_NS::JsParseString(json);
        REQUIRE(operation->execute(&context, args.GetJsObject()).success);

        // Should still have 120
        CHECK_EQ(attr.GetNumTimeSamples(), 120);

        // Run again, with no attribute filter.
        json = R"(
            {
                "paths": ["/CubeLinear"],
                "attributes": []
            }
         )";

        args = PXR_NS::JsParseString(json);
        REQUIRE(operation->execute(&context, args.GetJsObject()).success);

        // No filtering, time samples cleansed
        CHECK_EQ(attr.GetNumTimeSamples(), 48);

        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }
}


TEST_CASE("Test Utils")
{
    SUBCASE("Test material albedo")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        UsdShadeMaterial material = UsdShadeMaterial::Define(stage, SdfPath("/World/Material"));
        UsdShadeShader shader = UsdShadeShader::Define(stage, SdfPath("/World/Material/Shader"));

        UsdShadeInput colorInput = shader.CreateInput(TfToken("diffuseColor"), SdfValueTypeNames->Color3f);
        UsdShadeOutput surfaceOutput = shader.CreateOutput(TfToken("surface"), SdfValueTypeNames->Token);

        material.GetSurfaceOutput().ConnectToSource(surfaceOutput);

        GfVec3f color(0.5, 0.75, 1.0);
        colorInput.Set(color);

        ColorValue colorValue{};
        CHECK(_getMaterialAlbedo(material, colorValue));

        // Assert the correct color was retrieved from the material via output
        CHECK_EQ(colorValue.color, color);
    }
}


TEST_CASE("Test Atlas UVs")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test invalid mesh")
    {

        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/World/Mesh"));
        CHECK(mesh.GetPrim().IsValid());

        UsdGeomPrimvarsAPI primvarsAPI(mesh);

        // Check no existing UVs
        UsdGeomPrimvar primvarSt = primvarsAPI.GetPrimvar(TfToken("st"));
        CHECK(!primvarSt.HasAuthoredValue());

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        auto operation = core.getOperation("generateAtlasUVs");
        REQUIRE(operation);
        REQUIRE(operation->execute(&context, JsObject{}).success);

        // Still no authored value on an invalid mesh
        primvarSt = primvarsAPI.GetPrimvar(TfToken("st"));
        CHECK(!primvarSt.HasAuthoredValue());

        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }
}


TEST_CASE("Test Projection UVs")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test invalid mesh")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/World/Mesh"));
        CHECK(mesh.GetPrim().IsValid());

        UsdGeomPrimvarsAPI primvarsAPI(mesh);

        // Check no existing UVs
        UsdGeomPrimvar primvarSt = primvarsAPI.GetPrimvar(TfToken("st"));
        CHECK(!primvarSt.HasAuthoredValue());

        ExecutionContext context = testutils::_getContext(stage);

        auto operation = core.getOperation("generateProjectionUVs");
        REQUIRE(operation);
        REQUIRE(operation->execute(&context, JsObject{}).success);

        // Still no authored value on an invalid mesh
        primvarSt = primvarsAPI.GetPrimvar(TfToken("st"));
        CHECK(!primvarSt.HasAuthoredValue());

        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }

    SUBCASE("Test invalid matrix")
    {
        UsdStageRefPtr stage = testutils::_openStage("meshNoUVs.usd");
        REQUIRE(stage);

        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/World/pCube1"));
        CHECK(mesh.GetPrim().IsValid());

        UsdGeomPrimvarsAPI primvarsAPI(mesh);

        // Check no existing UVs
        UsdGeomPrimvar primvarSt = primvarsAPI.GetPrimvar(TfToken("st"));
        CHECK(!primvarSt.HasAuthoredValue());

        auto operation = core.getOperation("generateProjectionUVs");
        REQUIRE(operation);

        ExecutionContext context = testutils::_getContext(stage);

        // Args with invalid preprojection matrix (requires 16 floats)
        std::string json = R"(
            {
                "paths": [],
                "useWorldSpaceScales": true,
                "projectionType": 4,
                "preprojectionXform": [1.0, 2.0]
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);

        // Execution should fail
        CHECK_FALSE(operation->execute(&context, args.GetJsObject()).success);

        // Still no authored value on an invalid mesh
        primvarSt = primvarsAPI.GetPrimvar(TfToken("st"));
        CHECK(!primvarSt.HasAuthoredValue());

        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }
}


TEST_CASE("Test Stats")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test String Format")
    {
        // Number of bytes just under the threshold
        double bytes = 1021.3;
        std::string b = _getFormattedBytes(bytes);
        CHECK_EQ(b, "1021.30 B");

        bytes *= 1024;
        std::string kb = _getFormattedBytes(bytes);
        CHECK_EQ(kb, "1021.30 KB");

        bytes *= 1024;
        std::string mb = _getFormattedBytes(bytes);
        CHECK_EQ(mb, "1021.30 MB");

        bytes *= 1024;
        std::string gb = _getFormattedBytes(bytes);
        CHECK_EQ(gb, "1021.30 GB");

        bytes *= 1024;
        std::string tb = _getFormattedBytes(bytes);
        CHECK_EQ(tb, "1021.30 TB");
    }

    SUBCASE("Test Counting")
    {

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        stage->DefinePrim(SdfPath("/World/Untyped"), TfToken());

        UsdPrim primInvisible = stage->DefinePrim(SdfPath("/World/Invisible"), TfToken("Xform"));
        UsdGeomImageable imageable(primInvisible);
        imageable.GetVisibilityAttr().Set(UsdGeomTokens->invisible);

        UsdPrim primInactive = stage->DefinePrim(SdfPath("/World/Inactive"), TfToken("Xform"));
        primInactive.SetActive(false);

        UsdGeomMesh meshPrim = UsdGeomMesh::Define(stage, SdfPath("/World/Mesh"));
        UsdGeomPrimvarsAPI primvarsAPI(meshPrim);

        UsdGeomBasisCurves curve = UsdGeomBasisCurves::Define(stage, SdfPath("/World/Curves"));
        curve.GetPointsAttr().Set(VtVec3fArray({
            GfVec3f(1.0, 1.0, 1.0),
            GfVec3f(2.0, 2.0, 2.0),
            GfVec3f(3.0, 3.0, 3.0),
        }));

        // Non-indexed primvar
        UsdGeomPrimvar primvar1 =
            primvarsAPI.CreatePrimvar(TfToken("displayColor"), SdfValueTypeNames->Color3fArray, UsdGeomTokens->uniform);
        VtVec3fArray colors1({
            GfVec3f(1.0, 0.0, 0.0),
            GfVec3f(1.0, 1.0, 0.0),
            GfVec3f(1.0, 1.0, 1.0),
            GfVec3f(0.0, 1.0, 0.0),
            GfVec3f(0.0, 1.0, 1.0),
            GfVec3f(0.0, 0.0, 1.0),
        });
        primvar1.Set(colors1);

        // Indexed primvar
        VtVec3fArray colors2({
            GfVec3f(1.0, 0.0, 0.0),
            GfVec3f(0.0, 0.0, 1.0),
        });
        VtIntArray indices({ 0, 1, 0, 1, 0, 1, 0, 1 });
        UsdGeomPrimvar primvar2 = primvarsAPI.CreateIndexedPrimvar(TfToken("displayColorIndexed"),
                                                                   SdfValueTypeNames->Color3fArray,
                                                                   colors2,
                                                                   indices,
                                                                   UsdGeomTokens->varying);

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;
        context.analysisMode = 1;

        auto operation = core.getOperation("printStats");
        REQUIRE(operation);

        std::string json = R"(
            {
                "splitCollocatedPoints": false,
                "countPrimvars": true
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);

        // Verify the operation succeeds.
        OperationResult result = operation->execute(&context, args.GetJsObject());
        CHECK(result.success);
        REQUIRE(result.output);

        JsValue payload = JsParseString(result.output);
        REQUIRE(payload.IsObject());

        JsObject _payload = payload.GetJsObject();

        JsObject analysis = _payload["analysis"].GetJsObject();

        // Check mesh count
        JsObject types = analysis["types"].GetJsObject();
        JsObject mesh = types["Mesh"].GetJsObject();
        int meshCount = mesh["count"].GetInt();
        CHECK_EQ(meshCount, 1);

        // Check curve count
        JsObject curves = types["BasisCurves"].GetJsObject();
        int curveCount = curves["count"].GetInt();
        CHECK_EQ(curveCount, 1);

        // Check primvar1 value count
        JsObject primvars = analysis["primvars"].GetJsObject();
        JsObject displayColor = primvars["displayColor"].GetJsObject();
        int displayColorCount = displayColor["valueCount"].GetInt();
        CHECK_EQ(displayColorCount, 6);

        // Check primvar2 value count
        JsObject displayColor2 = primvars["displayColorIndexed"].GetJsObject();
        int displayColorCount2 = displayColor2["valueCount"].GetInt();
        CHECK_EQ(displayColorCount2, 8);

        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }


    SUBCASE("Test Weld Points")
    {

        UsdStageRefPtr stage = testutils::_openStage("splitMeshes_various.usda");

        ExecutionContext context = testutils::_getContext(stage);
        context.generateReport = 1;
        context.verbose = 1;

        auto operation = core.getOperation("printStats");
        REQUIRE(operation);

        std::string json = R"(
            {
                "splitCollocatedPoints": false
            }
         )";

        // Verify the operation succeeds.
        OperationResult result = operation->execute(&context, JsParseString(json).GetJsObject());
        CHECK(result.success);

        REQUIRE(result.output);

        JsValue resultValue = JsParseString(result.output);
        REQUIRE(resultValue.IsObject());

        JsObject root = resultValue.GetJsObject();

        JsValue analysis = root["analysis"];
        REQUIRE(analysis.IsObject());

        JsObject object = analysis.GetJsObject();

        // Assert expected total
        CHECK_EQ(object["prims"].GetInt(), 9);

        // Assert expected disjoint number
        JsObject types = object["types"].GetJsObject();
        JsObject meshes = types["Mesh"].GetJsObject();
        CHECK_EQ(meshes["disjoint"].GetInt(), 16);

        // Test again, with splitCollocated enabled
        json = R"(
            {
                "splitCollocatedPoints": true
            }
         )";

        // Verify the operation succeeds.
        result = operation->execute(&context, JsParseString(json).GetJsObject());
        CHECK(result.success);

        resultValue = JsParseString(result.output);
        REQUIRE(resultValue.IsObject());

        root = resultValue.GetJsObject();

        analysis = root["analysis"];
        REQUIRE(analysis.IsObject());

        object = analysis.GetJsObject();

        // Assert expected total
        CHECK_EQ(object["prims"].GetInt(), 9);

        // Assert expected disjoint number - more, as we are not deduplicating the collocated points
        types = object["types"].GetJsObject();
        meshes = types["Mesh"].GetJsObject();
        CHECK_EQ(meshes["disjoint"].GetInt(), 26);


        // Free the context to tidy up reportPath
        so_execution_context_free(&context);
    }
}


TEST_CASE("Test Split Meshes")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test empty mesh")
    {

        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        // Define mesh, with no topology
        UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/World/Mesh"));
        CHECK(mesh.GetPrim().IsValid());
        CHECK(!mesh.GetPointsAttr().HasAuthoredValue());

        stage->OverridePrim(SdfPath("/World/Geometry"));

        VtIntArray invalidFaces = { 0 };
        mesh.GetFaceVertexCountsAttr().Set(invalidFaces);

        // No children (ie, geom subsets)
        CHECK(mesh.GetPrim().GetAllChildren().empty());

        ExecutionContext context = testutils::_getContext(stage);

        auto operation = core.getOperation("splitMeshes");
        REQUIRE(operation);

        // Put the same path in twice, to cover skipping already seen prims
        // Also adds an invalid SdfPath to test nothing fails.
        std::string json = R"(
            {
                "paths": ["/World", "/World", "!"],
                "method": 0
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);
        CHECK(operation->execute(&context, args.GetJsObject()).success);

        // Assert still no children - nothing was split because no topology
        CHECK(mesh.GetPrim().IsValid());
        CHECK(mesh.GetPrim().GetAllChildren().empty());
    }

    SUBCASE("Test splitting solid mesh")
    {

        UsdStageRefPtr stage = testutils::_openStage("simpleCube.usda");
        REQUIRE(stage);

        // Prim is valid, no children
        UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/Cube"));
        CHECK(prim.IsValid());
        CHECK(prim.GetAllChildren().empty());

        ExecutionContext context = testutils::_getContext(stage);

        auto operation = core.getOperation("splitMeshes");
        REQUIRE(operation);

        // Put the same path in twice, to cover skipping already seen prims
        // Also adds an invalid SdfPath to test nothing fails.
        std::string json = R"(
            {
                "method": 0,
                "splitCollocatedPoints": false
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);
        CHECK(operation->execute(&context, args.GetJsObject()).success);

        // Prim still has no children - nothing to split
        CHECK(prim.GetAllChildren().empty());
    }

    SUBCASE("Test splitting with invalid options")
    {

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        ExecutionContext context = testutils::_getContext(stage);

        auto operation = core.getOperation("splitMeshes");
        REQUIRE(operation);

        // Put the same path in twice, to cover skipping already seen prims
        // Also adds an invalid SdfPath to test nothing fails.
        std::string json = R"(
            {
                "method": 0,
                "splitOn": 1
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);

        // Should fail
        CHECK_FALSE(operation->execute(&context, args.GetJsObject()).success);
    }
}


static size_t _countInstanced(const UsdStageWeakPtr& stage)
{
    size_t result = 0;
    for (const auto& prim : stage->Traverse())
    {
        if (prim.IsInstance())
        {
            ++result;
        }
    }

    return result;
}


TEST_CASE("Test Utility Functions")
{

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test simple deinstance")
    {
        UsdStageRefPtr stage = testutils::_openStage("instancedCubes.usda");
        REQUIRE(stage);

        // Assert initial state
        CHECK_EQ(_countInstanced(stage), 3);

        ExecutionContext context = testutils::_getContext(stage);

        auto operation = core.getOperation("utilityFunction");
        REQUIRE(operation);

        std::string json = R"(
            {
                "function": 0
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);
        CHECK(operation->execute(&context, args.GetJsObject()).success);

        // Assert operation did the thing
        CHECK_EQ(_countInstanced(stage), 0);
    }
}


TEST_CASE("Test Optimize Primvars")
{
    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test simplify and ignore")
    {

        UsdStageRefPtr stage = testutils::_openStage("optimizePrimvars.usda");
        REQUIRE(stage);

        ExecutionContext context = testutils::_getContext(stage);

        auto operation = core.getOperation("optimizePrimvars");
        REQUIRE(operation);

        // Mode=Ignore and Simplify=False means nothing to do
        // Operation should not fail though
        std::string json = R"(
            {
                "mode": 0,
                "simplify": false
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);
        CHECK(operation->execute(&context, args.GetJsObject()).success);
    }
}


TEST_CASE("Test decimate meshes")
{
    // Current Decimate is only available when building against Python3.12 so skip test otherwise
    if (SCENE_OPTIMIZER_PYTHON_VERSION != std::string("3.12"))
    {
        return;
    }

    auto& core = SceneOptimizerCore::getInstance();

    SUBCASE("Test CPU threshold greater than GPU threshold")
    {

        UsdStageRefPtr stage = testutils::_openStage("simpleCube.usda");
        REQUIRE(stage);

        ExecutionContext context = testutils::_getContext(stage);

        auto operation = core.getOperation("decimateMeshes");
        REQUIRE(operation);

        // Test with CPU > GPU
        std::string json = R"(
            {
                "cpuVertexCountThreshold": 100000,
                "gpuVertexCountThreshold": 99999
            }
         )";

        JsValue args = PXR_NS::JsParseString(json);
        CHECK(operation->execute(&context, args.GetJsObject()).success);
    }
}


static std::vector<std::string> primsToPaths(const std::vector<UsdPrim>& prims)
{
    std::vector<std::string> result;
    result.reserve(prims.size());

    for (const auto& prim : prims)
    {
        result.push_back(prim.GetPrimPath().GetAsString());
    }

    return result;
}


TEST_CASE("Test basic path expressions")
{

    UsdStageRefPtr stage = testutils::_openStage("pathResolverTest.usda");
    REQUIRE(stage);

    bool meshesOnly = false;
    bool reverse = false;

    std::vector<std::string> inputExpressions;
    std::vector<std::string> outputPaths;

    SUBCASE("Test explicit prim")
    {
        inputExpressions = { "/World" };
        outputPaths = { "/World" };
    }

    SUBCASE("Test recursion")
    {
        inputExpressions = { "/World//" };
        outputPaths = { "/World",      "/World/Foo1", "/World/Foo1/FooChild1", "/World/Foo2", "/World/Foo2/FooChild2",
                        "/World/Bar1", "/World/Bar2" };
    }

    SUBCASE("Test globbing")
    {
        inputExpressions = { "//Foo*" };
        outputPaths = { "/World/Foo1", "/World/Foo1/FooChild1", "/World/Foo2", "/World/Foo2/FooChild2" };
    }

    SUBCASE("Test not")
    {
        inputExpressions = { "//Foo* - //*Child*" };
        outputPaths = { "/World/Foo1", "/World/Foo2" };
    }

    SUBCASE("Test and")
    {
        inputExpressions = { "//*Child* + //*Bar*" };
        outputPaths = { "/World/Foo1/FooChild1", "/World/Foo2/FooChild2", "/World/Bar1", "/World/Bar2" };
    }

    const std::vector<UsdPrim>& result = _resolveExpressionsToPrims(stage, inputExpressions, meshesOnly, reverse);

    CHECK_EQ(primsToPaths(result), outputPaths);

    // Check resolveSdfPaths returns an equivalent result (note it will be in reverse).
    // This is for coverage.
    SdfPathVector converted = _resolveSdfPaths(stage, inputExpressions);
    std::reverse(converted.begin(), converted.end());
    REQUIRE_EQ(converted.size(), outputPaths.size());
    for (size_t i = 0; i < converted.size(); ++i)
    {
        CHECK_EQ(converted[i].GetAsString(), outputPaths[i]);
    }
}


TEST_CASE("Test path expression wildcards")
{

    UsdStageRefPtr stage = UsdStage::CreateInMemory();

    stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));
    stage->DefinePrim(SdfPath("/World/Foo1"), TfToken("Mesh"));
    stage->DefinePrim(SdfPath("/World/Foo2"), TfToken("Mesh"));
    stage->DefinePrim(SdfPath("/World/Foo3"), TfToken("Mesh"));
    stage->DefinePrim(SdfPath("/World/Fop1"), TfToken("Mesh"));

    bool meshesOnly = true;
    bool reverse = false;
    std::vector<std::string> inputExpressions;
    std::vector<std::string> outputPaths;

    SUBCASE("Test character list")
    {
        inputExpressions = { "/World/Foo[12]" };
        outputPaths = { "/World/Foo1", "/World/Foo2" };
    }

    SUBCASE("Test any char")
    {
        inputExpressions = { "/World/Fo?1" };
        outputPaths = { "/World/Foo1", "/World/Fop1" };
    }

    SUBCASE("Test char range")
    {
        inputExpressions = { "/World/Foo[0-9]" };
        outputPaths = { "/World/Foo1", "/World/Foo2", "/World/Foo3" };
    }

    // Assert result
    auto prims = _resolveExpressionsToPrims(stage, inputExpressions, meshesOnly, reverse);
    CHECK_EQ(primsToPaths(prims), outputPaths);
}


TEST_CASE("Test other path expressions")
{
    SUBCASE("Test meshesOnly")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        stage->DefinePrim(SdfPath("/World"), TfToken("Xform"));
        stage->DefinePrim(SdfPath("/World/Material"), TfToken("Material"));
        stage->DefinePrim(SdfPath("/World/Material/Shader"), TfToken("Shader"));
        stage->DefinePrim(SdfPath("/World/NotMesh"), TfToken("Xform"));
        stage->DefinePrim(SdfPath("/World/Mesh"), TfToken("Mesh"));

        std::vector<std::string> paths = { "//*" };
        bool meshesOnly = true;
        std::vector<UsdPrim> prims = _resolveExpressionsToPrims(stage, paths, meshesOnly);

        CHECK_EQ(prims.size(), 1);
    }

    SUBCASE("Test searching empty stage")
    {
        UsdStageRefPtr stage = UsdStage::CreateInMemory();

        std::vector<std::string> paths;
        bool meshesOnly = true;
        std::vector<UsdPrim> prims = _resolveExpressionsToPrims(stage, paths, meshesOnly);
        CHECK_EQ(prims.size(), 0);
    }
}


TEST_CASE("Test Path Expression Predicate Functions")
{
    UsdStageRefPtr stage = testutils::_openStage("expressions.usda");

    std::vector<std::string> paths;
    size_t expected = 0;
    constexpr bool meshesOnly = false;
    constexpr bool reverse = false;
    Usd_PrimFlagsPredicate predicate = UsdPrimAllPrimsPredicate;

    SUBCASE("Test hasAPI")
    {
        paths = { "//{hasAPI:MaterialBindingAPI}" };
        expected = 3;
    }

    SUBCASE("Test not")
    {
        paths = { "//{not (hasAPI:MaterialBindingAPI)}" };
        expected = 13;
    }

    SUBCASE("Test Kind")
    {
        paths = { "//{kind:component}" };
        expected = 2;
    }

    SUBCASE("Test Specifier")
    {
        paths = { "//{specifier:over,class}" };
        expected = 2;
    }

    SUBCASE("Test Abstract")
    {
        paths = { "//{abstract}" };
        expected = 1;
    }

    SUBCASE("Test isa")
    {
        paths = { "//{isa:Xform}" };
        expected = 3;
    }

    SUBCASE("Test Compound")
    {
        paths = { "//{hasAPI:MaterialBindingAPI and not (isa:Sphere or isa:Cone)}" };
        expected = 1;
    }

    SUBCASE("Test name and predicate")
    {
        paths = { "//Cube*{hasAPI:MaterialBindingAPI}" };
        expected = 1;
    }

    // Resolve expression and assert expected value
    std::vector<UsdPrim> prims = _resolveExpressionsToPrims(stage, paths, meshesOnly, reverse, predicate);
    CHECK_EQ(expected, prims.size());
}


TEST_CASE("Test Resolving Prims")
{
    UsdStageRefPtr stage = UsdStage::CreateInMemory();

    // Assert a camera resolves to false
    UsdGeomCamera camera = UsdGeomCamera::Define(stage, SdfPath("/Camera"));
    CHECK_FALSE(_resolvePrim(stage, camera.GetPrim(), nullptr));

    // Assert an invalid prim resolves to false
    CHECK_FALSE(_resolvePrim(stage, UsdPrim(), nullptr));

    // Assert root path resolves to false
    CHECK_FALSE(_resolvePrim(stage, stage->GetPseudoRoot(), nullptr));

    UsdGeomMesh mesh = UsdGeomMesh::Define(stage, SdfPath("/Mesh"));
    std::set<UsdPrim> cache;

    // Assert a standard mesh resolves true
    CHECK(_resolvePrim(stage, mesh.GetPrim(), &cache));

    // Assert a subsequent check for a cached prim is false
    CHECK_FALSE(_resolvePrim(stage, mesh.GetPrim(), &cache));

    // Assert a prim underneath a camera resolves to false
    UsdGeomMesh subMesh = UsdGeomMesh::Define(stage, SdfPath("/Camera/Mesh"));
    CHECK_FALSE(_resolvePrim(stage, subMesh.GetPrim(), nullptr, true));

    // Assert a prim starting with "OmniverseKit_" does not resolve
    UsdGeomMesh omniKitMesh = UsdGeomMesh::Define(stage, SdfPath("/OmniverseKit_Persp"));
    CHECK_FALSE(_resolvePrim(stage, omniKitMesh.GetPrim(), nullptr));

    // Check that a light, and a mesh underneath a light, are not resolved.
    UsdLuxDiskLight light = UsdLuxDiskLight::Define(stage, SdfPath("/DiskLight"));
    UsdGeomMesh lightMesh = UsdGeomMesh::Define(stage, SdfPath("/DiskLight/Mesh"));
    CHECK_FALSE(_resolvePrim(stage, light.GetPrim(), nullptr, true));
    CHECK_FALSE(_resolvePrim(stage, lightMesh.GetPrim(), nullptr, true));

    // Check that the light mesh does resolve if checkParents is disabled
    CHECK(_resolvePrim(stage, lightMesh.GetPrim(), nullptr, false));
}


TEST_CASE("Test Primvar Path Optimization")
{

    UsdStageRefPtr stage = testutils::_openStage("validatePrimvars.usda");
    REQUIRE(stage);

    UsdPrim prim = stage->GetPrimAtPath(SdfPath("/World/CubeIndexable"));
    UsdGeomPrimvarsAPI primvarsAPI(prim);

    // Assert that initially this specific primvar is not indexed
    UsdGeomPrimvar primvar = primvarsAPI.GetPrimvar(TfToken("st"));
    CHECK_FALSE(primvar.IsIndexed());

    // Create JSON that targets an explicit primvar/attribute path to index, with
    // mode set to force-index
    std::string json = R"(
            {
                "mode": 2,
                "primvarPaths": ["/World/CubeIndexable.primvars:st"]
            }
    )";

    auto operation = SceneOptimizerCore::getInstance().getOperation("optimizePrimvars");
    REQUIRE(operation);

    ExecutionContext context = testutils::_getContext(stage);
    context.verbose = 1;

    JsValue args = PXR_NS::JsParseString(json);
    CHECK(operation->execute(&context, args.GetJsObject()).success);

    primvar = primvarsAPI.GetPrimvar(TfToken("st"));
    CHECK(primvar.IsIndexed());
}


TEST_CASE("Test CUDA availability from multiple threads")
{
    // This test verifies that isCudaAvailable() is thread-safe when called
    // from multiple threads concurrently using TBB parallel_for.
    // The function uses std::call_once internally to ensure initialization
    // happens only once, even under concurrent access.
    //
    // We dynamically load a separate shared library that wraps
    // isCudaAvailable(). Since the implementation lives in the core plugin,
    // all callers share the same static state regardless of which library
    // invokes the function.

    typedef bool (*PFN_testUtilsIsCudaAvailable)();

    carb::extras::LibraryHandle libHandle =
        carb::extras::loadLibrary("TestCudaUtils", carb::extras::fLibFlagMakeFullLibName);
    if (!libHandle)
    {
        CARB_LOG_WARN("Could not load TestCudaUtils: %s - skipping test",
                      carb::extras::getLastLoadLibraryError().c_str());
        return;
    }

    auto pfnTest = carb::extras::getLibrarySymbol<PFN_testUtilsIsCudaAvailable>(libHandle, "testUtilsIsCudaAvailable");
    if (!pfnTest)
    {
        CARB_LOG_WARN("Could not find testUtilsIsCudaAvailable - skipping test");
        carb::extras::unloadLibrary(libHandle);
        return;
    }

    constexpr size_t numThreads = 1000;
    std::vector<int> results(numThreads);
    std::atomic<size_t> callCount{ 0 };

    // Use TBB parallel_for to call the helper function from multiple threads
    // This will cause isCudaAvailable() to be called for the first time
    // from within the helper library, with multiple threads racing
    tbb::parallel_for(tbb::blocked_range<size_t>(0, numThreads),
                      [&results, &callCount, pfnTest](const tbb::blocked_range<size_t>& range)
                      {
                          for (size_t i = range.begin(); i != range.end(); ++i)
                          {
                              ++callCount;
                              // Each thread calls the helper function which calls isCudaAvailable()
                              results[i] = pfnTest();
                          }
                      });

    // Verify all threads executed
    CHECK_EQ(callCount, numThreads);

    // Check what isCudaAvailable() returns in the main test executable
    // (shares the same cached result via the core plugin)
    bool mainExecutableCudaAvailable = isCudaAvailable();

    // All threads should get the same result (either all true or all false)
    bool firstResult = results[0];
    bool allConsistent = true;
    size_t trueCount = 0;
    size_t falseCount = 0;

    for (size_t i = 0; i < numThreads; ++i)
    {
        if (results[i])
        {
            ++trueCount;
        }
        else
        {
            ++falseCount;
        }

        if (static_cast<bool>(results[i]) != firstResult)
        {
            allConsistent = false;
            CARB_LOG_ERROR("Thread %zu returned %d, but first thread returned %d",
                           i,
                           results[i] ? 1 : 0,
                           firstResult ? 1 : 0);
        }
    }

    CARB_LOG_INFO("Test results: %zu threads returned true, %zu returned false, main executable: %s",
                  trueCount,
                  falseCount,
                  mainExecutableCudaAvailable ? "true" : "false");

    // All threads must get consistent results
    CHECK(allConsistent);

    // If CUDA is available in the main executable, it should also be available
    // in the helper library (they're on the same system)
    if (mainExecutableCudaAvailable)
    {
        // This check will FAIL if there's a race condition that causes
        // the first call to return false when CUDA is actually available
        CHECK_EQ(firstResult, true);
        CHECK_EQ(trueCount, numThreads);
        CHECK_EQ(falseCount, 0);
    }

    // Call one more time from main thread to verify consistency
    CHECK_EQ(pfnTest(), firstResult);

    // Clean up
    carb::extras::unloadLibrary(libHandle);

    // The test passes if:
    // 1. No crashes or race conditions occurred during initialization
    // 2. All threads got consistent results
    // 3. If CUDA is available, all threads correctly detected it (no false negatives)
    // 4. std::call_once ensured initialization happened exactly once in the helper library
}


TEST_CASE("Test coalescer")
{
    // Basic smoke test; the coalescer is exercised more thoroughly via PluginStats unit tests.
    Coalescer coalescer([] {}, std::chrono::milliseconds(10));
    coalescer.wait();
    coalescer.cancel();
    coalescer.trigger();
    CHECK_EQ(coalescer.isActive(), false);
}
