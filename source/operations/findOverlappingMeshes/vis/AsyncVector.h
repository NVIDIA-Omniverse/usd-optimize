// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// C++
#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

namespace omni::scene::optimizer
{

/// @brief Thread-safe vector for asynchronous operations.
template <typename T>
class AsyncVector
{
    static_assert(std::is_trivially_destructible_v<T>,
                  "AsyncVector::clear() does not invoke destructors; T must be trivially destructible.");

public:
    /// @brief Reserves space for elements.
    /// @param bufferSize The buffer size.
    void reserve(size_t bufferSize)
    {
        std::lock_guard<std::mutex> lock(m_resizeMutex);
        if (bufferSize > capacity_internal())
        {
            reserve_internal(bufferSize);
        }
    }

    /// @brief Clears the vector.
    void clear()
    {
        std::lock_guard<std::mutex> lock(m_resizeMutex);
        m_size.store(0);
    }

    /// @brief Checks if empty.
    /// @return True if empty.
    bool empty() const
    {
        return !m_size.load();
    }

    /// @brief Gets the size.
    /// @return The size.
    size_t size() const
    {
        return m_size.load();
    }

    /// @brief Gets the capacity.
    /// @return The capacity.
    size_t capacity() const
    {
        std::lock_guard<std::mutex> lock(m_resizeMutex);
        return capacity_internal();
    }

    /// @brief Gets the data pointer.
    /// @return The data pointer.
    const T* data() const
    {
        return m_buffer.data();
    }

    /// @brief Access operator.
    /// @param index The index.
    /// @return Reference to element.
    const T& operator[](size_t index) const
    {
        if (index >= size())
        {
            throw std::out_of_range("AsyncVector::operator[]: accessing data beyond valid range.");
        }

        return m_buffer[index];
    }

    /// @brief Access operator.
    /// @param index The index.
    /// @return Reference to element.
    T& operator[](size_t index)
    {
        if (index >= size())
        {
            throw std::out_of_range("AsyncVector::operator[]: accessing data beyond valid range.");
        }

        return m_buffer[index];
    }

    /// @brief Appends elements.
    /// @param elements The elements.
    /// @param N Number of elements.
    void append(const T* elements, size_t N)
    {
        std::lock_guard<std::mutex> lock(m_resizeMutex);
        const size_t index = m_size.load();
        const size_t requiredCapacity = index + N;
        if (requiredCapacity > capacity_internal())
        {
            reserve_internal(std::max(requiredCapacity, 2 * capacity_internal()));
        }
        std::copy(elements, elements + N, m_buffer.begin() + index);
        m_size.store(index + N);
    }

    /// @brief Appends a single element.
    /// @param element The element.
    void append(const T& element)
    {
        append(&element, 1);
    }

    // Iterators will iterate over the elements in the order they were added
    using iterator = T*; ///< Iterator type.
    using const_iterator = const T*; ///< Const iterator type.

    /// @brief Begin iterator.
    /// @return Begin iterator.
    iterator begin()
    {
        return m_buffer.data();
    }
    /// @brief Begin const iterator.
    /// @return Begin const iterator.
    const_iterator begin() const
    {
        return m_buffer.data();
    }
    /// @brief End iterator.
    /// @return End iterator.
    iterator end()
    {
        return m_buffer.data() + size();
    }
    /// @brief End const iterator.
    /// @return End const iterator.
    const_iterator end() const
    {
        return m_buffer.data() + size();
    }

private:
    /// @brief Reserves internal buffer, lock-free.
    /// @param bufferSize The buffer size.
    void reserve_internal(size_t bufferSize)
    {
        m_buffer.resize(bufferSize);
        m_buffer.resize(m_buffer.capacity()); // Maximize buffer
    }

    /// @brief Returns vector capacity, lock-free.
    /// @return Capacity of this vector.
    size_t capacity_internal() const
    {
        return m_buffer.size();
    }

    std::vector<T> m_buffer; ///< The buffer.
    std::atomic<size_t> m_size{ 0 }; ///< The size.
    mutable std::mutex m_resizeMutex; ///< Resize mutex.
};

} // namespace omni::scene::optimizer
