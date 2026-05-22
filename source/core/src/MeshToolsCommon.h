// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"

// Mesh Tools
#include <MeshTools/ClashDetector.h>
#include <MeshTools/Stage.h>


namespace omni::scene::optimizer
{

OMNI_SO_EXPORT
std::shared_ptr<MeshTools::Stage> GetStage(PXR_NS::UsdStageRefPtr usdStage,
                                           const std::vector<PXR_NS::UsdPrim>& prims,
                                           bool checkTransparency = false);

OMNI_SO_EXPORT
PXR_NS::GfMatrix4d _getTransformFromToFuzzy(const PXR_NS::VtArray<PXR_NS::GfVec3f>& sourcePoints,
                                            const PXR_NS::VtArray<int> sourceIndices,
                                            const PXR_NS::VtArray<int> sourceFaceSizes,
                                            const PXR_NS::VtArray<PXR_NS::GfVec3f>& targetPoints,
                                            const PXR_NS::VtArray<int> targetIndices,
                                            const PXR_NS::VtArray<int> targetFaceSizes);

OMNI_SO_EXPORT
void GetStageMeshDescriptors(std::vector<MeshTools::ClashMeshDescriptor>& meshDescriptors,
                             PXR_NS::SdfPathVector& paths,
                             PXR_NS::UsdStageRefPtr usdStage,
                             const std::vector<PXR_NS::UsdPrim>& prims);

} // namespace omni::scene::optimizer
