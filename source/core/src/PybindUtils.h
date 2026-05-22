// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Defs.h>

// Usd
#include <pxr/base/js/json.h>
#include <pxr/base/js/value.h>

// Pybind
#include <pybind11/pybind11.h>


namespace omni::scene::optimizer
{


/// Returns the ExecutionContext C struct from the Python wrapper class
inline ExecutionContext* _getExecutionContextFromPyWrapper(pybind11::object context)
{
    return context.attr("_impl").cast<ExecutionContext*>();
}


/// Creates and returns a Pybind Object for the given JsValue
inline pybind11::object _jsonValueToPybindObject(const PXR_NS::JsValue& value);


/// Creates and returns a Pybind Dict from a JsObject
inline pybind11::dict _jsonObjectToPybindDict(const PXR_NS::JsObject& object)
{
    pybind11::dict pyDict;

    for (const auto& it : object)
    {
        pyDict[pybind11::str(it.first)] = _jsonValueToPybindObject(it.second);
    }

    return pyDict;
}

/// Creates and returns a Pybind List from a JsArray
inline pybind11::list _jsonArrayToPybindList(const PXR_NS::JsArray& array)
{
    pybind11::list pyList;

    for (const PXR_NS::JsValue& value : array)
    {
        pyList.append(_jsonValueToPybindObject(value));
    }

    return pyList;
}


inline pybind11::object _jsonValueToPybindObject(const PXR_NS::JsValue& value)
{

    pybind11::object result{};

    if (value.IsString())
    {
        result = pybind11::str(value.GetString());
    }
    else if (value.IsBool())
    {
        result = pybind11::bool_(value.GetBool());
    }
    else if (value.IsInt())
    {
        result = pybind11::int_(value.GetInt());
    }
    else if (value.IsReal())
    {
        result = pybind11::float_(value.GetReal());
    }
    else if (value.IsArray())
    {
        result = _jsonArrayToPybindList(value.GetJsArray());
    }
    else if (value.IsObject())
    {
        result = _jsonObjectToPybindDict(value.GetJsObject());
    }

    return result;
}


inline pybind11::tuple _operationResultToPybindTuple(OperationResult& result)
{
    // Prepare result tuple.
    pybind11::object output(pybind11::cast(nullptr));

    // If there is output, convert the arbitrary JSON data to python objects
    if (result.output && strnlen(result.output, 1024))
    {
        PXR_NS::JsValue resultOutput = PXR_NS::JsParseString(result.output);
        if (resultOutput)
        {
            output = _jsonValueToPybindObject(resultOutput);
        }
        else
        {
            result.success = false;

            // set or extend the current error message to include the fact that we failed to parse the output as JSON
            std::string errorMessage;
            if (result.error != nullptr)
            {
                errorMessage = result.error + std::string(" | ");
                free(result.error);
                result.error = nullptr;
            }
            errorMessage += "Failed to parse operation output as JSON";
            const size_t len = errorMessage.length() + 1;
            result.error = (char*)malloc(sizeof(char) * len);
            strncpy(result.error, errorMessage.c_str(), len);
        }
    }

    return pybind11::make_tuple(result.success, result.error, output);
}

} // namespace omni::scene::optimizer
