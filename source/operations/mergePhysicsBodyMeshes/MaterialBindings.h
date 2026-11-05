// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <array>
#include <map>
#include <usd_optimize/core/UsdIncludes.h>

namespace usd_optimize::physics_body_meshes
{

/// Resolve the physics materials used by a mesh's faces, including inherited and collection bindings.
/// Match USD physics parsing: bindings require MaterialBindingAPI and materials require PhysicsMaterialAPI.
PXR_NS::SdfPathVector physicsMaterials(const PXR_NS::UsdPrim& prim);

/// Preserve all-purpose, full, and preview face assignments through the existing mesh merger.
/// Working materials identify combinations of material purposes. Only restored bindings reach the input stage.
class MaterialBindings
{
public:
    /// Preserve render or physics bindings while preparing geometry on the working stage.
    explicit MaterialBindings(const PXR_NS::UsdStageWeakPtr& workingStage, bool physics = false);

    /// Resolve all candidates before changing their binding hierarchy, then bake the resolved bindings locally.
    void bake(const std::vector<PXR_NS::UsdPrim>& prims);

    /// Replace a working mesh's bindings with one temporary material per combination of face assignments.
    void normalize(const PXR_NS::UsdPrim& prim);

    /// Replace temporary bindings on a new output mesh and its subsets with the resolved original materials.
    void restore(const PXR_NS::UsdPrim& output) const;

private:
    using Assignment = std::array<PXR_NS::SdfPath, 3>;

    struct OriginalBindings
    {
        Assignment base;
        std::vector<std::pair<PXR_NS::VtIntArray, Assignment>> subsets;
    };

    /// Find or create the temporary material representing one resolved assignment.
    PXR_NS::SdfPath _material(const Assignment& assignment);

    PXR_NS::UsdStageWeakPtr m_stage;
    bool m_physics;
    PXR_NS::SdfPath m_root;
    std::map<Assignment, PXR_NS::SdfPath> m_materials;
    std::map<PXR_NS::SdfPath, Assignment> m_assignments;
    std::map<PXR_NS::SdfPath, OriginalBindings> m_originalBindings;
};

} // namespace usd_optimize::physics_body_meshes
