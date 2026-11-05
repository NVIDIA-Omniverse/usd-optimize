// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "MeshCompatibility.h"

#include "ColliderProperties.h"

#include <pxr/base/tf/staticTokens.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/usd/sdf/copyUtils.h>
#include <pxr/usd/sdf/schema.h>
#include <pxr/usd/usd/tokens.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/subset.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdShade/tokens.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <stdexcept>
#include <usd_optimize/core/Utils.h>
#include <usd_optimize/core/geometry/Bucket.h>

PXR_NAMESPACE_USING_DIRECTIVE

namespace usd_optimize::physics_body_meshes
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    (st)
    (st0)
    (primvars)
    ((primvarsSt, "primvars:st"))
    ((primvarsSt0, "primvars:st0"))
    ((primvarsNormals, "primvars:normals"))
);
// LCOV_EXCL_STOP
// clang-format on


/// Keep inherited primvars available after geometry moves out of an intermediate container.
void resolveInheritedPrimvars(const UsdPrim& prim)
{
    for (const auto& inherited : UsdGeomPrimvarsAPI(prim).FindPrimvarsWithInheritance())
    {
        if (!inherited.HasAuthoredValue() || inherited.GetAttr().GetPrim() == prim)
        {
            continue;
        }
        VtValue value;
        if (!inherited.ComputeFlattened(&value))
        {
            throw std::runtime_error("Cannot resolve inherited primvar " + inherited.GetAttr().GetPath().GetAsString());
        }
        auto local = UsdGeomPrimvarsAPI(prim).CreatePrimvar(inherited.GetPrimvarName(),
                                                            inherited.GetTypeName(),
                                                            inherited.GetInterpolation(),
                                                            inherited.GetElementSize());
        local.Set(value);
    }
}

/// Geometry and primvar samples need a separate animated-mesh merge implementation.
std::string geometrySkipReason(const UsdPrim& prim, bool collider)
{
    const bool hasMaterialSubsets = !collider && !UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets().empty();
    auto schemas = prim.GetAppliedSchemas();
    const auto authoredPhysics = physicsSchemas(prim);
    schemas.insert(schemas.end(), authoredPhysics.begin(), authoredPhysics.end());
    for (const auto& schema : schemas)
    {
        const auto& name = schema.GetString();
        if (TfStringStartsWith(name, "Skel") || name.find("Deformable") != std::string::npos ||
            name.find("Particle") != std::string::npos)
        {
            return "deformed geometry requires a separate merge policy: " + name;
        }
    }
    for (const auto& attr : prim.GetAttributes())
    {
        const auto& name = attr.GetName().GetString();
        if (collider && (TfStringStartsWith(name, "primvars:") || attr.GetName() == UsdGeomTokens->normals ||
                         attr.GetName() == UsdGeomTokens->visibility))
        {
            continue;
        }
        if (!collider && isPhysicsProperty(name))
        {
            continue;
        }
        if (!TfStringStartsWith(name, "xformOp") && attr.GetName() != UsdGeomTokens->visibility &&
            attr.GetNumTimeSamples() != 0)
        {
            return "time-sampled geometry or properties: " + attr.GetPath().GetAsString();
        }
    }
    for (const auto& primvar : UsdGeomPrimvarsAPI(prim).FindPrimvarsWithInheritance())
    {
        if (collider)
        {
            if (isPhysicsProperty(primvar.GetPrimvarName().GetString()))
            {
                return "physics primvar requires a preservation rule: " + primvar.GetAttr().GetPath().GetAsString();
            }
            continue;
        }
        if (primvar.GetAttr().GetNumTimeSamples() != 0 || primvar.GetIndicesAttr().GetNumTimeSamples() != 0)
        {
            return "time-sampled primvar: " + primvar.GetAttr().GetPath().GetAsString();
        }
        if (primvar.HasAuthoredValue() && primvar.GetElementSize() != 1)
        {
            return "primvar element size is not supported: " + primvar.GetAttr().GetPath().GetAsString();
        }
        if (primvar.HasAuthoredValue() && !primvar.GetTypeName().IsArray())
        {
            return "scalar primvar cannot be preserved: " + primvar.GetAttr().GetPath().GetAsString();
        }
        const auto type = primvar.GetTypeName().GetScalarType().GetType();
        if (primvar.HasAuthoredValue() && type != TfType::Find<bool>() && type != TfType::Find<int>() &&
            type != TfType::Find<float>() && type != TfType::Find<TfToken>() && type != TfType::Find<std::string>() &&
            type != TfType::Find<GfVec2f>() && type != TfType::Find<GfVec3f>())
        {
            return "primvar type is not supported: " + primvar.GetAttr().GetPath().GetAsString();
        }
        // Preparation also slices material subsets, before their bindings are normalized for the final merge.
        if (primvar.HasAuthoredValue() && hasMaterialSubsets && primvar.GetInterpolation() != UsdGeomTokens->constant &&
            type != TfType::Find<float>() && type != TfType::Find<GfVec2f>() && type != TfType::Find<GfVec3f>())
        {
            return "primvar type cannot be preserved when slicing material subsets: " +
                   primvar.GetAttr().GetPath().GetAsString();
        }
    }
    for (const auto& child : prim.GetChildren())
    {
        if (child.IsA<UsdGeomSubset>())
        {
            TfToken family;
            UsdGeomSubset(child).GetFamilyNameAttr().Get(&family);
            if (family != UsdShadeTokens->materialBind)
            {
                return "subset family is not supported: " + child.GetPath().GetAsString();
            }
            const auto schemaNames = UsdGeomSubset::GetSchemaAttributeNames();
            for (const auto& attr : child.GetAuthoredAttributes())
            {
                if (attr.GetNumTimeSamples() != 0)
                {
                    return "time-sampled subset property: " + attr.GetPath().GetAsString();
                }
                if (std::find(schemaNames.begin(), schemaNames.end(), attr.GetName()) == schemaNames.end())
                {
                    return "subset property requires a preservation rule: " + attr.GetPath().GetAsString();
                }
            }
        }
    }
    return {};
}

