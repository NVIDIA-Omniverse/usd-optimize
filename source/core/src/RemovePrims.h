// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"


namespace omni::scene::optimizer
{


/// The various methods that can be used to remove prims
enum class RemoveMethod
{
    eIgnore = 0, // Do nothing
    eDelete = 1, // Delete prims from the stage
    eDeactivate = 2, // Deactivate prims
    eHide = 3, // Hide prims
    eSetAttribute = 4, // Set a custom boolean attribute "hidden" accordingly
};


OMNI_SO_EXPORT
void _deletePrims(const PXR_NS::UsdStageWeakPtr& usdStage,
                  const std::vector<PXR_NS::UsdPrim>& prims,
                  bool deactivate = true,
                  bool deleteOvers = false);


/// Delete the specified prims.
OMNI_SO_EXPORT
void _deletePrims(const PXR_NS::UsdStageWeakPtr& usdStage, const std::vector<std::string>& primPaths);


/// Deactivate the specified prims.
///
/// \param prims List of prims to set inactive.
OMNI_SO_EXPORT
void _deactivatePrims(const std::vector<PXR_NS::UsdPrim>& prims);


/// Hide the specified prims.
///
/// \param prims List of prims to set not visible.
OMNI_SO_EXPORT
void _hidePrims(const std::vector<PXR_NS::UsdPrim>& prims);


/// Set the custom hidden attribute on the specified prims.
///
/// \param prims List of prims to set the attribute on
/// \param hidden Whether the hidden attribute will be set to true or false
OMNI_SO_EXPORT
void _setAttributeOnPrims(const std::vector<PXR_NS::UsdPrim>& prims, bool hidden = true);


/// Uses the specified method to remove the prims from the stage.
///
/// \param method How the prims should be removed from the stage
/// \param usdStage The Usd stage
/// \param removePrims The prims to remove
/// \param visiblePrims special argument that is only needed if RemoveMethod::eSetAttribute is used which specifies the
///                     prims that are visible so they can have their hidden attribute set to true.
OMNI_SO_EXPORT
void _removePrims(RemoveMethod method,
                  const PXR_NS::UsdStageWeakPtr& usdStage,
                  const std::vector<PXR_NS::UsdPrim>& removePrims,
                  const std::vector<PXR_NS::UsdPrim>& visiblePrims = {});


} // namespace omni::scene::optimizer
