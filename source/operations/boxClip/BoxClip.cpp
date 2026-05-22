// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "BoxClip.h"

// OmniMeshOps
#include <OmniMeshOps/Slice.h>
#include <OmniMeshOps/usd/MeshData.h>

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Utils.h>

// USD
#include <pxr/usd/usd/primCompositionQuery.h>

// TBB
#include <tbb/blocked_range.h>
#include <tbb/parallel_for.h>

// C++
#include <limits>

PXR_NAMESPACE_USING_DIRECTIVE

// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((noDelete, "no_delete"))
    ((hideInStageWindow, "hide_in_stage_window"))
);
// LCOV_EXCL_STOP
// clang-format on

SO_PLUGIN_INIT(omni::scene::optimizer::BoxClipOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categoryBoxClip = "BOXCLIP";

BoxClipOperation::BoxClipOperation()
    : OmniOperation("boxClip", "Box Clip", "This operation clips meshes by a provided box.")
    , m_min({ 0, 0, 0 })
    , m_max({ 0, 0, 0 })
    , m_clipBoxDef(ClipBoxDefinition::eByPrim)
    , m_clipBoxPrimPath()
    , m_ignoreClipBoxSide(IgnoreClipBoxSide::eDisabled)
    , m_partiallyIntersectedPrims(PartiallyIntersectedPrims::eKeep)
    , m_keepGeometry(KeepGeometry::eInside)
    , m_clipMode(ClipMode::eInsideKeep)
    , m_clipBoxPrimSdfPath()
{

    addArgument("paths", "Meshes To Process", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("clipBoxDef", "Clip Box Definition", kDisplayTypeEnum, "How the clip box is defined", m_clipBoxDef)
        .setEnumValues<ClipBoxDefinition>(
            { { ClipBoxDefinition::eByAABB, "Corners of Box" }, { ClipBoxDefinition::eByPrim, "Prim" } });

    addGroup("clipBoxValues",
             addJoin("Clip Box min corner",
                     "The min of the world-space AABB",
                     addArgument("minX", "min x", kDisplayTypeFloatSlider, "Min x value", m_min[0]),
                     addArgument("minY", "min y", kDisplayTypeFloatSlider, "Min y value", m_min[1]),
                     addArgument("minZ", "min z", kDisplayTypeFloatSlider, "Min z value", m_min[2])),

             addJoin("Clip Box max corner",
                     "The max of the world-space AABB",
                     addArgument("maxX", "max x", kDisplayTypeFloatSlider, "Max x value", m_max[0]),
                     addArgument("maxY", "max y", kDisplayTypeFloatSlider, "Max y value", m_max[1]),
                     addArgument("maxZ", "max z", kDisplayTypeFloatSlider, "Max z value", m_max[2])))
        .setVisibleIf("clipBoxDef == 0")
        .setEnableIf("clipBoxDef == 0");

    addArgument("clipBoxPrimPath",
                "Clip Box Prim",
                kDisplayTypePrimPath,
                "Prim whose extents defines the clip box",
                m_clipBoxPrimPath)
        .setPlaceholder("Prim that defines the clip box")
        .setVisibleIf("clipBoxDef != 0")
        .setEnableIf("clipBoxDef != 0");

    addArgument("ignoreClipBoxSide",
                "Ignore Clip Box Side",
                kDisplayTypeEnum,
                "Optionally ignore one side of the clip box (extending it to infinity)",
                m_ignoreClipBoxSide)
        .setEnumValues<IgnoreClipBoxSide>({ { IgnoreClipBoxSide::eDisabled, "None" },
                                            { IgnoreClipBoxSide::eNegX, "-X" },
                                            { IgnoreClipBoxSide::ePosX, "+X" },
                                            { IgnoreClipBoxSide::eNegY, "-Y" },
                                            { IgnoreClipBoxSide::ePosY, "+Y" },
                                            { IgnoreClipBoxSide::eNegZ, "-Z" },
                                            { IgnoreClipBoxSide::ePosZ, "+Z" } });

    auto& keepGeomArg = addArgument("keepGeometry",
                                    "Keep Geometry",
                                    kDisplayTypeEnum,
                                    "Choose whether to keep geometry inside or outside the clip box.",
                                    m_keepGeometry);
    keepGeomArg.setEnumValues<KeepGeometry>(
        { { KeepGeometry::eInside, "Inside Clip Box" }, { KeepGeometry::eOutside, "Outside Clip Box" } });
    keepGeomArg.setVisible(false);

    auto& partialArg = addArgument("partialIntersections",
                                   "Partial Intersections",
                                   kDisplayTypeEnum,
                                   "Keep or discard prims that partially intersect the clip box",
                                   m_partiallyIntersectedPrims);
    partialArg.setEnumValues<PartiallyIntersectedPrims>({ { PartiallyIntersectedPrims::eKeep, "Keep" },
                                                          { PartiallyIntersectedPrims::eKeepIntersection, "Cut Mesh" },
                                                          { PartiallyIntersectedPrims::eDiscard, "Discard" } });
    partialArg.setVisible(false);

    auto& clipModeArg =
        addArgument("clipMode", "Clip Mode", kDisplayTypeEnum, "How geometry is clipped relative to the box", m_clipMode);
    clipModeArg.setEnumValues<ClipMode>({
        { ClipMode::eInsideKeep, "Keep if fully inside clip box + keep if partially inside" },
        { ClipMode::eInsideCutMesh, "Keep if fully inside clip box + cut if partially inside" },
        { ClipMode::eInsideDiscard, "Keep if fully inside clip box + discard if partially inside" },
        { ClipMode::eOutsideKeep, "Keep if fully outside clip box + keep if partially outside" },
        { ClipMode::eOutsideDiscard, "Keep if fully outside clip box + discard if partially outside" },
    });
}


std::string BoxClipOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion BoxClipOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string BoxClipOperation::getCategory() const
{
    return s_categoryBoxClip;
}


std::string BoxClipOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


OperationResult BoxClipOperation::executePre()
{
    if (m_clipBoxDef == ClipBoxDefinition::eByAABB)
    {
        m_resolvedMin = m_min;
        m_resolvedMax = m_max;

        // Validate that min values are less than max values
        for (int i = 0; i < 3; i++)
        {
            if (m_resolvedMin[i] > m_resolvedMax[i])
            {
                std::ostringstream msg;
                msg << "Clip min values must be ≤ max values "
                    << "(axis " << i << ": " << m_resolvedMin[i] << " > " << m_resolvedMax[i] << ")";
                return { false, getCStr(msg.str().c_str()) };
            }
        }
    }
    else
    {
        m_clipBoxPrimSdfPath = SdfPath(m_clipBoxPrimPath);
        auto prim = getUsdStage()->GetPrimAtPath(m_clipBoxPrimSdfPath);
        if (auto imageable = UsdGeomImageable(prim))
        {
            GfBBox3d bound = imageable.ComputeWorldBound(UsdTimeCode::Default(), UsdGeomTokens->default_);
            GfRange3d bbox = bound.ComputeAlignedBox();
            for (int i = 0; i < 3; i++)
            {
                m_resolvedMin[i] = bbox.GetMin()[i];
                m_resolvedMax[i] = bbox.GetMax()[i];
            }
        }
        else
        {
            return { false, getCStr("Clip prim was invalid") };
        }
    }

    switch (m_ignoreClipBoxSide)
    {
    case IgnoreClipBoxSide::eNegX:
        m_resolvedMin[0] = -std::numeric_limits<double>::max();
        break;
    case IgnoreClipBoxSide::ePosX:
        m_resolvedMax[0] = std::numeric_limits<double>::max();
        break;
    case IgnoreClipBoxSide::eNegY:
        m_resolvedMin[1] = -std::numeric_limits<double>::max();
        break;
    case IgnoreClipBoxSide::ePosY:
        m_resolvedMax[1] = std::numeric_limits<double>::max();
        break;
    case IgnoreClipBoxSide::eNegZ:
        m_resolvedMin[2] = -std::numeric_limits<double>::max();
        break;
    case IgnoreClipBoxSide::ePosZ:
        m_resolvedMax[2] = std::numeric_limits<double>::max();
        break;
    case IgnoreClipBoxSide::eDisabled:
        /* fall-through*/
    default:
        break;
    }

    // Only decompose clipMode when it was explicitly set (non-default).
    // This preserves backward compatibility: CLI/test callers that set
    // keepGeometry and partialIntersections directly won't have their
    // values overwritten by the default clipMode.
    if (m_clipMode != ClipMode::eInsideKeep)
    {
        switch (m_clipMode)
        {
        case ClipMode::eInsideKeep:
            m_keepGeometry = KeepGeometry::eInside;
            m_partiallyIntersectedPrims = PartiallyIntersectedPrims::eKeep;
            break;
        case ClipMode::eInsideCutMesh:
            m_keepGeometry = KeepGeometry::eInside;
            m_partiallyIntersectedPrims = PartiallyIntersectedPrims::eKeepIntersection;
            break;
        case ClipMode::eInsideDiscard:
            m_keepGeometry = KeepGeometry::eInside;
            m_partiallyIntersectedPrims = PartiallyIntersectedPrims::eDiscard;
            break;
        case ClipMode::eOutsideKeep:
            m_keepGeometry = KeepGeometry::eOutside;
            m_partiallyIntersectedPrims = PartiallyIntersectedPrims::eKeep;
            break;
        case ClipMode::eOutsideDiscard:
            m_keepGeometry = KeepGeometry::eOutside;
            m_partiallyIntersectedPrims = PartiallyIntersectedPrims::eDiscard;
            break;
        }
    }

    if (m_keepGeometry == KeepGeometry::eOutside &&
        m_partiallyIntersectedPrims == PartiallyIntersectedPrims::eKeepIntersection)
    {
        SO_LOG_VERBOSE("Keep Geometry Outside does not support Cut Mesh; falling back to Keep.");
        m_partiallyIntersectedPrims = PartiallyIntersectedPrims::eKeep;
    }

    std::ostringstream oss;
    oss << "Resolved clip box: [" << m_resolvedMin[0] << ", " << m_resolvedMin[1] << ", " << m_resolvedMin[2]
        << "] -> [" << m_resolvedMax[0] << ", " << m_resolvedMax[1] << ", " << m_resolvedMax[2] << "]\n";
    SO_LOG_VERBOSE(oss.str().c_str());

    return { true };
}

bool _getBoolCustomFlag(const UsdPrim& prim, const TfToken& key, bool defaultValue = false)
{
    if (!prim)
    {
        return defaultValue;
    }

    // Fast check if the key exists in customData
    if (!prim.HasCustomDataKey(key))
    {
        return defaultValue;
    }

    VtValue v = prim.GetCustomDataByKey(key);
    if (v.IsEmpty())
    {
        return defaultValue;
    }

    // Accept bool or numeric representations
    if (v.IsHolding<bool>())
    {
        return v.UncheckedGet<bool>();
    }
    if (v.IsHolding<int>())
    {
        return v.UncheckedGet<int>() != 0;
    }
    if (v.IsHolding<std::string>())
    {
        const auto& s = v.UncheckedGet<std::string>();
        return (s == "1" || s == "true" || s == "True");
    }
    return defaultValue;
}

bool _primHasNoDelete(const UsdPrim& prim)
{
    return _getBoolCustomFlag(prim, _tokens->noDelete, /*defaultValue=*/false);
}

bool _primHiddenInStageWindow(const UsdPrim& prim)
{
    return _getBoolCustomFlag(prim, _tokens->hideInStageWindow, /*defaultValue=*/false);
}

void BoxClipOperation::preProcessPrims(std::vector<UsdPrim>& prims)
{
    // Use concurrent containers for thread-safe operations
    tbb::concurrent_vector<UsdPrim> instancesToFlatten;
    tbb::concurrent_vector<UsdPrim> deletePrims;
    tbb::concurrent_vector<size_t> indicesToKeep;

    auto filterPrimsFn = [&](const tbb::blocked_range<size_t>& range)
    {
        for (size_t i = range.begin(); i != range.end(); ++i)
        {
            const auto& prim = prims[i];

            if (_primHiddenInStageWindow(prim))
            {
                continue;
            }

            UsdGeomImageable usd_imageable(prim);
            if (!usd_imageable)
            {
                continue;
            }

            if (prim.IsInstance())
            {
                GfBBox3d bound = usd_imageable.ComputeWorldBound(UsdTimeCode::Default(), UsdGeomTokens->default_);
                GfRange3d bbox = bound.ComputeAlignedBox();
                GfRange3d clipBox(GfVec3d(m_resolvedMin.data()), GfVec3d(m_resolvedMax.data()));

                bool isOutside = clipBox.IsOutside(bbox);
                bool isContained = clipBox.Contains(bbox);

                bool keepOutside = (m_keepGeometry == KeepGeometry::eOutside);
                bool shouldDelete = keepOutside ? isContained : isOutside;

                if (shouldDelete)
                {
                    if (!_primHasNoDelete(prim))
                    {
                        deletePrims.push_back(prim);
                    }
                }
                else
                {
                    instancesToFlatten.push_back(prim);
                }
            }
            else if (prim.IsA<UsdGeomMesh>())
            {
                indicesToKeep.push_back(i);
            }
        }
    };

    if (getContext()->singleThreaded)
    {
        filterPrimsFn(tbb::blocked_range<size_t>(0, prims.size()));
    }
    else
    {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, prims.size()), filterPrimsFn);
    }

    // Sequential reconstruction of prims vector
    std::vector<UsdPrim> newPrims;
    newPrims.reserve(indicesToKeep.size());
    for (size_t idx : indicesToKeep)
    {
        newPrims.push_back(prims[idx]);
    }
    prims = std::move(newPrims);

    // Before deleting anything, flatten instances to remove all references.
    // Note that a referenced source prim might be outside the clipbox, but still have some
    // instances that are within the clipbox.
    for (const UsdPrim& instancePrim : instancesToFlatten)
    {
        bool success = _flattenInstance(instancePrim);
        if (success)
        {
            // add all children (including nested ones) to the prims to process vector
            for (const UsdPrim& child : instancePrim.GetDescendants())
            {
                if (child.IsA<UsdGeomMesh>())
                {
                    prims.push_back(child);
                }
            }
        }
    }

    _deletePrims(getUsdStage(), std::vector<UsdPrim>(deletePrims.begin(), deletePrims.end()), true);
}