/// Validate primvar values before resolving inheritance or converting shapes to meshes.
std::string primvarDataSkipReason(const UsdPrim& prim)
{
    const UsdGeomMesh mesh(prim);
    VtVec3fArray points;
    VtIntArray counts, indices;
    if (mesh)
    {
        mesh.GetPointsAttr().Get(&points);
        mesh.GetFaceVertexCountsAttr().Get(&counts);
        mesh.GetFaceVertexIndicesAttr().Get(&indices);
    }
    for (const auto& primvar : UsdGeomPrimvarsAPI(prim).FindPrimvarsWithInheritance())
    {
        if (!primvar.HasAuthoredValue())
        {
            continue;
        }
        VtValue value;
        if (!primvar.ComputeFlattened(&value))
        {
            return "invalid indexed primvar: " + primvar.GetAttr().GetPath().GetAsString();
        }
        // Shape topology is known only after conversion; check its interpolation sizes on the resulting mesh.
        if (!mesh)
        {
            continue;
        }
        const auto interpolation = primvar.GetInterpolation();
        const size_t expected = interpolation == UsdGeomTokens->constant ? 1 :
                                interpolation == UsdGeomTokens->uniform  ? counts.size() :
                                interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying ?
                                                                          points.size() :
                                interpolation == UsdGeomTokens->faceVarying ? indices.size() :
                                                                              0;
        const size_t size = value.IsArrayValued() ? value.GetArraySize() : 1;
        if (expected == 0 || size != expected)
        {
            return "primvar size does not match its interpolation: " + primvar.GetAttr().GetPath().GetAsString();
        }
    }
    return {};
}

