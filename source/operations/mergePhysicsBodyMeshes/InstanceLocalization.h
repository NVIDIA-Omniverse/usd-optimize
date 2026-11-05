// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "PathIssue.h"

#include <usd_optimize/core/UsdIncludes.h>
#include <vector>

namespace usd_optimize::physics_body_meshes
{

/// Localize selected instances on the working stage only when their composed content stays unchanged.
/// Validate against the original composition because flattening omits ignored opinions below instances.
/// Append unsafe instances and their changed object paths to skipped; return safe paths for later authoring.
PXR_NS::SdfPathVector makeInstancesLocal(const PXR_NS::UsdStageWeakPtr& source,
                                         const PXR_NS::UsdStageWeakPtr& working,
                                         const PXR_NS::SdfPathVector& startPaths,
                                         std::vector<PathIssue>& skipped);

} // namespace usd_optimize::physics_body_meshes
