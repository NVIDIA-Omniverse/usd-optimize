// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/Argument.h"

// C
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

// Primary keys
static constexpr const char* KEY_NAME = "name";
static constexpr const char* KEY_DISPLAY_NAME = "displayName";
static constexpr const char* KEY_DISPLAY_TYPE = "displayType";
static constexpr const char* KEY_DESCRIPTION = "description";
static constexpr const char* KEY_DEFAULT_VALUE = "defaultValue";
static constexpr const char* KEY_GROUP = "group";
static constexpr const char* KEY_IS_FLOAT = "isFloat";
static constexpr const char* KEY_ARRAY_TYPE = "arrayType";

// Extended/metadata keys
static constexpr const char* KEY_ENUMS = "enums";
static constexpr const char* KEY_JOIN_NEXT = "joinNext";
static constexpr const char* KEY_JOIN_NEXT_DESC = "joinNextDescription";
static constexpr const char* KEY_MAX = "max";
static constexpr const char* KEY_MIN = "min";
static constexpr const char* KEY_METADATA = "metadata";
static constexpr const char* KEY_PLACEHOLDER = "placeholder";
static constexpr const char* KEY_PRECISION = "precision";
static constexpr const char* KEY_STEP = "step";
static constexpr const char* KEY_VISIBLE = "visible";
static constexpr const char* KEY_FLOAT_PRESETS = "floatPresets";

// Expressions
static constexpr const char* KEY_ENABLEIF = "enableIf";
static constexpr const char* KEY_VISIBLEIF = "visibleIf";


namespace omni::scene::optimizer
{

FloatPresetsVector _getDefaultLinearUnitsPresets()
{
    return {
        { 0.0, "none" },          { 0.01, "centimeters" },  { 1.0, "meters" },
        { 0.001, "millimeters" }, { 1000.0, "kilometers" }, { 0.0254, "inches" },
        { 0.3048, "feet" },       { 0.9144, "yards" },      { 1609.344, "miles" },
        { 1e-9, "nanometers" },   { 1e-6, "micrometers" },  { 9.4607304725808e15, "lightyears" },
    };
}

class Argument::Impl
{
public:
    std::string m_name;
    std::string m_displayName;
    std::string m_displayType;
    std::string m_description;
    JsValue m_defaultValue;
    JsObject m_metadata;
    std::vector<EnumMap> m_enums;
    std::vector<FloatPreset> m_floatPresets;

    void* m_target = nullptr;
    bool m_isFloat = false;
    ArgumentArrayType m_arrayType = ArgumentArrayType::eNone;

    Impl() = default;

    Impl(const std::string& name,
         const std::string& displayName,
         const std::string& displayType,
         const std::string& description,
         const JsValue& defaultValue,
         void* target)
        : m_name(name)
        , m_displayName(displayName)
        , m_displayType(displayType)
        , m_description(description)
        , m_defaultValue(defaultValue)
        , m_target(target)
    {
    }

    JsValue getValueForKey(const std::string& key) const
    {
        auto findIt = m_metadata.find(key);
        if (findIt != m_metadata.end())
        {
            return findIt->second;
        }

        return JsValue{};
    }


    std::string getStringForKey(const std::string& key) const
    {
        const JsValue& value = getValueForKey(key);
        if (value.IsNull() || !value.IsString())
        {
            return std::string{};
        }

        return value.GetString();
    }


    JsValue toJson() const
    {
        JsObject result;

        result[KEY_NAME] = JsValue(m_name);
        result[KEY_DISPLAY_NAME] = JsValue(m_displayName);
        result[KEY_DISPLAY_TYPE] = JsValue(m_displayType);
        result[KEY_DESCRIPTION] = JsValue(m_description);
        result[KEY_DEFAULT_VALUE] = m_defaultValue;
        result[KEY_IS_FLOAT] = JsValue(m_isFloat);
        result[KEY_ARRAY_TYPE] = JsValue(static_cast<int>(m_arrayType));

        // Nest metadata, for easy deserialization. Also always write, even if empty.
        JsObject metadata;
        metadata.insert(m_metadata.begin(), m_metadata.end());
        result[KEY_METADATA] = metadata;

        // Enum mappings - optional
        if (!m_enums.empty())
        {
            JsArray enums;
            for (const auto& [name, val] : m_enums)
            {
                JsArray _enum = { JsValue(val), JsValue(name) };
                enums.emplace_back(_enum);
            }

            result[KEY_ENUMS] = enums;
        }

        // Float presets - optional
        if (!m_floatPresets.empty())
        {
            JsArray maps;
            for (const auto& [name, val] : m_floatPresets)
            {
                JsArray preset = { JsValue(val), JsValue(name) };
                maps.emplace_back(preset);
            }

            result[KEY_FLOAT_PRESETS] = maps;
        }

        return result;
    }


