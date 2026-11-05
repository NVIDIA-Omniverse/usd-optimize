// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include "usd_optimize/core/Defs.h"
#include "usd_optimize/core/UsdIncludes.h"

#include <map>
#include <string>
#include <vector>

namespace usd_optimize
{

/// Changes allowed when preparing a compatible group of visual meshes for merging.
/// Disabled changes leave differing values in place and report them as not reconciled.
struct MeshPreparationOptions
{
    bool generateNormals = true;
    bool generateUVs = false;
    bool makeDoubleSided = false;
    bool useDefaultPrimvarValues = false;
    std::map<PXR_NS::TfToken, std::string> primvarDefaults;
};

/// Counts of preparation changes and differences that could not be reconciled.
struct MeshPreparationResult
{
    size_t generated = 0;
    size_t renamed = 0;
    size_t filled = 0;
    size_t schemaFixed = 0;
    size_t notReconciled = 0;

    /// Count all preparation changes and unresolved differences.
    size_t total() const
    {
        return generated + renamed + filled + schemaFixed + notReconciled;
    }

    /// Accumulate preparation counts from another group.
    MeshPreparationResult& operator+=(const MeshPreparationResult& other)
    {
        generated += other.generated;
        renamed += other.renamed;
        filled += other.filled;
        schemaFixed += other.schemaFixed;
        notReconciled += other.notReconciled;
        return *this;
    }
};

/// Whether a mesh has an authored normals attribute or normals primvar.
USD_OPTIMIZE_EXPORT bool meshHasNormals(const PXR_NS::UsdPrim& prim);

/// Parse one constant primvar element. Return an empty value for an unsupported type or invalid value.
USD_OPTIMIZE_EXPORT PXR_NS::VtValue parsePrimvarDefault(const PXR_NS::SdfValueTypeName& typeName,
                                                        const std::string& text);

/// Prepare visual meshes in one compatible group in their stage's current edit target.
/// Resolve UV names without changing material readers, generate missing normals and UVs when requested,
/// fill missing primvars under the supplied policy, and optionally reconcile double-sided rendering.
/// Meshes that still lack normals or UVs are prepared separately from meshes that have them.
/// Throw std::invalid_argument if the context stage is missing or a prim is invalid, not a mesh,
/// or outside that stage, before editing any mesh.
/// An empty group makes no changes.
USD_OPTIMIZE_EXPORT MeshPreparationResult prepareMeshes(ExecutionContext& context,
                                                        const std::string& group,
                                                        const std::vector<PXR_NS::UsdPrim>& prims,
                                                        const MeshPreparationOptions& options);

} // namespace usd_optimize
