// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/OmniOperation.h>

// C++
#include <string>
#include <vector>

namespace omni::scene::optimizer
{

/// The type of definition for the Clip Box itself
enum class ClipBoxDefinition
{
    eByAABB = 0, // By giving a world-space AABB (min, max)
    eByPrim = 1, // By giving a prim, and taking that prim's boundingbox as world-space AABB
};

// Option to ignore one side of the clip box.
enum class IgnoreClipBoxSide
{
    eDisabled = 0, // ignore no sides
    eNegX = 1, // ignore negative X side
    ePosX = 2, // ignore positive X side
    eNegY = 3, // ignore negative Y side
    ePosY = 4, // ignore positive Y side
    eNegZ = 5, // ignore negative Z side
    ePosZ = 6 // ignore positive Z side
};

enum class PartiallyIntersectedPrims
{
    eKeep = 0,
    eKeepIntersection = 1,
    eDiscard = 2
};

enum class KeepGeometry
{
    eInside = 0,
    eOutside = 1
};

enum class ClipMode
{
    eInsideKeep = 0,
    eInsideCutMesh = 1,
    eInsideDiscard = 2,
    eOutsideKeep = 3,
    eOutsideDiscard = 4
};

/// Makes meshes manifold using OmniMesh.
class BoxClipOperation : public OmniOperation
{
public:
    BoxClipOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

protected:
    bool meshesOnly() override
    {
        return false;
    }

    void preProcessPrims(std::vector<PXR_NS::UsdPrim>&) override;

    /// Process
    ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context&) override;

    /// Pre
    OperationResult executePre() override;

private:
    std::array<double, 3> m_min;
    std::array<double, 3> m_max;
    ClipBoxDefinition m_clipBoxDef;
    std::string m_clipBoxPrimPath;
    IgnoreClipBoxSide m_ignoreClipBoxSide;
    PartiallyIntersectedPrims m_partiallyIntersectedPrims;
    KeepGeometry m_keepGeometry;
    ClipMode m_clipMode;

    /// These get resolved based on the other parameters
    std::array<double, 3> m_resolvedMin;
    std::array<double, 3> m_resolvedMax;
    PXR_NS::SdfPath m_clipBoxPrimSdfPath;
};

} // namespace omni::scene::optimizer
