// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/DisjointSet.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"

// C++
#include <fstream>


namespace omni::scene::optimizer
{

class DisjointSet::Impl
{
public:
    std::vector<int> m_parent;
    std::vector<int> m_rank;
    int m_maxIndex = 0;

    Impl(const int* indices, size_t count)
    {
        // Work out the max face vertex index.
        // In general we hope there are reused verts and so we don't need to reserve the full count, only enough
        // to hold the max face vertex index. Anecdotal testing suggested an extra loop to do this first saved time
        // because of the smaller memory allocation then required.
        for (size_t i = 0; i < count; ++i)
        {
            m_maxIndex = std::max(m_maxIndex, indices[i]);
        }

        // Resize to store the unique indices
        m_parent.resize(m_maxIndex + 1, 0);
        m_rank.resize(m_maxIndex + 1, 0);

        // Populate
        for (size_t i = 0; i < count; ++i)
        {
            m_parent[indices[i]] = indices[i];
        }
    }

    int findSet(int v)
    {
        // Path halving, at least updates _some_ parents, but not all
        // Still need to use findSet when accessing the data later
        while (m_parent[v] != v)
        {
            m_parent[v] = m_parent[m_parent[v]];
            v = m_parent[v];
        }

        return v;
    }

    void unionSet(int a, int b)
    {
        a = findSet(a);
        b = findSet(b);

        if (a != b)
        {
            if (m_rank[a] < m_rank[b])
            {
                std::swap(a, b);
            }

            m_parent[b] = a;

            if (m_rank[a] == m_rank[b])
            {
                m_rank[a]++;
            }
        }
    }
};


DisjointSet::DisjointSet(const int* indices, size_t count)
    : pImpl(new Impl(indices, count))
{
}


DisjointSet::~DisjointSet()
{
    delete pImpl;
}


int DisjointSet::findSet(int v)
{
    return pImpl->findSet(v);
};


void DisjointSet::unionSet(int x, int y)
{
    pImpl->unionSet(x, y);
};


} // namespace omni::scene::optimizer
