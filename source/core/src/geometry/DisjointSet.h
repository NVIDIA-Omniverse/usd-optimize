// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"


namespace omni::scene::optimizer
{

/// Disjoint-set/union-find object
/// Implements union via union by rank
class OMNI_SO_EXPORT DisjointSet
{
public:
    /// Initialize a new set.
    ///
    /// Creates a new set based on the specified faceVertexIndices. Note that the face vertex indices are loaded
    /// as-is, so the face vertex index is its position in this array.
    ///
    /// Always use \p findSet to query the actual value, as it by design it is "eventually" compact, but won't
    /// necessarily be after the unions are applied.
    ///
    /// \param indices Pointer to faceVertexIndex int array
    /// \param count Number of indices in the array
    DisjointSet(const int* indices, size_t count);

    ~DisjointSet();

    int findSet(int v);

    void unionSet(int x, int y);

private:
    class Impl;

    Impl* pImpl;
};

} // namespace omni::scene::optimizer
