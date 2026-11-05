// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "PrimitiveToMesh.h"

#include "PrimitiveToMeshProcessedData.h"

#include <cmath>
#include <limits>

// Carbonite
#include <carb/profiler/Profile.h>

// Usd Optimize Core
#include <usd_optimize/core/Core.h>

// USD
#include <pxr/usd/usd/primCompositionQuery.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/capsule.h>
#include <pxr/usd/usdGeom/cone.h>
#include <pxr/usd/usdGeom/cube.h>
#include <pxr/usd/usdGeom/cylinder.h>
#include <pxr/usd/usdGeom/metrics.h>
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdGeom/sphere.h>

PXR_NAMESPACE_USING_DIRECTIVE

USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::PrimitiveToMeshOperation);


namespace usd_optimize
{

/// Constants
constexpr const char* s_categoryPrimitivesToMeshes = "PRIMITIVES_TO_MESHES";

/// PrimitiveToMeshOperation methods
PrimitiveToMeshOperation::PrimitiveToMeshOperation()
    : OmniOperation("primitivesToMeshes",
                    "Primitives to Meshes",
                    "Replace gprim types sphere, cylinder, capsule, cone, or cube with a mesh approximation.")
{
    addArgument("paths",
                "Primitives to convert",
                kDisplayTypePrimPaths,
                "Optional list of prim paths to consider",
                m_meshPrimPaths)
        .setPlaceholder("Add primitives or all will be processed");

    // Spheres
    addArgument("convertSpheres",
                "Convert sphere primitives",
                kDisplayTypeBool,
                "Whether or not to generate meshes from sphere prims.",
                m_options.convertSpheres);

    addGroup("spheres",
             addArgument("sphereLongitudeDivisions",
                         "Longitude divisions",
                         kDisplayTypeInt,
                         "The number of longitudinal divisions in which to divide spheres.  Must be at least 3.",
                         m_options.sphereParameters.n_radial)
                 .setMin(3)
                 .setMax(s_maxPrimitiveDivisions),

             addArgument("sphereLatitudeDivisions",
                         "Latitude divisions",
                         kDisplayTypeInt,
                         "The number of latitudinal divisions in which to divide spheres.  Must be at least 2.",
                         m_options.sphereParameters.n_axial)
                 .setMin(2)
                 .setMax(s_maxPrimitiveDivisions))
        .setVisibleIf("convertSpheres"); // group spheres

    // Cylinders
    addArgument("convertCylinders",
                "Convert cylinder primitives",
                kDisplayTypeBool,
                "Whether or not to generate meshes from cylinder prims.",
                m_options.convertCylinders);

    addGroup("cylinders",
             addArgument("cylinderWallDivisions",
                         "Wall divisions",
                         kDisplayTypeInt,
                         "The number of divisions to make around the cylinder wall.  Must be at least 3.",
                         m_options.cylinderParameters.n_radial)
                 .setMin(3)
                 .setMax(s_maxPrimitiveDivisions),

             addArgument("cylinderLatitudeDivisions",
                         "Length divisions",
                         kDisplayTypeInt,
                         "The number of end-to-end divisions to make along the cylinder.  Must be positive.",
                         m_options.cylinderParameters.n_axial)
                 .setMin(1)
                 .setMax(s_maxPrimitiveDivisions),

             addArgument("cylinderEndcaps",
                         "Generate endcaps",
                         kDisplayTypeBool,
                         "Whether or not to add endcaps to generated cylinder meshes.",
                         m_options.cylinderParameters.capped))
        .setVisibleIf("convertCylinders"); // group cylinders

    // Capsules
    addArgument("convertCapsules",
                "Convert capsule primitives",
                kDisplayTypeBool,
                "Whether or not to generate meshes from capsule prims.",
                m_options.convertCapsules);

    addGroup("capsules",
             addArgument("capsuleRadialDivisions",
                         "Radial divisions",
                         kDisplayTypeInt,
                         "The number of divisions around the capsule. Must be at least 3.",
                         m_options.capsuleParameters.n_radial)
                 .setMin(3)
                 .setMax(s_maxPrimitiveDivisions),

             addArgument("capsuleLengthDivisions",
                         "Length divisions",
                         kDisplayTypeInt,
                         "The number of lengthwise divisions along the cylindrical body. Must be positive.",
                         m_options.capsuleParameters.n_axial)
                 .setMin(1)
                 .setMax(s_maxPrimitiveDivisions),

             addArgument("capsuleCapDivisions",
                         "Cap latitude divisions",
                         kDisplayTypeInt,
                         "The number of latitude divisions per hemispherical endcap. Must be positive.",
                         m_options.capsuleParameters.n_cap)
                 .setMin(1)
                 .setMax(s_maxPrimitiveDivisions))
        .setVisibleIf("convertCapsules");

    // Cones
    addArgument("convertCones",
                "Convert cone primitives",
                kDisplayTypeBool,
                "Whether or not to generate meshes from cone prims.",
                m_options.convertCones);

    addGroup("cones",
             addArgument("coneSideDivisions",
                         "Side divisions",
                         kDisplayTypeInt,
                         "The number of divisions to make around the side of the cone.  Must be at least 3.",
                         m_options.coneParameters.n_radial)
                 .setMin(3)
                 .setMax(s_maxPrimitiveDivisions),

             addArgument("coneLengthDivisions",
                         "Length divisions",
                         kDisplayTypeInt,
                         "The number of divisions to make along the length of the cone.  Must be positive.",
                         m_options.coneParameters.n_axial)
                 .setMin(1)
                 .setMax(s_maxPrimitiveDivisions),

             addArgument("coneBases",
                         "Generate bases",
                         kDisplayTypeBool,
                         "Whether or not to add a base to generated cone meshes.",
                         m_options.coneParameters.capped))
        .setVisibleIf("convertCones"); // group cones

    // Cubes
    addArgument("convertCubes",
                "Convert cube primitives",
                kDisplayTypeBool,
                "Whether or not to generate meshes from cube prims.",
                m_options.convertCubes);
}


std::string PrimitiveToMeshOperation::getDocumentation() const
{
    return "This operation replaces gprim types sphere, cylinder, capsule, cone, and "
           "cube with a mesh approximation. This allows the geometry to be "
           "used with operations that require mesh types.\n\n"
           "Capsule height describes the cylindrical body, excluding the two hemispherical caps. "
           "Capsule meshes approximate this height and preserve the radius and axis without stretching the caps. "
           "Use capsuleRadialDivisions for circumferential detail, capsuleLengthDivisions for body "
           "subdivisions, and capsuleCapDivisions for rounded-end detail. Increasing these values "
           "increases mesh size. Capsules are always closed.\n\n"
           "Capsule prototypes have radius one. The height/radius ratio is first converted to a single-precision float. "
           "All eight exponent bits and the highest 20 fraction bits, without the sign bit, are encoded as seven "
           "uppercase hexadecimal digits after ``_hr``. The lowest three fraction bits are zeroed for geometry, "
           "so ratios with the same code share a prototype without ratio-collision suffixes, independently of "
           "conversion order. Capsules with "
           "the same quantized ratio, axis, and subdivision settings share a prototype; a uniform scale restores each "
           "capsule's radius, while its body height approximates the original according to the quantized ratio. "
           "Capsules with invalid dimensions (nonpositive radius, "
           "negative height, nonfinite dimensions, or a ratio outside the finite float range) are skipped.";
}

std::string PrimitiveToMeshOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}

