// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "MassProperties.h"

#include <pxr/base/tf/token.h>

#include <map>

namespace usd_optimize::physics_body_meshes
{

/// A source prim's body, output scope, and replacement state for visual and collision geometry.
struct SourceGeometry
{
    PXR_NS::SdfPath rigidBody;
    PXR_NS::SdfPath outputParent;
    bool collider = false;
    bool shape = false;
    bool visualApplicable = true;
    bool visualMerged = false;
    bool collisionMerged = false;
};

/// A prepared mesh output and the source geometry it replaces.
struct MergedGeometry
{
    PXR_NS::SdfPath output;
    PXR_NS::SdfPath rigidBody;
    PXR_NS::SdfPath outputParent;
    PXR_NS::SdfPathVector sources;
    bool collider = false;
    PXR_NS::TfToken approximation;
    bool approximationChanged = false;
    std::optional<MassProperties> mass;
};

/// Choose an unused child path without replacing existing geometry, including inactive prims.
PXR_NS::SdfPath uniqueChildPath(const PXR_NS::UsdStageWeakPtr& stage,
                                const PXR_NS::SdfPath& parent,
                                const PXR_NS::TfToken& name);

/// Author prepared outputs and replace their source contributions in the current edit target.
/// Retain body and joint frames. Roll back the layer if any replacement cannot take effect.
void authorResults(const PXR_NS::UsdStageWeakPtr& stage,
                   const PXR_NS::UsdStageWeakPtr& preparedStage,
                   const std::map<PXR_NS::SdfPath, SourceGeometry>& sources,
                   const std::vector<MergedGeometry>& merged,
                   const PXR_NS::SdfPathVector& localizedInstances);

} // namespace usd_optimize::physics_body_meshes