ProcessedData* BoxClipOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{
    try
    {
        UsdGeomMesh usd_mesh(prim);

        if (!usd_mesh || (m_clipBoxDef == ClipBoxDefinition::eByPrim && prim.GetPath() == m_clipBoxPrimSdfPath))
        {
            return new ProcessedHostMeshData{ {}, prim, false /* don't write, so leave existing mesh unchanged */ };
        }

        GfBBox3d bound = usd_mesh.ComputeWorldBound(UsdTimeCode::Default(), UsdGeomTokens->default_);
        GfRange3d bbox = bound.ComputeAlignedBox();
        GfRange3d clipBox(GfVec3d(m_resolvedMin.data()), GfVec3d(m_resolvedMax.data()));

        bool bbox_contained = clipBox.Contains(bbox);
        bool bbox_outside = clipBox.IsOutside(bbox);

        if (m_keepGeometry == KeepGeometry::eOutside)
        {
            // Keep Outside mode: cull geometry INSIDE the box, keep geometry OUTSIDE

            if (bbox_outside)
            {
                // Mesh is completely outside the clip box - keep it unchanged
                return new ProcessedHostMeshData{ {}, prim, false /* don't write, so leave existing mesh unchanged */ };
            }

            if (bbox_contained)
            {
                // Mesh is completely inside the clip box - delete it
                return new ProcessedHostMeshData{ {}, prim, true /* replace existing mesh with empty mesh */ };
            }

            // Partial intersection (only Keep and Discard are supported in Keep Outside mode)
            if (m_partiallyIntersectedPrims == PartiallyIntersectedPrims::eDiscard)
            {
                // Discard any mesh that touches the box
                return new ProcessedHostMeshData{ {}, prim, true /* replace existing mesh with empty mesh */ };
            }

            // Keep the mesh unchanged (it has parts outside the box)
            return new ProcessedHostMeshData{ {}, prim, false /* don't write, so leave existing mesh unchanged */ };
        }
        else
        {
            // Normal mode: cull geometry OUTSIDE the box, keep geometry INSIDE

            if (bbox_contained)
            {
                return new ProcessedHostMeshData{ {}, prim, false /* don't write, so leave existing mesh unchanged */ };
            }

            if (bbox_outside)
            {
                return new ProcessedHostMeshData{ {}, prim, true /* replace existing mesh with empty mesh */ };
            }

            if (m_partiallyIntersectedPrims == PartiallyIntersectedPrims::eDiscard)
            {
                return new ProcessedHostMeshData{ {}, prim, true /* replace existing mesh with empty mesh */ };
            }
            else if (m_partiallyIntersectedPrims == PartiallyIntersectedPrims::eKeep)
            {
                return new ProcessedHostMeshData{ {}, prim, false /* don't write, so leave existing mesh unchanged */ };
            }

            // Clip the mesh
            omo::usd::HostMeshData mesh(usd_mesh);
            omo::HostMeshData clipped_mesh;
            try
            {
                clipped_mesh = omo::boxClip(mesh, m_resolvedMin, m_resolvedMax);
                return new ProcessedHostMeshData{ clipped_mesh,
                                                  prim,
                                                  true /* replace existing mesh with clipped mesh  */ };
            }
            catch (const std::exception& e)
            {
                std::string errorMsg =
                    std::string("Failed to clip mesh on prim ") + prim.GetPath().GetText() + ": " + e.what();
                SO_LOG_ERROR(errorMsg.c_str());
                return new ProcessedHostMeshData{ {}, prim, false /* keep original mesh on clip failure */ };
            }
        }
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        SO_LOG_ERROR(errorMsg.c_str());
        return nullptr;
    }
}

} // namespace omni::scene::optimizer
