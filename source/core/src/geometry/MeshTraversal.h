// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#pragma once

// USD
#include <pxr/usd/usd/prim.h>
#include <pxr/usd/usdPhysics/tokens.h>

#include <algorithm>

namespace usd_optimize
{

/// Whether a valid prim has an effective PhysicsRigidBodyAPI or PhysicsArticulationRootAPI.
/// Does not inspect ancestors.
inline bool _isPhysicsBodyRoot(const PXR_NS::UsdPrim& prim)
{
    // Use effective schemas, including built-in and auto-applied APIs. In USD 26.08, each HasAPI call copies
    // this vector; reading it once by reference avoids those copies and a second search.
    const PXR_NS::TfTokenVector& schemas = prim.GetAppliedSchemas();
    return std::any_of(schemas.begin(),
                       schemas.end(),
                       [](const PXR_NS::TfToken& schema)
                       {
                           return schema == PXR_NS::UsdPhysicsTokens->PhysicsArticulationRootAPI ||
                                  schema == PXR_NS::UsdPhysicsTokens->PhysicsRigidBodyAPI;
                       });
}

} // namespace usd_optimize
