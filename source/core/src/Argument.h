// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"

// USD
#include <pxr/base/js/json.h>

// C++
#include <vector>


namespace omni::scene::optimizer
{

/// Argument Display Types
static constexpr const char* kDisplayTypeAttributeList = "attributeList";
static constexpr const char* kDisplayTypeBool = "bool";
static constexpr const char* kDisplayTypeCode = "code";
static constexpr const char* kDisplayTypeEnum = "enum";
static constexpr const char* kDisplayTypeFloat = "float";
static constexpr const char* kDisplayTypeFloatArray = "floatArray";
static constexpr const char* kDisplayTypeFloatPresets = "floatPresets";
static constexpr const char* kDisplayTypeFloatSlider = "floatSlider";
static constexpr const char* kDisplayTypeInt = "int";
static constexpr const char* kDisplayTypeIntSlider = "intSlider";
static constexpr const char* kDisplayTypePrimPath = "primPath";
static constexpr const char* kDisplayTypePrimPaths = "primPaths";
static constexpr const char* kDisplayTypeText = "text";
static constexpr const char* kDisplayTypeTextList = "textList";

// Typedefs
using EnumMap = std::pair<int, std::string>;
using FloatPreset = std::pair<double, std::string>;
using FloatPresetsVector = std::vector<FloatPreset>;

/// Describes the type of data an array-based argument is expected to hold
enum class ArgumentArrayType
{
    eNone = 0,
    eIntArray = 1,
    eStringArray = 2,
    eDoubleArray = 3,
    eFloatArray = 4,
};

/// Returns the default list of float presets for the `UsdGeomLinearUnits` definitions.
OMNI_SO_EXPORT
FloatPresetsVector _getDefaultLinearUnitsPresets();

/// Generic argument class.
///
/// This class represents an argument that can configure how an operation works.
/// It contains basic info like its name, but also more UI-specific info like its display
/// type, things that are intended primarily to drive a user interface.
///
/// The idea is that Arguments can store enough info that all configuration for an operation
/// can be specified within the plugin, with no extra configuration required elsewhere to build
/// a rich user interface.
class OMNI_SO_EXPORT Argument
{

public:
    /// Construct an invalid argument
    Argument();

    /// Construct a valid argument with primary attributes
    Argument(const std::string& name,
             const std::string& displayName,
             const std::string& displayType,
             const std::string& description,
             const PXR_NS::JsValue& defaultValue,
             void* target = nullptr);

    /// Disable copying.
    Argument(const Argument&) = delete;
    void operator=(const Argument&) = delete;

    /// Destructor
    virtual ~Argument();

    /// Returns the name of the argument.
    std::string getName() const;

    /// Returns the display name of the argument.
    std::string getDisplayName() const;

    /// Get the display type of the argument.
    ///
    /// Some argument types may require a specific UI widget. For example, a list of strings
    /// may want be a "primPaths" widget in the UI which allows more useful controls for
    /// manipulating paths of prims (drag/drop, etc).
    std::string getDisplayType() const;

    /// Get the description of this argument.
    std::string getDescription() const;

    /// Get the default value.
    PXR_NS::JsValue getDefaultValue() const;

    /// Returns whether this Argument has a minimum value.
    bool hasMin() const;

    /// Returns the minimum value for this Argument, if one is available.
    ///
    /// \return The minimum value or NaN
    float getMin() const;

    /// Set the minimum value for this Argument.
    Argument& setMin(float minValue);

    /// Returns whether this Argument has a maximum value.
    bool hasMax() const;

    /// Returns the maximum value for this Argument, if one is available.
    ///
    /// \return The maximum value or NaN
    float getMax() const;

    /// Set the maximum value for this Argument.
    Argument& setMax(float maxValue);

    /// Returns whether this Argument has a step value.
    ///
    /// Step can be used to configure stepping in a UI slider.
    bool hasStep() const;

    /// Returns the step value for this Argument, if one is available.
    ///
    /// \return The step value or NaN
    float getStep() const;

    /// Set the step value for this Argument.
    Argument& setStep(float value);

    /// Returns whether this Argument has a precision value specified.
    bool hasPrecision() const;

    /// Gets the precision value.
    ///
    /// Arguments can specify an optional "precision" that indicates to a user interface
    /// how many digits of precision are required. This can be used to ensure a UI can
    /// correctly display very small values, if necessary.
    ///
    /// \return The precision, or NaN
    int getPrecision() const;

    /// Set a precision value for this argument.
    Argument& setPrecision(int precision);

    /// Get the placeholder text.
    ///
    /// This is used by the user interface to provide a placeholder text value in some widgets. For
    /// example, in a prim paths widget this can help the user understand what adding prims to it
    /// might do.
    ///
    /// \return The placeholder text, or an empty string.
    std::string getPlaceholder() const;

    /// Set the placeholder text.
    ///
    /// Sets the placeholder text to display in a UI if there is no value set.
    ///
    /// \param placeholder The placeholder text.
    /// \return
    Argument& setPlaceholder(const std::string& placeholder);