    static Argument::Impl* deserialize(const std::string& serialized)
    {
        const JsValue& json = JsParseString(serialized);
        if (json.IsNull() || !json.IsObject())
        {
            return nullptr;
        }

        JsObject jsonObject = json.GetJsObject();

        // Create argument instance with basic values
        // Note this creates an argument with a nullptr for the target - this cannot be used
        // to actually set values on a class.
        auto argument = new Impl(jsonObject[KEY_NAME].GetString(),
                                 jsonObject[KEY_DISPLAY_NAME].GetString(),
                                 jsonObject[KEY_DISPLAY_TYPE].GetString(),
                                 jsonObject[KEY_DESCRIPTION].GetString(),
                                 jsonObject[KEY_DEFAULT_VALUE],
                                 nullptr);

        // Enum mappings
        auto findEnums = jsonObject.find(KEY_ENUMS);
        if (findEnums != jsonObject.end())
        {
            // Enums are an array of arrays (fake tuples)
            const JsArray& enums = findEnums->second.GetJsArray();
            for (const JsValue& value : enums)
            {
                const JsArray& _enum = value.GetJsArray();
                argument->m_enums.emplace_back(_enum[1].GetInt(), _enum[0].GetString());
            }
        }

        // Float presets
        auto findFloatPresets = jsonObject.find(KEY_FLOAT_PRESETS);
        if (findFloatPresets != jsonObject.end())
        {
            // Float presets are an array of arrays (fake tuples)
            const JsArray& presets = findFloatPresets->second.GetJsArray();
            for (const JsValue& value : presets)
            {
                const JsArray& preset = value.GetJsArray();
                argument->m_floatPresets.emplace_back(preset[1].GetReal(), preset[0].GetString());
            }
        }

        // is float
        auto findIsFloat = jsonObject.find(KEY_IS_FLOAT);
        if (findIsFloat != jsonObject.end() && findIsFloat->second.IsBool())
        {
            argument->m_isFloat = jsonObject[KEY_IS_FLOAT].GetBool();
        }

        // array type
        auto findArrayType = jsonObject.find(KEY_ARRAY_TYPE);
        if (findArrayType != jsonObject.end() && findArrayType->second.IsInt())
        {
            argument->m_arrayType = static_cast<ArgumentArrayType>(jsonObject[KEY_ARRAY_TYPE].GetInt());
        }

        // Metadata assumed to always exist, even if empty
        argument->m_metadata = jsonObject[KEY_METADATA].GetJsObject();

        return argument;
    }


    void addEnumValue(int value, const std::string& name)
    {
        m_enums.emplace_back(value, name);
    }
};

Argument::Argument()
    : pImpl(new Impl())
{
}


Argument::Argument(const std::string& name,
                   const std::string& displayName,
                   const std::string& displayType,
                   const std::string& description,
                   const JsValue& defaultValue,
                   void* target)
    : pImpl(new Impl(name, displayName, displayType, description, defaultValue, target))
{
}


Argument::Argument(Impl* impl)
    : pImpl(impl)
{
}


Argument::~Argument()
{
    delete pImpl;
}


std::string Argument::getName() const
{
    return pImpl->m_name;
}


std::string Argument::getDisplayName() const
{
    return pImpl->m_displayName;
}


JsValue Argument::getDefaultValue() const
{
    return pImpl->m_defaultValue;
}


std::string Argument::getDisplayType() const
{
    return pImpl->m_displayType;
}


std::string Argument::getDescription() const
{
    return pImpl->m_description;
}


std::vector<EnumMap> Argument::getEnumValues() const
{
    return pImpl->m_enums;
}


void Argument::setFloatPresets(const std::vector<FloatPreset>& presets)
{
    pImpl->m_floatPresets = presets;
}


std::vector<FloatPreset> Argument::getFloatPresets() const
{
    return pImpl->m_floatPresets;
}


std::string Argument::getEnableIf() const
{
    return pImpl->getStringForKey(KEY_ENABLEIF);
}


Argument& Argument::setEnableIf(const std::string& expression)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_ENABLEIF, JsValue(expression)));
    return *this;
}


std::string Argument::getVisibleIf() const
{
    return pImpl->getStringForKey(KEY_VISIBLEIF);
}


Argument& Argument::setVisibleIf(const std::string& expression)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_VISIBLEIF, JsValue(expression)));
    return *this;
}


bool Argument::hasMin() const
{
    return !pImpl->getValueForKey(KEY_MIN).IsNull();
}


Argument& Argument::setMin(float minValue)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_MIN, JsValue(minValue)));
    return *this;
}


float Argument::getMin() const
{
    const JsValue& value = pImpl->getValueForKey(KEY_MIN);
    return value.IsNull() ? NAN : value.GetReal();
}


bool Argument::hasMax() const
{
    return !pImpl->getValueForKey(KEY_MAX).IsNull();
}


Argument& Argument::setMax(float maxValue)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_MAX, JsValue(maxValue)));
    return *this;
}


float Argument::getMax() const
{
    const JsValue& value = pImpl->getValueForKey(KEY_MAX);
    return value.IsNull() ? NAN : value.GetReal();
}


