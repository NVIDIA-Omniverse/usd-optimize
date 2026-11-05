// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Usd Optimize Core
#include <usd_optimize/core/Operation.h>
#include <usd_optimize/core/UsdIncludes.h>
#include <usd_optimize/core/geometry/SpatialClustering.h>


namespace usd_optimize
{

/// Conform Meshes Operation
///
/// Makes the meshes inside each merge boundary compatible with each other so that merge can combine them.
class ConformMeshesOperation : public Operation
{

public:
    /// Constructor
    explicit ConformMeshesOperation();

    /// Get the documentation string for this plugin.
    std::string getDocumentation() const override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    UsdOptimizePluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

private:
    std::vector<std::string> m_meshPrimPaths;
    bool m_generateNormals = true;
    bool m_generateUVs = true;
    std::vector<std::string> m_primvarDefaults;

    // Finds merge boundaries as merge does
    SpatialClustering m_clustering;
};


} // namespace usd_optimize
