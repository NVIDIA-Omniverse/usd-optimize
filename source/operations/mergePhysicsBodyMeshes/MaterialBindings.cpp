// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MaterialBindings.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdPhysics/materialAPI.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

#include <set>
#include <stdexcept>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (physics)
);
// LCOV_EXCL_STOP
// clang-format on

namespace
{

using Assignment = std::array<SdfPath, 3>;

/// Resolve the render-purpose or physics material paths bound to a prim.
Assignment _assignment(const UsdPrim& prim, bool physics)
{
    const UsdShadeMaterialBindingAPI api(prim);
    if (physics)
    {
        // The USD physics parser ignores bindings unless this prim has MaterialBindingAPI.
        const auto material = api ? api.ComputeBoundMaterial(_tokens->physics) : UsdShadeMaterial{};
        return { material && material.GetPrim().HasAPI<UsdPhysicsMaterialAPI>() ? material.GetPath() : SdfPath{}, {}, {} };
    }
    /// Return the material bound for one purpose, or an empty path when unbound.
    const auto materialPath = [&](const TfToken& purpose)
    {
        const auto material = api.ComputeBoundMaterial(purpose);
        return material ? material.GetPath() : SdfPath{};
    };
    return { materialPath(UsdShadeTokens->allPurpose),
             materialPath(UsdShadeTokens->full),
             materialPath(UsdShadeTokens->preview) };
}

/// Unbound physics subsets use the collider's base material; render subsets retain their own assignment.
Assignment _subsetAssignment(const UsdPrim& subset, const Assignment& base, bool physics)
{
    const auto assignment = _assignment(subset, physics);
    return physics && assignment[0].IsEmpty() ? base : assignment;
}

/// Resolve each face before moving it out of its original binding hierarchy.
std::vector<Assignment> _faceAssignments(const UsdPrim& prim, bool physics)
{
    VtIntArray counts;
    UsdGeomMesh(prim).GetFaceVertexCountsAttr().Get(&counts);
    const auto base = _assignment(prim, physics);
    std::vector<Assignment> faces(counts.size(), base);
    for (const auto& subset : UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets())
    {
        VtIntArray indices;
        subset.GetIndicesAttr().Get(&indices);
        const auto material = _subsetAssignment(subset.GetPrim(), base, physics);
        for (const auto index : indices)
        {
            if (index < 0 || static_cast<size_t>(index) >= faces.size())
            {
                throw std::runtime_error("Invalid material subset indices: " + subset.GetPath().GetAsString());
            }
            faces[index] = material;
        }
    }
    return faces;
}

/// Remove local material-binding relationships before authoring resolved assignments.
void _clearBindings(UsdPrim prim)
{
    for (const auto& relationship : prim.GetAuthoredRelationships())
    {
        const auto& name = relationship.GetName().GetString();
        if (name == "material:binding" || TfStringStartsWith(name, "material:binding:"))
        {
            prim.RemoveProperty(relationship.GetName());
        }
    }
}

/// Author one resolved purpose binding. Unbound purposes stay unauthored: empty bindings don't block inheritance.
void _bind(const UsdPrim& prim, const SdfPath& material, const TfToken& purpose)
{
    if (material.IsEmpty())
    {
        return;
    }
    auto api = UsdShadeMaterialBindingAPI::Apply(prim);
    if (!api.Bind(UsdShadeMaterial::Get(prim.GetStage(), material), UsdShadeTokens->weakerThanDescendants, purpose))
    {
        throw std::runtime_error("Cannot preserve material binding on " + prim.GetPath().GetAsString());
    }
}

/// Replace local bindings with the resolved assignment for the required material purposes.
void _bindAssignment(const UsdPrim& prim, const Assignment& assignment, bool physics)
{
    _clearBindings(prim);
    if (physics && assignment[0].IsEmpty())
    {
        // An empty binding cannot override stronger ancestor bindings. The physics parser requires this API.
        prim.RemoveAPI<UsdShadeMaterialBindingAPI>();
        return;
    }
    _bind(prim, assignment[0], UsdShadeTokens->allPurpose);
    if (physics)
    {
        _bind(prim, assignment[0], _tokens->physics);
    }
    else
    {
        _bind(prim, assignment[1], UsdShadeTokens->full);
        _bind(prim, assignment[2], UsdShadeTokens->preview);
    }
}

} // namespace

/// Return the distinct physics materials assigned to the mesh faces.
SdfPathVector physicsMaterials(const UsdPrim& prim)
{
    std::set<SdfPath> materials;
    for (const auto& face : _faceAssignments(prim, true))
    {
        materials.insert(face[0]);
    }
    return { materials.begin(), materials.end() };
}

/// Choose the working stage and the material purposes to preserve.
MaterialBindings::MaterialBindings(const UsdStageWeakPtr& workingStage, bool physics)
    : m_stage(workingStage)
    , m_physics(physics)
{
}

