// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//


#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>
#include <omni/scene.optimizer/core/geometry/VirtualMesh.h>

// C++
#include <random>


namespace omni::scene::optimizer
{


/// Utility operation used to generate a Usd scene using reference data from the input stage
class GenerateSceneOperation : public Operation
{
public:
    /// Constructor
    explicit GenerateSceneOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Returns whether or not this operation is visible.
    bool getVisible() const override;

protected:
    /// Entry point
    OperationResult executeImpl() override;

private:
    int m_seed = 123456789;
    std::vector<std::string> m_referenceMeshPaths;
    std::string m_generatedMeshPath;
    int m_meshCount = 32;
    bool m_uniformLayout = false;
    bool m_2DLayout = false;
    float m_layoutSpacing = 200.0f;
    float m_uniqueMeshPercent = 0.5f;
    bool m_scaleUniqueMeshes = true;
    float m_clusteredPercent = 0.75f;
    int m_numClusters = 16;
    std::vector<std::string> m_materialPaths;

    // Mersenne Twister random number generator
    std::mt19937 m_random;

    // Performs generation of unique meshes
    void generateMeshes(std::vector<VirtualMesh>& refMeshes, PXR_NS::UsdGeomXformCache& xformCache);
};


} // namespace omni::scene::optimizer