UsdOptimizePluginVersion PrimitiveToMeshOperation::getVersion() const
{
    return { 1, 0, 0 };
}

std::string PrimitiveToMeshOperation::getCategory() const
{
    return s_categoryPrimitivesToMeshes;
}

std::string PrimitiveToMeshOperation::getDisplayGroup() const
{
    return s_displayGroupGeometry;
}

ResolveFilter PrimitiveToMeshOperation::resolveFilter()
{
    const bool spheres = m_options.convertSpheres;
    const bool cylinders = m_options.convertCylinders;
    const bool capsules = m_options.convertCapsules;
    const bool cones = m_options.convertCones;
    const bool cubes = m_options.convertCubes;

    UsdPrimCompositionQuery::Filter composition_query_filter;
    composition_query_filter.arcTypeFilter = UsdPrimCompositionQuery::ArcTypeFilter::Reference;

    return [=](const UsdPrim& prim, UsdPrimRange::iterator&) -> bool
    {
        return !_hasAuthoredTimeSamples(prim) &&
               !UsdPrimCompositionQuery(prim, composition_query_filter).GetCompositionArcs().size() &&
               ((spheres && prim.IsA<UsdGeomSphere>()) || (cylinders && prim.IsA<UsdGeomCylinder>()) ||
                (capsules && prim.IsA<UsdGeomCapsule>()) || (cones && prim.IsA<UsdGeomCone>()) ||
                (cubes && prim.IsA<UsdGeomCube>()));
    };
}