/// Capture candidate face bindings before replacing their inherited assignments locally.
void MaterialBindings::bake(const std::vector<UsdPrim>& prims)
{
    std::set<SdfPath> ancestors;
    for (const auto& prim : prims)
    {
        OriginalBindings original;
        original.base = _assignment(prim, m_physics);
        for (const auto& subset : UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets())
        {
            VtIntArray indices;
            subset.GetIndicesAttr().Get(&indices);
            original.subsets.emplace_back(indices, _subsetAssignment(subset.GetPrim(), original.base, m_physics));
        }
        m_originalBindings.emplace(prim.GetPath(), std::move(original));
        for (auto parent = prim.GetParent(); parent && !parent.IsPseudoRoot(); parent = parent.GetParent())
        {
            ancestors.insert(parent.GetPath());
        }
    }
    for (const auto& path : ancestors)
    {
        _clearBindings(m_stage->GetPrimAtPath(path));
    }
    for (const auto& prim : prims)
    {
        const auto& original = m_originalBindings.at(prim.GetPath());
        _bindAssignment(prim, original.base, m_physics);
        const auto subsets = UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets();
        for (size_t index = 0; index != subsets.size(); ++index)
        {
            _bindAssignment(subsets[index].GetPrim(), original.subsets.at(index).second, m_physics);
        }
    }
}

/// Find or create a temporary material for one combination of purpose bindings.
SdfPath MaterialBindings::_material(const Assignment& assignment)
{
    const auto found = m_materials.find(assignment);
    if (found != m_materials.end())
    {
        return found->second;
    }
    if (m_root.IsEmpty())
    {
        for (size_t suffix = 0;; ++suffix)
        {
            m_root = SdfPath("/_PhysicsMeshMergeMaterials" + std::to_string(suffix));
            if (!m_stage->GetPrimAtPath(m_root))
            {
                break;
            }
        }
        m_stage->DefinePrim(m_root, UsdGeomTokens->Scope);
    }
    const auto path = m_root.AppendChild(TfToken("Material_" + std::to_string(m_materials.size())));
    UsdShadeMaterial::Define(m_stage, path);
    m_materials.emplace(assignment, path);
    m_assignments.emplace(path, assignment);
    return path;
}

/// Encode face assignments as temporary materials understood by the generic mesh merger.
void MaterialBindings::normalize(const UsdPrim& prim)
{
    VtIntArray counts;
    UsdGeomMesh(prim).GetFaceVertexCountsAttr().Get(&counts);
    const auto& original = m_originalBindings.at(prim.GetPath());
    std::vector<Assignment> faces(counts.size(), original.base);
    for (const auto& [indices, assignment] : original.subsets)
    {
        for (const auto index : indices)
        {
            if (index < 0 || static_cast<size_t>(index) >= faces.size())
            {
                throw std::runtime_error("Invalid material subset indices: " + prim.GetPath().GetAsString());
            }
            faces[index] = assignment;
        }
    }
    std::map<Assignment, VtIntArray> assignments;
    for (size_t index = 0; index != faces.size(); ++index)
    {
        assignments[faces[index]].push_back(static_cast<int>(index));
    }
    const auto subsets = UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets();
    for (const auto& subset : subsets)
    {
        m_stage->RemovePrim(subset.GetPath());
    }
    _clearBindings(prim);
    if (assignments.empty())
    {
        return;
    }
    auto api = UsdShadeMaterialBindingAPI::Apply(prim);
    _bind(prim, _material(assignments.begin()->first), UsdShadeTokens->allPurpose);
    if (assignments.size() > 1)
    {
        size_t index = 0;
        for (const auto& [assignment, indices] : assignments)
        {
            const auto subset = api.CreateMaterialBindSubset(TfToken("Material_" + std::to_string(index++)), indices);
            _bind(subset.GetPrim(), _material(assignment), UsdShadeTokens->allPurpose);
        }
        api.SetMaterialBindSubsetsFamilyType(UsdGeomTokens->partition);
    }
}

/// Replace temporary output bindings with their original purpose-specific materials.
void MaterialBindings::restore(const UsdPrim& output) const
{
    for (const auto& prim : UsdPrimRange(output))
    {
        const auto relationship = UsdShadeMaterialBindingAPI(prim).GetDirectBindingRel();
        if (!relationship)
        {
            continue;
        }
        SdfPathVector targets;
        relationship.GetTargets(&targets);
        if (targets.size() != 1)
        {
            continue;
        }
        const auto found = m_assignments.find(targets.front());
        if (found == m_assignments.end())
        {
            continue;
        }
        _bindAssignment(prim, found->second, m_physics);
    }
}

} // namespace usd_optimize::physics_body_meshes
