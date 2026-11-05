// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <usd_optimize/core/Defs.h>
#include <usd_optimize/core/geometry/MeshPreparation.h>

namespace usd_optimize::physics_body_meshes
{

/// Resolved traversal starts and user choices for merging geometry within discovered physics body scopes.
struct MergeOptions
{
    PXR_NS::SdfPathVector startPaths;
    MeshPreparationOptions preparation;
    bool considerMaterials = false;
    bool mergeColliders = false;
    bool convexHullFallback = false;
    bool makeLocal = false;
};

/// Prepare compatible groups on working stages, then author successful merges into the input edit target.
/// The operation supplies a valid stage and resolved traversal starts.
/// Source layers are read through composition. Changes are confined to the current edit target.
OperationResult mergeMeshes(const PXR_NS::UsdStageWeakPtr& stage, ExecutionContext* context, const MergeOptions& options);

} // namespace usd_optimize::physics_body_meshes