ProcessedData* PrimitiveToMeshOperation::processMesh(const UsdPrim& usd_prim, tbb::task_group_context&)
{
    using namespace omo;

    omo::PrimitiveType::Enum primitive_type = omo::PrimitiveType::None;
    PrimitiveMeshParameters* meshParameters = nullptr;

    PrimitiveUpAxis up_axis = omo::importUpAxis(UsdGeomGetStageUpAxis(getUsdStage()));
    GfVec4d scale(1.0);
    double capsule_height_radius_ratio = 2.0;

    if (usd_prim.IsA<UsdGeomSphere>())
    {
        primitive_type = omo::PrimitiveType::Sphere;
        meshParameters = &m_options.sphereParameters;
        double radius;
        if (UsdGeomSphere(usd_prim).GetRadiusAttr().Get(&radius))
        {
            scale[0] = scale[1] = scale[2] = radius;
        }
        ++m_prim_report.sphereCount;
    }
    else if (usd_prim.IsA<UsdGeomCylinder>())
    {
        primitive_type = omo::PrimitiveType::Cylinder;
        meshParameters = &m_options.cylinderParameters;
        UsdGeomCylinder cylinder(usd_prim);
        TfToken axis;
        if (cylinder.GetAxisAttr().Get(&axis))
        {
            up_axis = omo::importUpAxis(axis);
        }
        double radius;
        if (cylinder.GetRadiusAttr().Get(&radius))
        {
            scale[(int(up_axis) + 1) % 3] = scale[(int(up_axis) + 2) % 3] = radius;
        }
        double height;
        if (cylinder.GetHeightAttr().Get(&height))
        {
            scale[int(up_axis)] = 0.5 * height;
        }
        ++m_prim_report.cylinderCount;
    }
    else if (usd_prim.IsA<UsdGeomCapsule>())
    {
        primitive_type = omo::PrimitiveType::Capsule;
        meshParameters = &m_options.capsuleParameters;
        UsdGeomCapsule capsule(usd_prim);
        TfToken axis;
        if (capsule.GetAxisAttr().Get(&axis))
        {
            up_axis = omo::importUpAxis(axis);
        }
        double radius = 0.0;
        double height = -1.0;
        capsule.GetRadiusAttr().Get(&radius);
        capsule.GetHeightAttr().Get(&height);
        if (!std::isfinite(radius) || radius <= 0.0 || !std::isfinite(height) || height < 0.0)
        {
            USD_OPTIMIZE_LOG_WARN("Skipping capsule %s: expected finite dimensions, radius > 0 and height >= 0",
                                  usd_prim.GetPath().GetText());
            return nullptr;
        }
        // Canonicalize zero so +0 and -0 heights use the same prototype.
        capsule_height_radius_ratio = height == 0.0 ? 0.0 : height / radius;
        if (!std::isfinite(capsule_height_radius_ratio) ||
            capsule_height_radius_ratio > std::numeric_limits<float>::max())
        {
            USD_OPTIMIZE_LOG_WARN("Skipping capsule %s: height/radius ratio is outside the finite float range",
                                  usd_prim.GetPath().GetText());
            return nullptr;
        }
        scale[0] = scale[1] = scale[2] = radius;
        ++m_prim_report.capsuleCount;
    }
    else if (usd_prim.IsA<UsdGeomCone>())
    {
        primitive_type = omo::PrimitiveType::Cone;
        meshParameters = &m_options.coneParameters;
        UsdGeomCone cone(usd_prim);
        TfToken axis;
        if (cone.GetAxisAttr().Get(&axis))
        {
            up_axis = omo::importUpAxis(axis);
        }
        double radius;
        if (cone.GetRadiusAttr().Get(&radius))
        {
            scale[(int(up_axis) + 1) % 3] = scale[(int(up_axis) + 2) % 3] = radius;
        }
        double height;
        if (cone.GetHeightAttr().Get(&height))
        {
            scale[int(up_axis)] = 0.5 * height;
        }
        ++m_prim_report.coneCount;
    }
    else // Because of resolveFilter(), this must be a cube
    {
        primitive_type = omo::PrimitiveType::Cube;
        meshParameters = &m_options.cubeParameters;
        double size;
        if (UsdGeomCube(usd_prim).GetSizeAttr().Get(&size))
        {
            scale[0] = scale[1] = scale[2] = 0.5 * size;
        }
        ++m_prim_report.cubeCount;
    }

    if (getContext()->verbose)
    {
        const std::string primPath = usd_prim.GetPath().GetAsString();
        const std::string primType = std::string("UsdGeom") + usd_prim.GetTypeName().GetText();
        USD_OPTIMIZE_LOG_VERBOSE("Prim: %s\nType %s", primPath.c_str(), primType.c_str());
    }

    const omo::Primitive prim = {
        { scale[0], 0.0, 0.0, 0.0, 0.0, scale[1], 0.0, 0.0, 0.0, 0.0, scale[2], 0.0, 0.0, 0.0, 0.0, 1.0 },
        primitive_type
    };

    return new PrimitiveToMeshProcessedData(usd_prim,
                                            prim,
                                            up_axis,
                                            *meshParameters,
                                            &m_hash_cache,
                                            true,
                                            omo::HostMesh(),
                                            capsule_height_radius_ratio);
}

inline void logPrimitiveReplacement(const char* name, size_t count)
{
    if (count != 1)
    {
        USD_OPTIMIZE_LOG_INFO("Replaced %zu %ss with meshes", count, name);
    }
    else
    {
        USD_OPTIMIZE_LOG_INFO("Replaced 1 %s with a mesh", name);
    }
}

void PrimitiveToMeshOperation::executePost(const TotalStats& totalStats)
{
    OmniOperation::executePost(totalStats);

    logPrimitiveReplacement("sphere", m_prim_report.sphereCount.load());
    logPrimitiveReplacement("cylinder", m_prim_report.cylinderCount.load());
    logPrimitiveReplacement("capsule", m_prim_report.capsuleCount.load());
    logPrimitiveReplacement("cone", m_prim_report.coneCount.load());
    logPrimitiveReplacement("cube", m_prim_report.cubeCount.load());

    m_hash_cache.clear();
}

} // namespace usd_optimize
