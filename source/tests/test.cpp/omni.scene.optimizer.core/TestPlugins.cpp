// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

// Test Utils
#include "../TestUtils.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Argument.h>
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/ParseJson.h>

// doctest
#include <doctest/doctest.h>

using namespace omni::scene::optimizer;
PXR_NAMESPACE_USING_DIRECTIVE


// Test enum
enum class TestEnum
{
    eFoo = 0,
    eBar = 3,
    eBaz = 8,
};

static constexpr const char* kTestPluginName = "testPlugin";
static constexpr const char* kTestPluginDisplayName = "Test Plugin";
static constexpr const char* kTestPluginDisplayGroup = "Test Group";
static constexpr const char* kTestPluginAuthor = "Mr Test Author";

// Struct to force as an invalid type
struct InvalidArgStruct
{
    int m_test;
};


class TestOperation : public Operation
{

public:
    explicit TestOperation()
        : Operation(kTestPluginName, kTestPluginDisplayName, "This is a test plugin, which does nothing useful")
    {

        // Random args
        addArgument("testString", "Test String", kDisplayTypeText, "Test Description", m_testString);
        addArgument("testInt", "Test Int", kDisplayTypeInt, "Debug/Test", m_testInt).setMin(1).setMax(200);

        // Floats
        addArgument("testFloat1", "Test Float 1", kDisplayTypeFloat, "Debug/Test", m_testFloat1);
        addArgument("testFloat2", "Test Float 2", kDisplayTypeFloat, "Debug/Test", m_testFloat2).setMin(1.0);
        addArgument("testFloat3", "Test Float 3", kDisplayTypeFloat, "Debug/Test", m_testFloat3).setMin(5);

        auto& testFloat4 = addArgument("testFloat4", "Test Float 4", kDisplayTypeFloat, "Debug/Test", m_testFloat4);
        testFloat4.setMin(1.0).setMax(2.0).setPrecision(20);

        addArgument("testDouble", "Test Double", kDisplayTypeFloat, "Debug/Test", m_testDouble).setVisible(false);

        // This argument should NOT be added, as we just added one with this name.
        // Verify later.
        addArgument("testDouble", "Test Double 2", kDisplayTypeFloat, "Debug/Test", m_testDouble);

        auto& arg = addArgument("testEnum", "Test Enum", kDisplayTypeEnum, "Debug/Test", m_testEnum);
        // Define enum <-> display name mappings
        arg.setEnumValues<TestEnum>({ { TestEnum::eBar, "Bar" }, { TestEnum::eBaz, "Baz" } });

        // Placeholder
        addArgument("testPlaceholder", "Test Placeholder", kDisplayTypePrimPaths, "Debug/Test", m_testPlaceholder)
            .setPlaceholder("test placeholder value");

        // Array based data
        addArgument("testPrimPaths",
                    "Prim Paths",
                    kDisplayTypePrimPaths,
                    "A list of prim paths to consider",
                    m_testPrimPaths);

        auto& argConditions =
            addArgument("testConditions", "Test Conditions", kDisplayTypeFloat, "Debug/Test", m_testConditions);
        argConditions.setEnableIf("testFloat1 == 1.0");
        argConditions.setVisibleIf("testFloat2 > 5.0");

        // This should not be added
        addArgument("testInvalid", "Test Invalid Arg", kDisplayTypeFloat, "Debug/Test", m_invalid);

        // Default array value with content
        addArgument("testDefaultArray", "Test Default Array", kDisplayTypePrimPaths, "Debug/Test", m_populatedPaths);

        addArgument("testDoubleArray", "Double Array", kDisplayTypeFloat, "Debug/Test", m_doubles);
        addArgument("testIntArray", "Int Array", kDisplayTypeInt, "Debug/Test", m_ints);

        addArgument("fail", "Fail", kDisplayTypeBool, "Debug/Test", m_fail);

        auto& argFloatPresets =
            addArgument("testFloatPresets", "Test Float Presets", kDisplayTypeFloatPresets, "Debug/Test", m_testFloatPreset);
        // Define float <-> display name mappings
        argFloatPresets.setFloatPresets({ { 1.1, "Bar" }, { -25.5, "Baz" } });

        addGroup("testGroup",
                 addArgument("testChild1", "Test Child 1", kDisplayTypeBool, "Debug/Test", m_testChild1),
                 addArgument("testChild2", "Test Child 2", kDisplayTypeBool, "Debug/Test", m_testChild2))
            .setEnableIf("True")
            .setVisibleIf("True");
    }