/// Validate mesh topology and attribute cardinality before building merge groups.
std::string meshDataSkipReason(const UsdPrim& prim, bool collider)
{
    const UsdGeomMesh mesh(prim);
    if (!mesh)
    {
        return "shape could not be converted to a visual mesh";
    }
    VtVec3fArray points;
    VtIntArray counts, indices;
    mesh.GetPointsAttr().Get(&points);
    mesh.GetFaceVertexCountsAttr().Get(&counts);
    mesh.GetFaceVertexIndicesAttr().Get(&indices);
    std::string reason;
    if (points.empty() || counts.empty() || !UsdGeomMesh::ValidateTopology(indices, counts, points.size(), &reason) ||
        std::any_of(counts.begin(),
                    counts.end(),
                    /* Reject faces that cannot form a polygon. */ [](int count) { return count < 3; }))
    {
        return "mesh has empty or invalid topology";
    }
    for (const auto& point : points)
    {
        if (!std::isfinite(point[0]) || !std::isfinite(point[1]) || !std::isfinite(point[2]))
        {
            return "mesh has non-finite points";
        }
    }
    if (!collider)
    {
        for (const auto& name : { UsdGeomTokens->velocities, UsdGeomTokens->accelerations })
        {
            VtVec3fArray vectors;
            if (prim.GetAttribute(name).Get(&vectors) && !vectors.empty())
            {
                return "mesh motion vectors require a separate transform policy";
            }
        }
        const auto primvarReason = primvarDataSkipReason(prim);
        if (!primvarReason.empty())
        {
            return primvarReason;
        }
        VtVec3fArray normals;
        if (mesh.GetNormalsAttr().Get(&normals) && !normals.empty())
        {
            const auto interpolation = mesh.GetNormalsInterpolation();
            const size_t expected = interpolation == UsdGeomTokens->constant    ? 1 :
                                    interpolation == UsdGeomTokens->uniform     ? counts.size() :
                                    interpolation == UsdGeomTokens->faceVarying ? indices.size() :
                                                                                  points.size();
            if (normals.size() != expected)
            {
                return "normal size does not match its interpolation";
            }
        }
    }
    std::set<int> assigned;
    for (const auto& subset : UsdShadeMaterialBindingAPI(prim).GetMaterialBindSubsets())
    {
        VtIntArray faces;
        TfToken elementType;
        subset.GetElementTypeAttr().Get(&elementType);
        if (!subset.GetIndicesAttr().Get(&faces) || elementType != UsdGeomTokens->face)
        {
            return "material subset has unsupported elements";
        }
        for (const int face : faces)
        {
            if (face < 0 || static_cast<size_t>(face) >= counts.size() || !assigned.insert(face).second)
            {
                return "material subset indices are invalid or overlap";
            }
        }
    }
    return {};
}

/// Reject relative transforms with reflections or invalid, singular, or non-finite values.
std::string transformSkipReason(const UsdPrim& prim, const UsdPrim& parent)
{
    UsdGeomXformCache xforms;
    const auto parentMatrix = xforms.GetLocalToWorldTransform(parent);
    const auto matrix = xforms.GetLocalToWorldTransform(prim) * parentMatrix.GetInverse();
    for (int row = 0; row != 4; ++row)
    {
        for (int column = 0; column != 4; ++column)
        {
            if (!std::isfinite(matrix[row][column]))
            {
                return "non-finite transform";
            }
        }
    }
    if (std::abs(parentMatrix.GetDeterminant()) < 1e-12 || matrix.GetDeterminant() <= 1e-12)
    {
        return "singular or reflected transform relative to the output parent";
    }
    return {};
}

