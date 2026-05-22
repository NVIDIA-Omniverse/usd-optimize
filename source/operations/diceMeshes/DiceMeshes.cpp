// SPDX-FileCopyrightText: Copyright (c) 2023-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//
#include "DiceMeshes.h"

// OmniMeshOps
#include <OmniMeshOps/Slice.h>
#include <OmniMeshOps/usd/MeshData.h>

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Utils.h>

// Split Meshes Operation
#include "splitMeshes/SplitMeshes.h"

// Usd
#include <pxr/usd/usdUtils/stageCache.h>

PXR_NAMESPACE_USING_DIRECTIVE

SO_PLUGIN_INIT(omni::scene::optimizer::DiceMeshesOperation);


namespace omni::scene::optimizer
{

/// Constants
constexpr const char* s_categoryDiceMeshes = "DICEMESHES";

DiceMeshesOperation::DiceMeshesOperation()
    : OmniOperation("diceMeshes", "Dice Meshes", "This operation dices up meshes into a grid.")
    , m_splitDices(false)
    , m_gridType(GridType::eRegular)
    , m_gridCellSize({ 0, 0, 0 })
    , m_gridOrigin({ 0, 0, 0 })
    , m_upX({ 1, 0, 0 })
    , m_upY({ 0, 1, 0 })
    , m_upZ({ 0, 0, 1 })
    , m_advancedSettings(false)
{

    addArgument("paths", "Meshes To Process", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("splitDices", "Split Dices", kDisplayTypeBool, "Split diced meshes into separate prims", m_splitDices);

    addArgument("gridType", "Grid Type", kDisplayTypeEnum, "The type of grid", m_gridType)
        .setEnumValues<GridType>({ { GridType::eRegular, "Regular" }, { GridType::eIrregular, "Irregular" } });

    addArgument("cutHeightsX", "Cut Heights X", kDisplayTypeText, "Cut heights in the X direction", m_cutHeightsX)
        .setVisibleIf("gridType == 1")
        .setPlaceholder("Space-separated cut heights for the x direction");
    addArgument("cutHeightsY", "Cut Heights Y", kDisplayTypeText, "Cut heights in the Y direction", m_cutHeightsY)
        .setVisibleIf("gridType == 1")
        .setPlaceholder("Space-separated cut heights for the y direction");
    addArgument("cutHeightsZ", "Cut Heights Z", kDisplayTypeText, "Cut heights in the Z direction", m_cutHeightsZ)
        .setVisibleIf("gridType == 1")
        .setPlaceholder("Space-separated cut heights for the z direction");

    addJoin(
        "Grid Cell Size",
        "Grid Cell Size",
        addArgument("gridCellX", "Grid Cell X", kDisplayTypeFloatSlider, "Grid Cell Height X", m_gridCellSize[0]).setMin(0),
        addArgument("gridCellY", "Grid Cell Y", kDisplayTypeFloatSlider, "Grid Cell Height Y", m_gridCellSize[1]).setMin(0),
        addArgument("gridCellZ", "Grid Cell Z", kDisplayTypeFloatSlider, "Grid Cell Height Z", m_gridCellSize[2]).setMin(0))
        .setVisibleIf("gridType == 0");

    addJoin("Grid Origin",
            "Grid Origin",
            addArgument("gridOriginX", "Grid Origin X", kDisplayTypeFloatSlider, "Grid Origin X", m_gridOrigin[0]),
            addArgument("gridOriginY", "Grid Origin Y", kDisplayTypeFloatSlider, "Grid Origin Y", m_gridOrigin[1]),
            addArgument("gridOriginZ", "Grid Origin Z", kDisplayTypeFloatSlider, "Grid Origin Z", m_gridOrigin[2]))
        .setVisibleIf("gridType == 0");

    addArgument("advancedSettings", "Advanced Settings", kDisplayTypeBool, "Toggle advanced settings", m_advancedSettings);

    addGroup("advancedGroup",
             addJoin("Up-vector X",
                     "Up-vector X",
                     addArgument("upVectorAx", "Up-vector A x", kDisplayTypeFloatSlider, "Up-vector X", m_upX[0]),
                     addArgument("upVectorAy", "Up-vector A y", kDisplayTypeFloatSlider, "Up-vector X", m_upX[1]),
                     addArgument("upVectorAz", "Up-vector A z", kDisplayTypeFloatSlider, "Up-vector X", m_upX[2])),

             addJoin("Up-vector Y",
                     "Up-vector Y",
                     addArgument("upVectorBx", "Up-vector B x", kDisplayTypeFloatSlider, "Up-vector Y", m_upY[0]),
                     addArgument("upVectorBy", "Up-vector B y", kDisplayTypeFloatSlider, "Up-vector Y", m_upY[1]),
                     addArgument("upVectorBz", "Up-vector B z", kDisplayTypeFloatSlider, "Up-vector Y", m_upY[2])),

             addJoin("Up-vector Z",
                     "Up-vector Z",
                     addArgument("upVectorCx", "Up-vector C x", kDisplayTypeFloatSlider, "Up-vector Z", m_upZ[0]),
                     addArgument("upVectorCy", "Up-vector C y", kDisplayTypeFloatSlider, "Up-vector Z", m_upZ[1]),
                     addArgument("upVectorCz", "Up-vector C z", kDisplayTypeFloatSlider, "Up-vector Z", m_upZ[2])))
        .setVisibleIf("advancedSettings");
}


std::string DiceMeshesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion DiceMeshesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string DiceMeshesOperation::getCategory() const
{
    return s_categoryDiceMeshes;
}


/// Get the display group.
std::string DiceMeshesOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}


OperationResult DiceMeshesOperation::executePre()
{
    auto parseNumbers = [](const std::string& inputStr) -> std::vector<double>
    {
        std::stringstream ss(inputStr);
        std::string token;
        std::vector<double> result;
        while (std::getline(ss, token, ' '))
        {
            try
            {
                auto val = std::stof(token);
                result.emplace_back(val);
            }
            catch (...)
            {
            }
        }
        return result;
    };

    m_parsedCutHeightsX = parseNumbers(m_cutHeightsX);
    m_parsedCutHeightsY = parseNumbers(m_cutHeightsY);
    m_parsedCutHeightsZ = parseNumbers(m_cutHeightsZ);

    return { true };
}


void DiceMeshesOperation::executePost(const TotalStats& totalStats)
{
    // super call
    OmniOperation::executePost(totalStats);

    // run split to create separate prims for each diced mesh? note: this is a temp workaround, in future we should be
    // able to get back the dices from omnimesh directly so we can author them as separate prims without having to
    // invoke split separately
    if (!m_splitDices)
    {
        return;
    }

    // get the split operation
    auto& core = SceneOptimizerCore::getInstance();
    auto splitOp = core.getOperation("splitMeshes");
    if (splitOp == nullptr)
    {
        return; // LCOV_EXCL_LINE
    }

    // set user data
    SplitMeshesUserData userData;
    userData.paths = m_meshPrimPaths;
    userData.splitCollocatedPoints = true;
    splitOp->setUserData(&userData);

    // execute split. No args required, using the defaults other than the user data.
    ExecutionContext childContext;
    so_execution_context_copy(&childContext, getContext());
    splitOp->execute(&childContext, JsObject());
    so_execution_context_free(&childContext);
}


ProcessedData* DiceMeshesOperation::processMesh(const UsdPrim& prim, tbb::task_group_context&)
{
    using namespace omo::usd;

    try
    {
        UsdGeomMesh usd_mesh(prim);
        HostMeshData mesh(usd_mesh, { omo::Defect::None });
        HostMeshData diced_mesh;

        if (m_gridType == GridType::eRegular)
        {
            diced_mesh = omo::gridSlice(mesh, m_gridCellSize, m_gridOrigin, m_upX, m_upY, m_upZ);
        }
        else
        {
            if (!m_parsedCutHeightsX.empty())
            {
                diced_mesh = omo::slice(mesh, m_upX, m_parsedCutHeightsX);
            }
            if (!m_parsedCutHeightsY.empty())
            {
                diced_mesh = omo::slice(mesh, m_upY, m_parsedCutHeightsY);
            }
            if (!m_parsedCutHeightsZ.empty())
            {
                diced_mesh = omo::slice(mesh, m_upZ, m_parsedCutHeightsZ);
            }
        }

        return new ProcessedHostMeshData{ diced_mesh, prim };
    }
    catch (const std::exception& e)
    {
        std::string errorMsg = prim.GetPath().GetAsString() + ": " + std::string(e.what());
        SO_LOG_ERROR(errorMsg.c_str());
    }
    return nullptr;
}

} // namespace omni::scene::optimizer