    OperationResult executeImpl() override
    {
        // Debug var to allow failing execution
        if (m_fail)
        {
            return { false };
        }

        // Test float clamping logic
        if (m_testFloat4 < 1.0 || m_testFloat4 > 2.0)
        {
            return { false };
        }

        // Test int clamping logic
        if (m_testInt < 1 || m_testInt > 200)
        {
            return { false };
        }

        return { true };
    };

    std::string getAuthor() const override
    {
        return kTestPluginAuthor;
    }

    SOPluginVersion getVersion() const override
    {
        return { 1, 2, 3 };
    }

    std::string getCategory() const override
    {
        return "Test";
    }

    std::string getDisplayGroup() const override
    {
        return kTestPluginDisplayGroup;
    }

    std::string m_testString;
    int m_testInt = 100;
    float m_testFloat1 = 5.0;
    float m_testFloat2 = 0.0;
    float m_testFloat3 = 0.0;
    float m_testFloat4 = 1.0;
    double m_testDouble = 0.0;
    TestEnum m_testEnum = TestEnum::eBar;
    std::vector<std::string> m_testPlaceholder;
    std::vector<std::string> m_testPrimPaths;
    std::vector<std::string> m_populatedPaths{ "/World/Foo", "/World/Bar" };
    float m_testConditions = 0.0;
    InvalidArgStruct m_invalid{};
    std::vector<double> m_doubles;
    std::vector<int> m_ints;
    bool m_fail = false;
    double m_testFloatPreset = 0.0;
    bool m_testChild1 = false;
    bool m_testChild2 = false;
};


static OperationUPtr getTestPlugin()
{
    return SceneOptimizerCore::getInstance().getOperation(kTestPluginName);
}


// Register plugin with SO
SO_PLUGIN_INIT(TestOperation);