namespace
{

/// Effective schemas omit unknown plugin APIs; authored applications must still survive grouping and output.
TfTokenVector _appliedSchemas(const UsdPrim& prim)
{
    auto schemas = prim.GetAppliedSchemas();
    SdfTokenListOp authored;
    if (prim.GetMetadata(UsdTokens->apiSchemas, &authored))
    {
        const auto applied = authored.GetAppliedItems();
        schemas.insert(schemas.end(), applied.begin(), applied.end());
    }
    std::sort(schemas.begin(), schemas.end());
    schemas.erase(std::unique(schemas.begin(), schemas.end()), schemas.end());
    return schemas;
}

/// Converter cache entries identify individual source shapes rather than asset semantics.
UsdMetadataValueMap _mergeMetadata(const UsdPrim& prim)
{
    auto metadata = prim.GetAllAuthoredMetadata();
    metadata.erase(UsdTokens->apiSchemas);
    metadata.erase(SdfFieldKeys->Specifier);
    metadata.erase(SdfFieldKeys->TypeName);
    const auto data = metadata.find(SdfFieldKeys->CustomData);
    if (data != metadata.end() && data->second.IsHolding<VtDictionary>())
    {
        auto values = data->second.UncheckedGet<VtDictionary>();
        values.erase("USD_OPTIMIZE_Prim_Hash");
        if (values.empty())
        {
            metadata.erase(data);
        }
        else
        {
            data->second = VtValue(values);
        }
    }
    return metadata;
}

/// Add properties that generic mesh bucketing does not compare, including all primvar types and presence.
VtDictionary _compatibilityValues(const UsdPrim& prim,
                                  const MeshPreparationOptions& preparation,
                                  MeshMergePhase phase,
                                  bool collider)
{
    const bool preparing = phase == MeshMergePhase::ePreparation;
    VtDictionary values;
    TfTokenVector schemas;
    for (const auto& schema : _appliedSchemas(prim))
    {
        if (schema != UsdShadeTokens->MaterialBindingAPI)
        {
            schemas.push_back(schema);
        }
    }
    values["schemas"] = VtValue(VtTokenArray(schemas.begin(), schemas.end()));
    TfToken visibility;
    UsdGeomImageable(prim).GetVisibilityAttr().Get(&visibility);
    values["visibility"] = VtValue(visibility);
    const auto schemaAttrs = UsdGeomMesh::GetSchemaAttributeNames();
    const std::set<TfToken> meshAttrs(schemaAttrs.begin(), schemaAttrs.end());
    for (const auto& attr : prim.GetAttributes())
    {
        const auto& name = attr.GetName().GetString();
        if (meshAttrs.count(attr.GetName()) || TfStringStartsWith(name, "xformOp") ||
            TfStringStartsWith(name, "primvars:"))
        {
            continue;
        }
        VtValue value;
        if (attr.Get(&value))
        {
            values["attributeType:" + name] = VtValue(attr.GetTypeName().GetAsToken());
            values["attribute:" + name] = value;
        }
    }
    for (const auto& primvar : UsdGeomPrimvarsAPI(prim).FindPrimvarsWithInheritance())
    {
        if (collider || !primvar.HasAuthoredValue() || primvar.GetPrimvarName() == UsdGeomTokens->normals)
        {
            continue;
        }
        const auto& name = primvar.GetPrimvarName();
        if (preparing && (name == _tokens->st || name == _tokens->st0 || preparation.primvarDefaults.count(name)))
        {
            continue;
        }
        values["primvar:" + name.GetString()] = VtValue(primvar.GetTypeName().GetAsToken());
    }
    if (!collider && (!preparing || !preparation.generateNormals))
    {
        values["normals"] = VtValue(meshHasNormals(prim));
    }
    for (const auto& [name, value] : _mergeMetadata(prim))
    {
        values["metadata:" + name.GetString()] = value;
    }
    return values;
}

/// Report unmatched geometry and differing additional compatibility values within this output scope.
void _reportUnmatchedMeshes(const std::vector<UsdPrim>& prims,
                            const std::vector<VirtualMesh>& groups,
                            const MeshPreparationOptions& preparation,
                            const std::map<SdfPath, VtDictionary>& physicsValues,
                            std::vector<PathIssue>& skipped)
{
    const bool collisionGeometry = !physicsValues.empty();
    std::set<SdfPath> grouped;
    for (const auto& group : groups)
    {
        for (const auto& input : group.getSupersetChildren())
        {
            grouped.insert(input.getSourcePath());
        }
    }
    if (grouped.size() != prims.size())
    {
        // Describe differences in this scope without guessing which peer the caller intended to merge.
        std::set<std::string> differences;
        VtDictionary first;
        bool haveFirst = false;
        for (const auto& prim : prims)
        {
            auto values = _compatibilityValues(prim, preparation, MeshMergePhase::eMerge, collisionGeometry);
            const auto physics = physicsValues.find(prim.GetPath());
            if (physics != physicsValues.end())
            {
                for (const auto& [name, value] : physics->second)
                {
                    values["physics:" + name] = value;
                }
            }
            if (!haveFirst)
            {
                first = std::move(values);
                haveFirst = true;
                continue;
            }
            for (const auto& [name, value] : first)
            {
                const auto other = values.find(name);
                if (other == values.end() || other->second != value)
                {
                    differences.insert(name);
                }
            }
            for (const auto& [name, value] : values)
            {
                if (!first.count(name))
                {
                    differences.insert(name);
                }
            }
        }
        std::string reason = collisionGeometry ? "no compatible collision geometry within its output scope" :
                                                 "no compatible visual geometry within its output scope";
        if (!differences.empty())
        {
            reason += "; differing compatibility values in scope: " +
                      TfStringJoin(std::vector<std::string>(differences.begin(), differences.end()), ", ");
        }
        for (const auto& prim : prims)
        {
            if (!grouped.count(prim.GetPath()))
            {
                skipped.push_back({ prim.GetPath().GetAsString(), reason });
            }
        }
    }
}

} // namespace