bool Argument::hasStep() const
{
    return !pImpl->getValueForKey(KEY_STEP).IsNull();
}


float Argument::getStep() const
{
    const JsValue& value = pImpl->getValueForKey(KEY_STEP);
    return value.IsNull() ? NAN : value.GetReal();
}


Argument& Argument::setStep(float value)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_STEP, JsValue(value)));
    return *this;
}


bool Argument::hasPrecision() const
{
    return !pImpl->getValueForKey(KEY_PRECISION).IsNull();
}


Argument& Argument::setPrecision(int value)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_PRECISION, JsValue(value)));
    return *this;
}


int Argument::getPrecision() const
{
    const JsValue& value = pImpl->getValueForKey(KEY_PRECISION);
    return value.IsNull() ? 0 : value.GetInt();
}


std::string Argument::getPlaceholder() const
{
    return pImpl->getStringForKey(KEY_PLACEHOLDER);
}


Argument& Argument::setPlaceholder(const std::string& placeholder)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_PLACEHOLDER, JsValue(placeholder)));
    return *this;
}


bool Argument::getVisible() const
{
    const auto& value = pImpl->getValueForKey(KEY_VISIBLE);
    if (!value.IsNull() && value.IsBool())
    {
        return value.GetBool();
    }

    // Default to true
    return true;
}


Argument& Argument::setVisible(bool value)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_VISIBLE, JsValue(value)));
    return *this;
}


Argument& Argument::setJoinNext(const std::string& name, const std::string& description)
{
    pImpl->m_metadata.insert(std::make_pair(KEY_JOIN_NEXT, name));
    pImpl->m_metadata.insert(std::make_pair(KEY_JOIN_NEXT_DESC, description));
    return *this;
}


std::string Argument::serialize() const
{
    return JsWriteToString(toJson());
}


JsValue Argument::toJson() const
{
    return pImpl->toJson();
}


Argument* Argument::deserialize(const std::string& serialized)
{
    auto impl = Impl::deserialize(serialized);

    if (impl == nullptr)
    {
        return nullptr;
    }

    return new Argument(impl);
}


void Argument::addEnumValue(int value, const std::string& name)
{
    pImpl->addEnumValue(value, name);
}

void* Argument::getTarget() const
{
    return pImpl->m_target;
}


void Argument::setIsFloat(bool value)
{
    pImpl->m_isFloat = value;
}


bool Argument::getIsFloat() const
{
    return pImpl->m_isFloat;
}


bool Argument::getIsBool() const
{
    return pImpl->m_defaultValue.IsBool();
}


bool Argument::getIsGroup() const
{
    if (dynamic_cast<const Group*>(this))
    {
        return true;
    }

    return false;
}


void Argument::setArrayType(omni::scene::optimizer::ArgumentArrayType type)
{
    pImpl->m_arrayType = type;
}


ArgumentArrayType Argument::getArrayType() const
{
    return pImpl->m_arrayType;
}


// Group::Impl isn't derived, it's just a separate
// impl to hold the child arguments
class Group::Impl
{
public:
    void addArgument(Argument* argument)
    {
        m_arguments.push_back(argument);
    }

    std::vector<Argument*> m_arguments;
};


Group::Group(const std::string& name)
    : pGroupImpl(new Impl)
{
    pImpl->m_name = name;
    pImpl->m_defaultValue = JsValue(0);
    pImpl->m_target = nullptr;
}


Group::~Group()
{
    delete pGroupImpl;
}


void Group::addArgument(Argument* argument)
{
    pGroupImpl->addArgument(argument);
}


size_t Group::getSize() const
{
    return pGroupImpl->m_arguments.size();
}


std::vector<Argument*> Group::getChildren() const
{
    return pGroupImpl->m_arguments;
}


JsValue Group::toJson() const
{
    JsObject result;

    result[KEY_NAME] = JsValue(getName());
    result[KEY_GROUP] = JsValue(pGroupImpl->m_arguments.size());

    // Copy certain metadata
    JsObject metadata;

    // EnableIf
    std::string enableIf = getEnableIf();
    if (!enableIf.empty())
    {
        metadata[KEY_ENABLEIF] = JsValue(enableIf);
    }

    // VisibleIf
    std::string visibleIf = getVisibleIf();
    if (!visibleIf.empty())
    {
        metadata[KEY_VISIBLEIF] = JsValue(visibleIf);
    }

    // Join Next
    JsValue joinNext = pImpl->getValueForKey(KEY_JOIN_NEXT);
    if (joinNext)
    {
        metadata[KEY_JOIN_NEXT] = JsValue(joinNext);
    }

    // Join Next Desc
    JsValue joinNextDesc = pImpl->getValueForKey(KEY_JOIN_NEXT_DESC);
    if (joinNextDesc)
    {
        metadata[KEY_JOIN_NEXT_DESC] = JsValue(joinNextDesc);
    }

    result[KEY_METADATA] = metadata;

    return result;
}


} // namespace omni::scene::optimizer