TEST_CASE("Test Plugins")
{

    auto& core = SceneOptimizerCore::getInstance();

    // Init the test plugin.
    // Note the static init token in the macro takes care of ensuring this only
    // actually loads the plugin once. But we do it here to avoid static init
    // order issues (e.g. with the enum being registered).
    sceneOptimizerPluginInit();

    SUBCASE("Test plugins loaded")
    {
        // Static init should have caused the plugins to load already.
        // We expect, at the very least, that the Test Plugin has been loaded.
        CHECK_GT(core.getOperations().size(), 0);

        // Explicitly find Test Plugin
        const OperationUPtr plugin = getTestPlugin();
        REQUIRE(plugin);

        // Basic tests it's the right thing
        CHECK_EQ(plugin->getName(), kTestPluginName);
        CHECK_EQ(plugin->getDisplayName(), kTestPluginDisplayName);
        CHECK_EQ(plugin->getDisplayGroup(), kTestPluginDisplayGroup);
        CHECK_EQ(plugin->getAuthor(), kTestPluginAuthor);

        SOPluginVersion version = plugin->getVersion();
        CHECK_EQ(version.major, 1);
        CHECK_EQ(version.minor, 2);
        CHECK_EQ(version.rev, 3);
    }


    SUBCASE("Test arguments")
    {
        const OperationUPtr plugin = getTestPlugin();
        REQUIRE(plugin);

        const Argument* invalidArg = plugin->getArgument("invalidArg");
        CHECK_EQ(invalidArg, nullptr);

        // String arg
        const Argument* testString = plugin->getArgument("testString");
        REQUIRE(testString);
        CHECK_EQ(testString->getDisplayType(), kDisplayTypeText);
        CHECK_EQ(testString->getDisplayName(), "Test String");
        CHECK_EQ(testString->getDescription(), "Test Description");
        CHECK_FALSE(testString->getIsBool());

        // Int arg
        const Argument* testInt = plugin->getArgument("testInt");
        REQUIRE(testInt);
        CHECK_EQ(testInt->getDisplayType(), kDisplayTypeInt);
        CHECK(testInt->getDefaultValue().IsInt());
        CHECK_EQ(testInt->getDefaultValue().GetInt(), 100);

        // Check testFloat1
        const Argument* testFloat1 = plugin->getArgument("testFloat1");
        REQUIRE(testFloat1);
        CHECK_EQ(testFloat1->getDisplayType(), kDisplayTypeFloat);
        CHECK(!testFloat1->hasMin());
        CHECK(!testFloat1->hasMax());
        CHECK_EQ(testFloat1->getDefaultValue().GetReal(), 5.0);

        // Check testFloat2
        const Argument* testFloat2 = plugin->getArgument("testFloat2");
        REQUIRE(testFloat2);
        CHECK(testFloat2->hasMin());
        CHECK_EQ(testFloat2->getMin(), 1.0);

        // Check testFloat3
        const Argument* testFloat3 = plugin->getArgument("testFloat3");
        REQUIRE(testFloat3);
        CHECK(testFloat3->hasMin());
        CHECK_EQ(testFloat3->getMin(), 5.0);
        CHECK(!testFloat3->hasPrecision());
        CHECK_EQ(testFloat3->getPrecision(), 0);

        // Check testFloat4
        const Argument* testFloat4 = plugin->getArgument("testFloat4");
        REQUIRE(testFloat4);
        CHECK(testFloat4->hasMin());
        CHECK(testFloat4->hasMax());
        CHECK_EQ(testFloat4->getMin(), 1.0);
        CHECK_EQ(testFloat4->getMax(), 2.0);
        CHECK(testFloat4->hasPrecision());
        CHECK_EQ(testFloat4->getPrecision(), 20);

        // Check testEnum
        const Argument* testEnum = plugin->getArgument("testEnum");
        REQUIRE(testEnum);
        CHECK_EQ(testEnum->getDisplayType(), kDisplayTypeEnum);

        // Check enums.
        // The enum has three values, but we only include a couple for use in the arg.
        const std::vector<EnumMap>& enums = testEnum->getEnumValues();
        CHECK_EQ(enums.size(), 2);

        // Check expected mapped enum values
        CHECK_EQ(enums[0].second, "Bar");
        CHECK_EQ(enums[0].first, 3);
        CHECK_EQ(enums[1].second, "Baz");
        CHECK_EQ(enums[1].first, 8);

        // Check testPlaceholder
        const Argument* testPlaceholder = plugin->getArgument("testPlaceholder");
        REQUIRE(testPlaceholder);
        CHECK_EQ(testPlaceholder->getPlaceholder(), "test placeholder value");
        CHECK(testPlaceholder->getDefaultValue().IsArray());

        const Argument* testConditions = plugin->getArgument("testConditions");
        REQUIRE(testConditions);
        CHECK(!testConditions->getEnableIf().empty());
        CHECK(!testConditions->getVisibleIf().empty());

        const Argument* testInvalid = plugin->getArgument("testInvalid");
        REQUIRE(!testInvalid);

        const Argument* testDouble = plugin->getArgument("testDouble");
        REQUIRE(testDouble);
        CHECK(!testDouble->getVisible());
        // Assert this is Test Double, not Test Double 2
        CHECK_EQ(testDouble->getDisplayName(), "Test Double");

        // Check testFloatPresets
        const Argument* testPresets = plugin->getArgument("testFloatPresets");
        REQUIRE(testPresets);
        CHECK_EQ(testPresets->getDisplayType(), kDisplayTypeFloatPresets);

        // Check presets.
        const std::vector<FloatPreset>& presets = testPresets->getFloatPresets();
        CHECK_EQ(presets.size(), 2);

        // Check expected mapped float values
        CHECK_EQ(presets[0].second, "Bar");
        CHECK_EQ(presets[0].first, 1.1);
        CHECK_EQ(presets[1].second, "Baz");
        CHECK_EQ(presets[1].first, -25.5);

        // Check a group
        const Argument* testGroup = plugin->getArgument("testGroup");
        CHECK(testGroup->getIsGroup());

        // Check group-specific values
        auto _testGroup = dynamic_cast<const Group*>(testGroup);
        REQUIRE(_testGroup);
        CHECK_EQ(_testGroup->getSize(), 2);

        // Quick check of the JSON logic.
        JsValue json = _testGroup->toJson();
        CHECK(json.IsObject());
        JsObject _json = json.GetJsObject();
        CHECK_EQ(_json["group"].GetInt(), 2);
    }


    SUBCASE("Test argument (de)serialization")
    {
        const OperationUPtr plugin = getTestPlugin();
        REQUIRE(plugin);

        const std::vector<const Argument*> args = plugin->getArgs();

        // Assert there are args, so we know the serialization checks below will do something
        REQUIRE_GT(args.size(), 0);

        // For each arg, serialize it. Then deserialize to create a new arg. Finally
        // serialize the new arg and compare, to ensure the before/after serialization
        // strings match.
        for (const Argument* arg : args)
        {
            // We don't really support deserializing a group. They don't get passed
            // to operations to execute, they only appear when requesting args so
            // the UI can do something.
            if (arg->getIsGroup())
            {
                continue;
            }

            std::string serializedBefore = arg->serialize();
            Argument* testArg = Argument::deserialize(serializedBefore);
            std::string serializedAfter = testArg->serialize();
            CHECK_EQ(serializedBefore, serializedAfter);

            // Manually compare a couple of arg values directly as well
            CHECK_EQ(arg->getDescription(), testArg->getDescription());
            // JsValue::operator== appears to fail on matching values
            // CHECK_EQ(arg->getDefaultValue(), testArg->getDefaultValue());
            CHECK_EQ(arg->hasMin(), testArg->hasMin());
            CHECK_EQ(arg->hasMax(), testArg->hasMax());
        }

        // Test attempting to deserialize invalid JSON data
        std::string invalidJson = "abc";
        CHECK(!Argument::deserialize(invalidJson));
    }


    SUBCASE("Test unloading and reloading plugin")
    {
        REQUIRE(getTestPlugin());

        // Deregister
        core.deregisterOperation(kTestPluginName);
        REQUIRE(!getTestPlugin());

        // Re-register the plugin
        sceneOptimizerPluginInit();
        REQUIRE(getTestPlugin());

        size_t registeredBefore = core.getOperations().size();

        // Try and register again manually - should do nothing
        core.registerOperation(&sceneOptimizerOperationCreate<TestOperation>,
                               &sceneOptimizerOperationDelete<TestOperation>);

        size_t registeredAfter = core.getOperations().size();
        CHECK_EQ(registeredBefore, registeredAfter);
    }


    SUBCASE("Test default plugins")
    {
        // Assert that various default plugins were correctly loaded.
        REQUIRE(core.getOperation("optimizePrimvars"));
        REQUIRE(core.getOperation("optimizeTimeSamples"));
        REQUIRE(core.getOperation("utilityFunction"));
    }


    SUBCASE("Test invalid plugin")
    {
        auto plugin = core.getOperation("invalid plugin");
        CHECK_EQ(plugin, nullptr);
    }


    SUBCASE("Test hidden plugin")
    {
        // Test Plugin should be visible
        auto plugin1 = getTestPlugin();
        REQUIRE(plugin1);
        CHECK(plugin1->getVisible());

        // printStats should NOT be visible
        auto plugin2 = core.getOperation("printStats");
        REQUIRE(plugin2);
        CHECK(!plugin2->getVisible());
    }


    SUBCASE("Test invalid library")
    {
        core.loadPlugin("/foo/bar/invalid.so");

        // Assert no crash!
        CHECK(true);
    }

    SUBCASE("Test authors and versions")
    {
        const std::vector<std::string> operations = core.getOperations();
        CHECK(!operations.empty());

        // Verify operations all specify an author
        for (const auto& name : operations)
        {
            auto op = core.getOperation(name);
            REQUIRE(op != nullptr);
            CHECK(!op->getAuthor().empty());

            // Verify version is not -1
            SOPluginVersion version = op->getVersion();
            CHECK_NE(version.major, -1);
            CHECK_NE(version.minor, -1);
            CHECK_NE(version.rev, -1);
        }
    }
}