/// Group meshes by inherited values and the operation's visual and physics compatibility rules.
std::vector<VirtualMesh> bucketMeshes(const UsdStageWeakPtr& stage,
                                      ExecutionContext* context,
                                      const std::vector<UsdPrim>& prims,
                                      const SdfPath& parent,
                                      const MeshPreparationOptions& preparation,
                                      bool considerMaterials,
                                      MeshMergePhase phase,
                                      std::vector<PathIssue>& skipped,
                                      const std::map<SdfPath, VtDictionary>& physicsValues)
{
    const bool preparing = phase == MeshMergePhase::ePreparation;
    const bool collisionGeometry = !physicsValues.empty();
    auto bucketer = std::make_shared<Bucketer>(context);
    _populateMergeBucketerAttributes(bucketer, false);
    bucketer->AddIgnoreAttributeNames({ UsdGeomTokens->visibility });
    bucketer->SetConsiderMaterials(considerMaterials);
    bucketer->SetAllowSingleMeshes(preparing);
    if (collisionGeometry)
    {
        bucketer->AddIgnoreAttributeNamespaces({ _tokens->primvars });
    }
    bucketer->SetCompatibilityProvider(
        /* Supply resolved visual and physics values to the compatibility key. */
        [&](const VirtualMesh& mesh)
        {
            auto values = _compatibilityValues(mesh.getPrim(), preparation, phase, collisionGeometry);
            const auto found = physicsValues.find(mesh.getSourcePath());
            if (found != physicsValues.end())
            {
                values["physics"] = VtValue(found->second);
            }
            return values;
        });
    if (preparing)
    {
        bucketer->AddIgnoreAttributeNames({ _tokens->primvarsSt, _tokens->primvarsSt0 });
        if (preparation.generateNormals)
        {
            bucketer->AddIgnoreAttributeNames({ _tokens->primvarsNormals });
        }
        if (preparation.makeDoubleSided)
        {
            bucketer->AddIgnoreAttributeNames({ UsdGeomTokens->doubleSided });
        }
        for (const auto& [name, value] : preparation.primvarDefaults)
        {
            bucketer->AddIgnoreAttributeNames({ TfToken("primvars:" + name.GetString()) });
        }
    }
    std::vector<VirtualMesh> meshes;
    UsdGeomXformCache xforms;
    UsdShadeMaterialBindingAPI::BindingsCache bindings;
    UsdShadeMaterialBindingAPI::CollectionQueryCache collections;
    for (const auto& prim : prims)
    {
        meshes.emplace_back(prim, xforms, bindings, collections);
    }
    bucketer->AddVirtualMeshes(meshes, parent);
    bucketer->Bucket(stage);
    auto groups = bucketer->GetOutputData();
    if (!preparing)
    {
        _reportUnmatchedMeshes(prims, groups, preparation, physicsValues, skipped);
    }
    return groups;
}

/// Preserve agreed metadata, APIs, and opaque custom arrays that VirtualMesh drops when slicing material subsets.
void copyMergeProperties(const UsdPrim& source, const UsdPrim& output)
{
    for (const auto& [name, value] : _mergeMetadata(source))
    {
        output.SetMetadata(name, value);
    }
    for (const auto& schema : _appliedSchemas(source))
    {
        output.AddAppliedSchema(schema);
    }
    const auto editTarget = output.GetStage()->GetEditTarget();
    for (const auto& attribute : source.GetAuthoredAttributes())
    {
        if (!attribute.IsCustom() || !attribute.GetTypeName().IsArray() ||
            TfStringStartsWith(attribute.GetName().GetString(), "primvars:"))
        {
            continue;
        }
        const auto destination = editTarget.MapToSpecPath(output.GetPath().AppendProperty(attribute.GetName()));
        if (!SdfCopySpec(source.GetStage()->GetRootLayer(), attribute.GetPath(), editTarget.GetLayer(), destination))
        {
            throw std::runtime_error("Cannot preserve custom attribute: " + attribute.GetPath().GetAsString());
        }
    }
}

} // namespace usd_optimize::physics_body_meshes
