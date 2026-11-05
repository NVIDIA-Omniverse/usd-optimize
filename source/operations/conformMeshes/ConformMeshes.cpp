// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "ConformMeshes.h"

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/geometry/Bucket.h>
#include <usd_optimize/core/geometry/MeshPreparation.h>
#include <usd_optimize/core/geometry/VirtualMesh.h>

// Carbonite
#include <carb/profiler/Profile.h>

// USD
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/tokens.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdShade/materialBindingAPI.h>

// C++
#include <algorithm>
#include <map>
#include <set>

PXR_NAMESPACE_USING_DIRECTIVE

USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::ConformMeshesOperation);


namespace usd_optimize
{

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((st1, "st1"))
    ((st2, "st2"))
    ((primvarsSt, "primvars:st"))
    ((primvarsSt0, "primvars:st0"))
    ((primvarsNormals, "primvars:normals"))
);
// LCOV_EXCL_STOP
// clang-format on

/// Constants
constexpr const char* s_categoryConformMeshes = "CONFORM";

ConformMeshesOperation::ConformMeshesOperation()
    : Operation("conformMeshes", "Conform Meshes", "Make meshes compatible with each other so that merge can combine them.")
{
    addArgument("meshPrimPaths",
                "Meshes to Conform",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("generateNormals",
                "Generate Normals",
                kDisplayTypeBool,
                "Generate normals for meshes that lack them when other meshes in the same merge boundary have them. "
                "Normals are not generated for subdivision surfaces, only for subdivisionScheme none",
                m_generateNormals);

    addArgument("generateUVs",
                "Generate UVs",
                kDisplayTypeBool,
                "Generate UVs for meshes that lack them when other meshes in the same merge boundary have them",
                m_generateUVs);

    addArgument("primvarDefaults",
                "Primvar Defaults",
                kDisplayTypeTextList,
                "Values to give a primvar on the meshes that lack it, as name=value, for example tint=0.5,0.5,0.5. "
                "A primvar with no default gets zero, or empty for strings and tokens",
                m_primvarDefaults);

    m_clustering.addMergePointArg(this);
    m_clustering.addConsiderMaterialsArg(this);
}


std::string ConformMeshesOperation::getDocumentation() const
{
    return std::string(R"DOC(The conform meshes operation runs before ``merge``. Within each merge boundary it makes the
meshes compatible with each other, so that meshes ``merge`` would otherwise keep apart, or merge while losing data,
are merged.

Meshes are grouped as ``merge`` groups them, using the same ``mergePoint`` and ``considerMaterials`` arguments, but
ignoring the differences listed below. A mesh is only changed to match the meshes in its own group. Differences that
keep groups apart, for example ``subdivisionScheme``, ``orientation``, ``purpose``, applied schemas, a UV set ``st1`` or
``st2`` on only some meshes, normals on only some meshes when they are not generated, or the material when
``considerMaterials`` is on, are logged and left alone. The other ``merge`` grouping arguments are assumed to be at
their defaults.

It makes these changes, and logs each one:

- Normals or UVs that some meshes of a group have and others lack are generated for the others, with
  ``generateNormals`` and ``generateProjectionUVs`` at their default settings. Nothing is generated when no mesh of the
  group has them, as the meshes merge as they are. The projected UVs fill the gap so the meshes merge, they are not
  authored to suit a textured material. Normals are only generated when the meshes' ``subdivisionScheme`` is
  ``none``, as USD says normals should not be authored on a subdivision surface, which computes its own. An unauthored
  ``subdivisionScheme`` is ``catmullClark``. A mesh has normals if ``normals`` or ``primvars:normals`` is authored on
  it. Generated normals are written to ``primvars:normals``, and ``merge`` combines them with the ``normals`` attribute
  of other meshes, each face keeping its own normals.
- A UV set named ``st`` on some meshes and ``st0`` on others is renamed to the name most of the meshes use (``st`` if
  the counts are equal). Generated UVs take that name and are not counted. A mesh whose bound material reads its UV
  set by name, through a ``UsdPrimvarReader_float2``, keeps that name, and when all such meshes use the same name the
  other meshes are renamed to it. A material that picks its UVs another way, for example by texture coordinate index,
  is not checked.
- A primvar that some meshes have and others lack is added to the others as a constant value, taken from
  ``primvarDefaults`` or else zero for numbers and empty for strings and tokens. A missing ``displayColor`` is filled
  with 0.18 grey and a missing ``displayOpacity`` with 1. A default that does not fit the primvar's type, including a
  number outside the type's range, ``inf`` or ``nan``, is logged and not used.
- Where ``doubleSided`` differs, it is set to true on every mesh in the group. A single-sided mesh therefore shows
  its back faces after the merge.

Meshes that still lack normals or UVs that others in the group have, because they were not generated, are only renamed,
filled and set double-sided to match each other, as ``merge`` keeps them apart from the rest.

It also logs, and leaves alone, the differences within a group it cannot reconcile: primvars with the same name but a
different value type, primvars of a type ``merge`` cannot combine or with an element size above 1, a mesh that has both
``st`` and ``st0``, a UV set that cannot be renamed because its material reads it by name, its values or indices have
time samples, or it is authored in a layer other than the edit target, UVs that some meshes lack when ``generateUVs`` is
off, normals that some meshes lack when ``generateNormals`` is off or the meshes are subdivision surfaces, and normals or
UVs that could not be generated for a mesh.
``merge`` still keeps such meshes apart, or drops the primvar.

Recommended pipelines
---------------------

``conformMeshes`` then ``merge``.

Starting configurations
-----------------------

Make every mesh under the stage mergeable:

.. code-block:: json

    [{"operation": "conformMeshes"}, {"operation": "merge"}]
)DOC");
}


std::string ConformMeshesOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion ConformMeshesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string ConformMeshesOperation::getCategory() const
{
    return s_categoryConformMeshes;
}


std::string ConformMeshesOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


namespace
{

/// "1 mesh" or "N meshes", for log messages.
std::string _meshCount(size_t count)
{
    return std::to_string(count) + (count == 1 ? " mesh" : " meshes");
}

/// Name the attributes that differ between the groups of a merge boundary, comparing each group's first mesh.
std::string _describeSplit(const std::vector<std::vector<UsdPrim>>& groups, bool considerMaterials)
{
    std::map<std::string, std::set<std::string>> values;
    for (const std::vector<UsdPrim>& group : groups)
    {
        const UsdPrim& prim = group.front();
        const UsdGeomMesh mesh(prim);
        TfToken token;
        mesh.GetSubdivisionSchemeAttr().Get(&token);
        values["subdivisionScheme"].insert(token.GetString());
        mesh.GetOrientationAttr().Get(&token);
        values["orientation"].insert(token.GetString());
        values["purpose"].insert(mesh.ComputePurpose().GetString());
        values["visibility"].insert(mesh.ComputeVisibility().GetString());
        std::string schemas;
        for (const TfToken& schema : prim.GetAppliedSchemas())
        {
            schemas += schema.GetString() + " ";
        }
        values["applied schemas"].insert(schemas);
        const UsdGeomPrimvarsAPI primvars(prim);
        values["UV set st1"].insert(primvars.GetPrimvar(_tokens->st1).HasAuthoredValue() ? "yes" : "no");
        values["UV set st2"].insert(primvars.GetPrimvar(_tokens->st2).HasAuthoredValue() ? "yes" : "no");
        values["normals"].insert(meshHasNormals(prim) ? "yes" : "no");
        if (considerMaterials)
        {
            const UsdShadeMaterial material = UsdShadeMaterialBindingAPI(prim).ComputeBoundMaterial();
            values["material"].insert(material ? material.GetPath().GetString() : std::string());
        }
    }

    std::string names;
    for (const auto& [name, distinct] : values)
    {
        if (distinct.size() > 1)
        {
            names += (names.empty() ? "" : ", ") + name;
        }
    }
    return names.empty() ? "other attributes" : names;
}

} // namespace


OperationResult ConformMeshesOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "UsdOptimize|ConformMeshesOperation|Execute");

    const UsdStageWeakPtr stage = getUsdStage();
    // The groups of meshes merge would try to combine
    MergeBoundaryLookup lookup = m_clustering.discoverMergeBoundaries(stage, m_meshPrimPaths);

    // The caller's defaults, as "name=value"
    std::map<TfToken, std::string> defaults;
    for (const std::string& entry : m_primvarDefaults)
    {
        const size_t split = entry.find('=');
        if (split == std::string::npos || split == 0)
        {
            USD_OPTIMIZE_LOG_WARN("Ignoring primvar default \"%s\": expected name=value", entry.c_str());
            continue;
        }
        defaults[TfToken(entry.substr(0, split))] = entry.substr(split + 1);
    }

    // Group meshes as merge would, ignoring what conform fixes, so a mesh only changes to match meshes it can merge
    // with.
    BucketerPtr bucketer = std::make_shared<Bucketer>(getContext());
    bucketer->SetConsiderMaterials(m_clustering.m_considerMaterials);
    bucketer->SetAllowSingleMeshes(true);
    _populateMergeBucketerAttributes(bucketer, false);
    bucketer->AddIgnoreAttributeNames({ _tokens->primvarsSt,
                                        _tokens->primvarsSt0,
                                        UsdGeomTokens->primvarsDisplayColor,
                                        UsdGeomTokens->primvarsDisplayOpacity,
                                        UsdGeomTokens->doubleSided });
    if (m_generateNormals)
    {
        bucketer->AddIgnoreAttributeNames({ _tokens->primvarsNormals });
    }
    {
        UsdGeomXformCache xformCache;
        UsdShadeMaterialBindingAPI::BindingsCache bindingsCache;
        UsdShadeMaterialBindingAPI::CollectionQueryCache collQueryCache;
        for (const auto& [boundary, prims] : lookup.parentToPrims)
        {
            std::vector<VirtualMesh> virtualMeshes;
            virtualMeshes.reserve(prims.size());
            for (const UsdPrim& prim : prims)
            {
                virtualMeshes.emplace_back(prim, xformCache, bindingsCache, collQueryCache);
            }
            bucketer->AddVirtualMeshes(virtualMeshes, boundary);
        }
    }
    bucketer->Bucket(stage);

    // The groups of each merge boundary, in path order so the result is deterministic
    std::map<SdfPath, std::vector<std::vector<UsdPrim>>> groupsByBoundary;
    std::set<SdfPath> grouped;
    for (const VirtualMesh& bucket : bucketer->GetOutputData())
    {
        std::vector<UsdPrim> prims;
        for (const VirtualMesh& child : bucket.getSupersetChildren())
        {
            if (grouped.insert(child.getSourcePath()).second)
            {
                prims.push_back(stage->GetPrimAtPath(child.getSourcePath()));
            }
        }
        if (prims.empty())
        {
            continue; // LCOV_EXCL_LINE
        }
        std::sort(prims.begin(),
                  prims.end(),
                  [](const UsdPrim& a, const UsdPrim& b) { return a.GetPath() < b.GetPath(); });
        const auto boundary = lookup.primToParent.find(prims.front().GetPath());
        if (boundary != lookup.primToParent.end())
        {
            groupsByBoundary[boundary->second].push_back(std::move(prims));
        }
    }

    MeshPreparationOptions options;
    options.generateNormals = m_generateNormals;
    options.generateUVs = m_generateUVs;
    options.makeDoubleSided = true;
    options.useDefaultPrimvarValues = true;
    options.primvarDefaults = defaults;
    MeshPreparationResult tally;
    size_t groupCount = 0;
    size_t compatibleCount = 0;
    size_t aloneCount = 0;
    for (const auto& [boundary, prims] : lookup.parentToPrims)
    {
        const std::string boundaryName = "merge boundary " + boundary.GetString();
        std::vector<std::vector<UsdPrim>>& groups = groupsByBoundary[boundary];

        for (const UsdPrim& prim : prims)
        {
            if (grouped.count(prim.GetPath()) == 0)
            {
                USD_OPTIMIZE_LOG_VERBOSE("Left alone (merge would skip it): %s", prim.GetPath().GetText());
            }
        }

        if (groups.size() > 1)
        {
            USD_OPTIMIZE_LOG_INFO("%s: not reconciled: meshes kept apart by %s, in %s groups",
                                  boundaryName.c_str(),
                                  _describeSplit(groups, m_clustering.m_considerMaterials).c_str(),
                                  std::to_string(groups.size()).c_str());
            ++tally.notReconciled;
        }

        for (const std::vector<UsdPrim>& group : groups)
        {
            // Nothing to conform a mesh against if it is alone
            if (group.size() < 2)
            {
                USD_OPTIMIZE_LOG_VERBOSE("Left alone (%s): %s",
                                         prims.size() < 2 ? "only mesh in its merge boundary" :
                                                            "no mesh in its merge boundary it can merge with",
                                         group.front().GetPath().GetText());
                ++aloneCount;
                continue;
            }
            ++groupCount;
            const std::string name =
                groups.size() == 1 ? boundaryName : boundaryName + ", group of " + group.front().GetPath().GetString();

            const size_t before = tally.total();
            tally += prepareMeshes(*getContext(), name, group, options);

            if (tally.total() == before)
            {
                USD_OPTIMIZE_LOG_VERBOSE("Left alone (meshes already compatible): %s", name.c_str());
                ++compatibleCount;
            }
        }
    }

    USD_OPTIMIZE_LOG_INFO(
        "Conformed %s (%s already compatible, %s left alone with no mesh to merge with): %s "
        "normals or UV sets generated, %s primvars renamed, %s primvars added, %s schema values changed, %s "
        "differences not reconciled",
        (std::to_string(groupCount) + (groupCount == 1 ? " group" : " groups")).c_str(),
        std::to_string(compatibleCount).c_str(),
        _meshCount(aloneCount).c_str(),
        std::to_string(tally.generated).c_str(),
        std::to_string(tally.renamed).c_str(),
        std::to_string(tally.filled).c_str(),
        std::to_string(tally.schemaFixed).c_str(),
        std::to_string(tally.notReconciled).c_str());

    return { true, nullptr, nullptr };
}


} // namespace usd_optimize