TEST_CASE("Test base virtual functions")
{
    // Init the test plugin.
    sceneOptimizerPluginInit();

    SUBCASE("Test base implementations")
    {
        OperationUPtr operation = getTestPlugin();
        REQUIRE(operation);

        CHECK_NOTHROW(operation->setUserData(nullptr));
    }
}


TEST_CASE("Test Plugin Execution")
{

    // Init the test plugin.
    sceneOptimizerPluginInit();

    SUBCASE("Test executing invalid plugin")
    {
        std::string json = R"(
[
    {
        "operation": "invalidOperation"
    }
]
)";
        // Simple test that an invalid operation does not crash
        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        CHECK(_parseJson(stage, json));
    }


    SUBCASE("Test executing test plugin")
    {

        UsdStageRefPtr stage = UsdStage::CreateInMemory();
        ExecutionContext context = testutils::_getContext(stage);

        OperationUPtr operation = getTestPlugin();
        REQUIRE(operation);

        // Verify passing no execution context fails
        CHECK_FALSE(operation->execute(nullptr, JsObject{}).success);

        // Verify passing a context succeeds
        CHECK(operation->execute(&context, JsObject{}).success);

        // Verify executing with fail=false is good
        CHECK(operation->execute(&context, JsParseString(R"({"fail": false})").GetJsObject()).success);

        // Verify executing with fail=true fails
        CHECK_FALSE(operation->execute(&context, JsParseString(R"({"fail": true})").GetJsObject()).success);

        // Test again with default args to ensure "fail" was reset
        CHECK(operation->execute(&context, JsObject{}).success);

        std::string json = R"(
        {
            "testIntArray": [0, 1, 2, 3, 4],
            "testDoubleArray": [1.0, 2.0, 3.0]
        })";

        // Test again with array arg values
        CHECK(operation->execute(&context, JsParseString(json).GetJsObject()).success);

        json = R"(
        {
            "testFloat4": 20.0,
            "testInt": 500
        })";

        // Test with float/int values that are outside the configured range
        CHECK(operation->execute(&context, JsParseString(json).GetJsObject()).success);
    }
}
