// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>

namespace omni::scene::optimizer
{

/// @brief A generic iterator that wraps a raw pointer.
template <typename T>
class Iterator
{
public:
    /// @brief The value type (non-const) for the iterator.
    using value_type = std::remove_const_t<T>;
    /// @brief Signed integer type for distances between iterators.
    using difference_type = std::ptrdiff_t;
    /// @brief Pointer type returned by operator->.
    using pointer = T*;
    /// @brief Reference type returned by operator*.
    using reference = T&;
    /// @brief Iterator category.
    using iterator_category = std::forward_iterator_tag;

    /// @brief Constructs an iterator from a raw pointer.
    /// @param ptr Pointer to the element.
    explicit Iterator(pointer ptr)
        : m_ptr(ptr)
    {
    }

    /// @brief Conversion constructor for const iterators.
    /// @tparam U The underlying type.
    /// @param other Iterator to convert from.
    template <typename U, typename = std::enable_if_t<std::is_same_v<T, const U>>>
    Iterator(const Iterator<U>& other)
        : m_ptr(other.m_ptr)
    {
    }

    /// @brief Dereference operator.
    /// @return Reference to the element.
    reference operator*() const
    {
        return *m_ptr;
    }

    /// @brief Pointer operator.
    /// @return Pointer to the element.
    pointer operator->() const
    {
        return m_ptr;
    }

    /// @brief Prefix increment.
    /// @return Updated iterator.
    Iterator& operator++()
    {
        ++m_ptr;
        return *this;
    }

    /// @brief Postfix increment.
    /// @return Previous iterator value.
    Iterator operator++(int)
    {
        Iterator temp = *this;
        ++m_ptr;
        return temp;
    }

    /// @brief Equality comparison.
    friend bool operator==(const Iterator& a, const Iterator& b)
    {
        return a.m_ptr == b.m_ptr;
    }

    /// @brief Inequality comparison.
    friend bool operator!=(const Iterator& a, const Iterator& b)
    {
        return a.m_ptr != b.m_ptr;
    }

private:
    pointer m_ptr; ///< Underlying pointer.

    template <typename U>
    friend class Iterator;
};

} // namespace omni::scene::optimizer
