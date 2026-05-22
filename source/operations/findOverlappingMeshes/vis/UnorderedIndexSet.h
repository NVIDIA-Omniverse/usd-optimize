// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "Iterator.h"

#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace omni::scene::optimizer
{

/// @brief Container for indices with O(1) operations.
template <typename IndexT>
class UnorderedIndexSet
{
    static_assert(std::is_unsigned_v<IndexT>);

public:
    UnorderedIndexSet()
        : m_max_size((IndexT)std::min(m_indices.max_size(), (size_t)std::numeric_limits<IndexT>().max()))
        , m_size(0)
    {
    }

    size_t size() const
    {
        return m_size;
    }

    bool empty() const
    {
        return !m_size;
    }

    size_t capacity() const
    {
        return m_indices.size();
    }

    void reserve(size_t new_capacity)
    {
        const size_t old_capacity = capacity();
        if (new_capacity <= old_capacity)
        {
            return;
        }

        if (new_capacity > static_cast<size_t>(m_max_size))
        {
            throw std::runtime_error("UnorderedIndexSet<>::reserve: Maximum capacity reached, cannot allocate more.");
        }

        // Allocate indices
        m_indices.resize(new_capacity);
        m_indices.resize(capacity()); // Does not allocate, just makes entire capacity accessible
        std::iota(m_indices.begin() + old_capacity, m_indices.end(), (IndexT)old_capacity);

        // Allocate ranks
        m_ranks.resize(capacity());
        std::iota(m_ranks.begin() + old_capacity, m_ranks.end(), (IndexT)old_capacity);
    }

    void clear()
    {
        m_size = 0;
    }

    void deterministic_clear()
    {
        clear();
        m_indices.resize(0);
        m_ranks.resize(0);
    }

    bool insert(IndexT index)
    {
        if (contains(index))
        {
            return false; // Already inserted
        }

        // Allocate more if needed
        reserve((size_t)index + 1);

        // Perform the insertion
        swap_with_end(index);
        ++m_size;

        return true;
    }

    IndexT insertUnique()
    {
        // Allocate more if needed
        if (size() >= capacity())
        {
            if (size() > capacity())
            {
                throw std::runtime_error(
                    "UnorderedIndexSet<>::insertUnique: Internal error, size somehow exceeds capacity.");
            }
            reserve(size() + 1);
        }

        return m_indices[m_size++];
    }

    bool erase(IndexT index)
    {
        if (!contains(index))
        {
            return false; // Not inserted
        }

        --m_size;
        swap_with_end(index);

        return true;
    }

    // pop() removes the back element more efficiently than erase()
    // Returns the index that was at the back() position
    IndexT pop()
    {
        if (!m_size)
        {
            throw std::out_of_range("UnorderedIndexSet<>::back: container is empty.");
        }

        return m_indices[--m_size];
    }

    bool contains(IndexT index) const
    {
        return index < capacity() && m_ranks[index] < size();
    }

    IndexT& operator[](IndexT pos)
    {
        if (pos >= size())
        {
            throw std::out_of_range("UnorderedIndexSet<>::operator(): index out of range.");
        }

        return m_indices[pos];
    }

    const IndexT& operator[](IndexT pos) const
    {
        if (pos >= size())
        {
            throw std::out_of_range("UnorderedIndexSet<>::operator(): index out of range.");
        }

        return m_indices[pos];
    }

    // Iterators will iterate over the indices in no particular order
    using iterator = Iterator<IndexT>;
    using const_iterator = Iterator<const IndexT>;

    iterator begin()
    {
        return iterator(m_indices.data());
    }
    const_iterator begin() const
    {
        return const_iterator(m_indices.data());
    }
    iterator end()
    {
        return iterator(m_indices.data() + m_size);
    }
    const_iterator end() const
    {
        return const_iterator(m_indices.data() + m_size);
    }

private:
    void swap_with_end(IndexT index)
    {
        const IndexT index_rank = m_ranks[index];
        const IndexT replace_index = m_indices[m_size];
        m_indices[m_size] = index;
        m_indices[index_rank] = replace_index;
        m_ranks[index] = (IndexT)m_size;
        m_ranks[replace_index] = index_rank;
    }

    const IndexT m_max_size;
    size_t m_size;
    std::vector<IndexT> m_indices;
    std::vector<IndexT> m_ranks;
};

} // namespace omni::scene::optimizer
