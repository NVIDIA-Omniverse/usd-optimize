// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Usd Optimize Core
#include <usd_optimize/core/Operation.h>

// C++
#include <string>
#include <vector>


namespace usd_optimize
{

/// Prepares and merges geometry within individual rigid bodies while preserving articulation and joint connections.
///
/// Analysis discovers candidate geometry and output scopes without editing the stage.
class MergePhysicsBodyMeshesOperation : public Operation
{

public:
    /// Register the operation arguments and defaults.
    MergePhysicsBodyMeshesOperation();

    /// Return the operation behavior, argument semantics, and supported geometry.
    std::string getDocumentation() const override;

    /// Return the plugin author.
    std::string getAuthor() const override;

    /// Return the plugin version.
    UsdOptimizePluginVersion getVersion() const override;

    /// Return the report category.
    std::string getCategory() const override;

    /// Return the UI display group.
    std::string getDisplayGroup() const override;

    /// Support read-only discovery of candidate meshes and their output scopes.
    bool getSupportsAnalysis() const override;

protected:
    /// Prepare and merge compatible geometry within the selected physics body scopes.
    OperationResult executeImpl() override;

    /// Identify candidate meshes without testing compatibility or editing the stage.
    OperationResult executeAnalysisImpl() override;

private:
    /// Action when colliders have different approximation methods or settings.
    enum class CollisionApproximationFallback
    {
        eKeepSeparate = 0,
        eConvexHull = 1,
    };

    /// Whether selected geometry inside instances may become local geometry.
    enum class InstanceMode
    {
        ePreserve = 0,
        eMakeLocal = 1,
    };

    std::vector<std::string> m_paths;
    bool m_considerMaterials = false;
    bool m_generateNormals = true;
    bool m_generateUVs = false;
    std::vector<std::string> m_primvarDefaults;
    bool m_makeDoubleSided = false;
    bool m_mergeColliders = false;
    CollisionApproximationFallback m_collisionApproximationFallback = CollisionApproximationFallback::eKeepSeparate;
    InstanceMode m_instanceMode = InstanceMode::ePreserve;
};


} // namespace usd_optimize
