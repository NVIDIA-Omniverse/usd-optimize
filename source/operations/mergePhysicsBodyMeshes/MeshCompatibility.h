// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "PathIssue.h"

#include <usd_optimize/core/geometry/MeshPreparation.h>
#include <usd_optimize/core/geometry/VirtualMesh.h>

namespace usd_optimize::physics_body_meshes
{

/// Bake effective inherited values before geometry leaves its source hierarchy.
void resolveInheritedPrimvars(const PXR_NS::UsdPrim& prim);

/// Explain geometry that the static visual or collision merger cannot preserve.
std::string geometrySkipReason(const PXR_NS::UsdPrim& prim, bool collider = false);

/// Explain unreadable primvars on meshes or shapes, including interpolation size mismatches on meshes.
std::string primvarDataSkipReason(const PXR_NS::UsdPrim& prim);

/// Explain mesh topology or primvar data that cannot be merged safely.
std::string meshDataSkipReason(const PXR_NS::UsdPrim& prim, bool collider = false);

/// Explain relative transforms that cannot preserve geometry and physics placement.
std::string transformSkipReason(const PXR_NS::UsdPrim& prim, const PXR_NS::UsdPrim& parent);

/// Whether groups can still receive preparation or must be ready to merge.
enum class MeshMergePhase
{
    ePreparation,
    eMerge,
};

/// Group geometry with the generic bucketer plus the operation's property and physics compatibility rules.
/// Preparation groups can differ only in properties covered by the requested preparation options.
/// Report unmatched merge candidates and any differing additional compatibility values in their scope.
std::vector<VirtualMesh> bucketMeshes(const PXR_NS::UsdStageWeakPtr& stage,
                                      ExecutionContext* context,
                                      const std::vector<PXR_NS::UsdPrim>& prims,
                                      const PXR_NS::SdfPath& parent,
                                      const MeshPreparationOptions& preparation,
                                      bool considerMaterials,
                                      MeshMergePhase phase,
                                      std::vector<PathIssue>& skipped,
                                      const std::map<PXR_NS::SdfPath, PXR_NS::VtDictionary>& physicsValues = {});

/// Preserve agreed prim metadata, non-physics APIs, and opaque custom arrays from a prepared working stage.
void copyMergeProperties(const PXR_NS::UsdPrim& source, const PXR_NS::UsdPrim& output);

} // namespace usd_optimize::physics_body_meshes