    /// Sets the mappings from enum value types to their string display name
    ///
    /// This info is intended to help the UI display readable names for options.
    /// Can be called using initializer list syntax:
    /// `arg.setEnumValues<TestEnum>({{TestEnum::eFoo, "Foo"}, {TestEnum::eBar, "Bar"}});`
    ///
    /// \param valueToNameMap vector of pairs that map enum value to string display name
    template <typename T>
    void setEnumValues(const std::vector<std::pair<T, std::string>>& valueToNameMap)
    {
        for (const auto& mapping : valueToNameMap)
        {
            addEnumValue(static_cast<int>(mapping.first), mapping.second);
        }
    }

    /// Get the mapping of enum values and names.
    std::vector<EnumMap> getEnumValues() const;

    /// Sets the mappings from float value types to their string display name
    ///
    /// This info is intended to help the UI display readable names for options.
    /// Can be called using initializer list syntax:
    /// `arg.setFloatPresets({{0.2, "Foo"}, {1.8, "Bar"}});`
    ///
    void setFloatPresets(const std::vector<FloatPreset>& presets);

    /// Get the mapping of float values and names.
    std::vector<FloatPreset> getFloatPresets() const;

    /// Get the enableIf expression for this argument.
    ///
    /// This expression is used to instruct a UI how to control the enabled state of a widget for this argument.
    /// It is expected to be a simple expression that can be eval'd by the user interface. It should evaluate to
    /// True in order to enable the widget or False in order to disable it. The expression will have access to
    /// local variables for each of the other current argument values.
    ///
    /// The current Scene Optimizer user interface is written in python, so it is assumed to be python.
    ///
    /// For example, to enable this argument if the value of another matches some condition you would use:
    /// \code{.py}
    /// otherArg > 100
    /// \endcode
    ///
    /// \return Either the expression or an empty string
    std::string getEnableIf() const;

    /// Set the enableIf expression
    Argument& setEnableIf(const std::string& expression);

    /// Get the visibleIf expression for this argument.
    ///
    /// This function is similar to \ref getEnableIf(), but controls whether the argument is visible or hidden in the
    /// UI, rather than whether it is enabled or disabled. Otherwise it functions in the same way as enableIf.
    ///
    /// \return Either the expression or an empty string
    std::string getVisibleIf() const;

    /// Set the visibleIf expression
    Argument& setVisibleIf(const std::string& expression);

    /// Returns whether this argument is visible.
    ///
    /// This controls whether a user interface should display the argument, or whether
    /// it should be hidden (for example, for internal/debug arguments).
    ///
    /// \return Whether the argument is visible
    bool getVisible() const;

    /// Set whether the argument is visible.
    Argument& setVisible(bool value);

    /// Group this argument with the next argument.
    ///
    /// For the purposes of a user interface, treat this argument and the next (and any others that continue
    /// the chain) as a group of arguments. For example, a user interface may want to group all of the arguments
    /// together, such as for individual R, G and B colors.
    ///
    /// The first argument that specifies joinWith will have its name and description used in the UI. The
    /// following arguments should have the same join name in order to consider them a group.
    ///
    /// \param name The name of the group
    /// \param description An overall description for the group of arguments
    /// \return A reference to this argument for chaining functions
    Argument& setJoinNext(const std::string& name, const std::string& description);

    /// Serialize this argument to a JSON string.
    std::string serialize() const;

    /// Create an Argument from a serialized representation.
    static Argument* deserialize(const std::string& serialized);

    /// Serialize this argument to a JSON object.
    virtual PXR_NS::JsValue toJson() const;

    /// Get the target address for populating this argument at execution time.
    void* getTarget() const;

    /// Specify this argument uses floats, not doubles
    ///
    /// Because JSON only supports double and not float we need to be careful to
    /// cast to the correct type when setting argument values.
    void setIsFloat(bool value);

    /// Return whether this arguments data type is float rather than double.
    bool getIsFloat() const;

    /// Return whether this arguments data type is bool.
    bool getIsBool() const;

    /// Return whether this argument is a group.
    ///
    /// Groups are for organization, so while they are "Arguments" they are
    /// treated a bit differently in various places.
    bool getIsGroup() const;

    /// Set the expected type of data an array should hold.
    void setArrayType(ArgumentArrayType type);

    /// Get the type of data this arguments array is expected to hold.
    ArgumentArrayType getArrayType() const;

protected:
    class Impl;
    Impl* pImpl;

private:
    explicit Argument(Impl* impl);

    void addEnumValue(int value, const std::string& name);
};


/// A group of arguments
///
/// Used to organize logical groups of arguments together, e.g. for display
/// in a UI.
///
/// A group supports conditional expressions (visibleIf, enableIf) so that
/// groups of arguments can conveniently be hidden or disabled, but
/// otherwise is really just a container.
class OMNI_SO_EXPORT Group : public Argument
{
public:
    /// Constructor
    ///
    /// \param name Unique group identifier
    explicit Group(const std::string& name);

    /// Destructor
    ~Group() override;

    /// Add a new argument to this group
    void addArgument(Argument* argument);

    /// Get the number of children the group has
    size_t getSize() const;

    /// Get the children of this group
    ///
    /// \return The children
    std::vector<Argument*> getChildren() const;

    /// Custom serialization
    PXR_NS::JsValue toJson() const override;

private:
    class Impl;
    Impl* pGroupImpl;
};


} // namespace omni::scene::optimizer
