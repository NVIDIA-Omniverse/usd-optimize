// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/UsdIncludes.h"
#include "omni/scene.optimizer/core/Utils.h"

// TBB
#include <tbb/concurrent_vector.h>


namespace omni::scene::optimizer
{


#ifndef DOXYGEN_SHOULD_SKIP_THIS
template <typename C>
struct is_vector : std::false_type
{
};

template <typename T, typename A>
struct is_vector<std::vector<T, A>> : std::true_type
{
};

template <typename T, typename A>
struct is_vector<tbb::concurrent_vector<T, A>> : std::true_type
{
};

template <typename C>
struct is_set : std::false_type
{
};

template <typename T, typename A>
struct is_set<std::set<T, A>> : std::true_type
{
};
#endif

/// Convenience function to convert arbitrary types to JSON.
///
/// It's fairly barebones at the moment, but can convert standard types, or vectors of those types,
/// to a JSON value.
template <typename T>
PXR_NS::JsValue _toJson(const T& data)
{

    PXR_NS::JsValue result;

    if constexpr (is_vector<T>::value || is_set<T>::value)
    {
        PXR_NS::JsArray array;
        array.reserve(data.size());

        for (const auto& it : data)
        {
            PXR_NS::JsValue value = _toJson(it);
            array.emplace_back(value);
        }

        result = array;
    }
    else
    {
        result = PXR_NS::JsValue(data);
    }

    return result;
}


/// Specialization for UsdPrim to extract the path as a string and use that.
template <>
inline PXR_NS::JsValue _toJson(const PXR_NS::UsdPrim& data)
{
    return PXR_NS::JsValue(data.GetPrimPath().GetAsString());
}


/// Specialization for SdfPath to extract as a string and use that.
template <>
inline PXR_NS::JsValue _toJson(const PXR_NS::SdfPath& data)
{
    return PXR_NS::JsValue(data.GetAsString());
}


/// Specialization for UsdAttribute to extract path as a string
template <>
inline PXR_NS::JsValue _toJson(const PXR_NS::UsdAttribute& data)
{
    return PXR_NS::JsValue(data.GetPath().GetAsString());
}


/// Overload to allow a map keyed by string, to a supported JSON value
template <typename T>
inline PXR_NS::JsValue _toJson(const std::map<std::string, T>& data)
{
    PXR_NS::JsObject object;

    for (const auto& it : data)
    {
        object[it.first] = _toJson(it.second);
    }

    return object;
}


/// Convenience function to convert arbitrary data types to a JSON string.
/// The caller owns the returned memory.
template <typename T>
char* _toJsonStr(const T& data)
{
    PXR_NS::JsValue output = _toJson(data);
    return getCStr(JsWriteToString(output));
}


} // namespace omni::scene::optimizer
