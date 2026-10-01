// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "Shrinkwrap.h"

#include <openvdb/tools/ShrinkwrapCore.h>

// Usd Optimize Core
#include <usd_optimize/core/Core.h>
#include <usd_optimize/core/ResolveSdfPaths.h>
#include <usd_optimize/core/Utils.h>

// USD
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/tf/stringUtils.h>
#include <pxr/base/vt/array.h>
#include <pxr/usd/usd/primRange.h>
#include <pxr/usd/usdGeom/mesh.h>
#include <pxr/usd/usdGeom/xformCache.h>
#include <pxr/usd/usdGeom/xformable.h>

#include <algorithm>
#include <cfloat>
#include <climits>
#include <cmath>
#include <limits>

PXR_NAMESPACE_USING_DIRECTIVE

// Register plugin with Usd Optimize
USD_OPTIMIZE_PLUGIN_INIT(usd_optimize::ShrinkwrapOperation);


namespace usd_optimize
{

using openvdb::shrinkwrap::ShrinkwrapMesh;
using openvdb::shrinkwrap::shrinkwrapMesh;
using openvdb::shrinkwrap::ShrinkwrapParams;
using openvdb::shrinkwrap::ShrinkwrapResult;

constexpr const char* s_categoryShrinkwrap = "SHRINKWRAP";


ShrinkwrapOperation::ShrinkwrapOperation()
    : Operation("shrinkwrap",
                "Shrinkwrap",
                "Convert meshes to a level set volume and extract a watertight mesh. "
                "Useful for closing holes, simplifying topology, and creating LODs.")
    , m_inputMode(InputMode::ePerMesh)
    , m_startTime(std::numeric_limits<double>::quiet_NaN())
    , m_endTime(std::numeric_limits<double>::quiet_NaN())
    , m_timeStep(1.0)
    , m_connectTimeSamples(true)
    , m_dim(512)
    , m_voxelSize(0.1)
    , m_erode(8.0)
    , m_threshold(0.0)
    , m_adaptivity(0.0)
    , m_extractLodPyramid(false)
{
    addArgument("paths",
                "Meshes to Shrinkwrap",
                kDisplayTypePrimPaths,
                "Optional list of prim paths/expressions to shrinkwrap",
                m_meshPrimPaths)
        .setPlaceholder("Add meshes or all will be processed");

    addArgument("inputMode",
                "Input Mode",
                kDisplayTypeEnum,
                "Process each mesh at default time, or combine all selected meshes sampled over a time range",
                m_inputMode)
        .setEnumValues<InputMode>({
            { InputMode::ePerMesh, "Per Mesh" },
            { InputMode::eTemporalCombined, "Temporal Combined" },
        });

    addArgument("startTime",
                "Start Time",
                kDisplayTypeFloat,
                "First time code to sample. NaN uses the stage start time code.",
                m_startTime)
        .setVisibleIf("inputMode == 1");

    addArgument("endTime",
                "End Time",
                kDisplayTypeFloat,
                "Last time code to sample. NaN uses the stage end time code.",
                m_endTime)
        .setVisibleIf("inputMode == 1");

    addArgument("timeStep",
                "Time Step",
                kDisplayTypeFloat,
                "Interval between temporal samples. The end time is always sampled.",
                m_timeStep)
        .setMin(std::numeric_limits<float>::min())
        .setRejectOutOfRange(true)
        .setVisibleIf("inputMode == 1");

    addArgument("connectTimeSamples",
                "Connect Time Samples",
                kDisplayTypeBool,
                "Fill motion between topology-compatible adjacent samples with swept triangle prisms",
                m_connectTimeSamples)
        .setVisibleIf("inputMode == 1");

    addArgument("outputPath",
                "Output Path",
                kDisplayTypePrimPath,
                "Required new absolute prim path for the combined static shrinkwrap mesh",
                m_outputPath)
        .setVisibleIf("inputMode == 1");

    addArgument("dim",
                "Grid Dimension",
                kDisplayTypeInt,
                "Grid dimension for the finest level. Controls resolution by specifying how many voxels along the "
                "largest axis. Higher values produce finer detail. Set to 0 to use Voxel Size instead.",
                m_dim)
        .setMin(0)
        .setVisible(false);

    addArgument("voxelSize",
                "Voxel Size",
                kDisplayTypeFloat,
                "Explicit voxel size. Smaller values produce finer detail but use more memory. "
                "Set to 0 to use Grid Dimension instead.",
                m_voxelSize)
        .setMin(0);

    // Level set controls
    addArgument("erode",
                "Erosion Steps",
                kDisplayTypeFloat,
                "Number of erosion steps. Controls how much the level set can shrink/erode the shape.",
                m_erode)
        .setMin(0);

    addArgument("threshold",
                "Closing Threshold",
                kDisplayTypeFloat,
                "Size of the largest geometric feature or gap beyond which the algorithm should erode "
                "no further. Higher values close more holes and gaps in the mesh.",
                m_threshold)
        .setMin(0);

    // Mesh output controls
    addArgument("adaptivity",
                "Adaptive Meshing Threshold",
                kDisplayTypeFloatSlider,
                "Controls adaptive meshing. 0 = no adaptive meshing (uniform tessellation), "
                "1 = most adaptive (fewest triangles).",
                m_adaptivity)
        .setMin(0)
        .setMax(1);

    // LOD
    addArgument("extractLodPyramid",
                "Extract LOD Pyramid",
                kDisplayTypeBool,
                "When enabled, extracts meshes for all LOD levels in the pyramid. "
                "When disabled, only the finest (tightest) level is output.",
                m_extractLodPyramid)
        .setVisible(false);
}


ShrinkwrapOperation::~ShrinkwrapOperation() = default;


std::string ShrinkwrapOperation::getDocumentation() const
{
    return R"DOC(This operation converts meshes to a level set volume using
`OpenVDB <https://www.openvdb.org/>`_ and extracts a watertight mesh back out. It is useful for closing
holes, simplifying topology, and creating LOD meshes. The algorithm rasterizes the input mesh into a
narrow-band level set, optionally erodes the surface to close gaps and holes, and extracts a new polygon
mesh from the resulting volume. The output mesh is written as a new sibling prim alongside the original,
which is preserved.

Input modes
-----------

The default ``Per Mesh`` mode processes each selected mesh independently at ``UsdTimeCode::Default`` and
writes a sibling output for each mesh. ``Temporal Combined`` mode evaluates every selected mesh across an
inclusive time range, combines all samples in world space, and writes one static swept-envelope mesh at
``outputPath``. ``startTime`` and ``endTime`` default to the stage time range when left as NaN. The end
time is always included even when ``timeStep`` does not land on it exactly.

Temporal combined output is a discrete sampled envelope, not an analytical continuous sweep. A smaller
``timeStep`` captures fast, rotating, or nonlinear motion more faithfully, but memory and processing cost
grow approximately with the number of meshes times the number of samples. An empty ``paths`` selection
samples every mesh on the stage and can therefore be expensive. The output is authored in world space
with transform inheritance reset so it remains static.

With ``connectTimeSamples`` disabled, each sample contributes only its own surface, with nothing bridging
the gap to the next one. If ``timeStep`` is too coarse for the motion, consecutive poses do not overlap in
space and the envelope is a set of disjoint shells, one per sample, rather than a single enclosing surface;
this looks like a plausible closed shape in a viewport while actually having holes between samples. To
check, decrease ``timeStep`` and confirm the envelope stops changing, or enable ``connectTimeSamples`` to
bridge topology-compatible samples with swept prisms instead.

When ``connectTimeSamples`` is enabled, corresponding triangles at adjacent samples are connected as
closed swept prisms before voxelization. This fills the interval between samples and produces a more
continuous envelope without requiring extremely small time steps. It requires stable point and topology
correspondence. Intervals with changing topology fall back to the sampled surfaces instead of prisms;
without prisms bridging the gap, poses that do not already overlap in space produce a **disconnected**
envelope with one shell per sample, which can look like a single closed surface in a viewport while
actually having holes running through it. If an interval changes topology, reduce ``timeStep`` around it so
consecutive samples' poses overlap directly. The operation logs an info-level count of how many intervals
fell back to sampled surfaces this way; a non-zero count on an asset that mixes deforming topology with
other motion is a signal to check for gaps. Swept prisms linearly approximate vertex motion: a triangle's
corners move in straight chords between samples, so on a rotating or otherwise nonlinear path the prism can
both overfill the inside of the arc and underfill its outside (the true swept extent lies outside the
chord). ``timeStep`` must be fine enough relative to the fastest rotation on the stage to keep both errors
small; when in doubt, decrease it and confirm the envelope extent stops changing.

``adaptivity`` trades containment margin for triangle count in this mode: at higher values it can pull
the envelope surface inward of positions the volume step actually swept, most visibly on flat regions.
The bounding box does not catch this — it is computed before the simplification pass runs. If the
envelope must contain the swept geometry, leave ``adaptivity`` at ``0`` in this mode, or verify
empirically that sampled points fall inside the output.

Choosing resolution
-------------------

``voxelSize`` is the dominant control and should be set first. It is the edge length of a level-set voxel,
in **stage units**: smaller values capture finer detail but cost cubically more memory and time, larger
values smooth the result and close bigger gaps. ``adaptivity`` (0-1) then simplifies flat regions of the
extracted mesh to reduce triangle count without re-running the volume step.

Scale and units
---------------

``voxelSize``, ``erode``, and ``threshold`` are all in stage units, so the right values depend on the
stage's ``metersPerUnit``. To get the same physical size in stage units, divide the desired size in metres
by ``metersPerUnit``: ``stageUnits = metres / metersPerUnit``. For example, a target resolution of 5 cm
(0.05 m) is ``voxelSize = 0.05`` on a stage authored in metres (``metersPerUnit`` = 1), but
``voxelSize = 5.0`` on a stage authored in centimetres (``metersPerUnit`` = 0.01) — 100x larger, since
each stage unit is 100x smaller. The starting configurations below use metre-scale examples; scale them by
``1 / metersPerUnit`` for other units.

Tuning order
------------

1. Set ``voxelSize`` for the target detail level (start small and increase until cost is acceptable).
2. Increase ``erode`` to close larger gaps and holes.
3. Adjust ``threshold`` to shift the extracted iso-surface inward or outward.
4. Raise ``adaptivity`` to thin out triangles on flat areas (costs containment margin in
   ``Temporal Combined`` mode; see `Input modes`_).

``erode`` is not a "how much to shrink" dial with a neutral value at 0; it is the number of constrained
erosion steps the level set is allowed to take, coarse to fine, while conforming toward the true surface.
At ``erode=0`` those conforming steps never run, so the output is the coarsest offset surface the algorithm
builds internally (governed by the mesh's bounding box, not by ``voxelSize``) merely upsampled to the
target resolution: a heavily bloated envelope, often several times the size of the input, not a
close-fitting wrap. Use the default of 8 as a starting point and raise it only to close larger gaps; do not
lower it toward 0 expecting a tighter result.

Recommended pipelines
---------------------

Often run after ``merge`` so a group of parts becomes one watertight shell; target the merged prims with
``paths``. Pairs well with ``decimateMeshes`` afterward for LOD generation.

Starting configurations
-----------------------

Values below assume a metre-scale stage (``metersPerUnit`` = 1); see `Scale and units`_ to rescale for
other units.

Conservative detail preservation:

.. code-block:: json

    [{"operation": "shrinkwrap", "voxelSize": 0.05, "erode": 8.0}]

Gap-closing / hole-filling (coarser, more erosion):

.. code-block:: json

    [{"operation": "shrinkwrap", "voxelSize": 0.2, "erode": 16.0, "adaptivity": 0.5}]

Motion envelope over a fixed time range (``Temporal Combined`` mode):

.. code-block:: json

    [{"operation": "shrinkwrap", "inputMode": 1, "startTime": 1, "endTime": 48, "timeStep": 1,
      "connectTimeSamples": true, "outputPath": "/World/Envelope", "voxelSize": 0.05}]
)DOC";
}


std::string ShrinkwrapOperation::getAuthor() const
{
    return USD_OPTIMIZE_TO_STRING(USD_OPTIMIZE_PLUGIN_AUTHOR);
}


UsdOptimizePluginVersion ShrinkwrapOperation::getVersion() const
{
    return { 1, 2, 0 };
}


std::string ShrinkwrapOperation::getCategory() const
{
    return s_categoryShrinkwrap;
}


std::string ShrinkwrapOperation::getDisplayGroup() const
{
    return s_displayGroupUtilities;
}


namespace
{

enum class AppendResult
{
    eSkipped,
    eAppended,
    eFatal,
    eTopologyChanged,
};


struct MeshSample
{
    std::vector<std::array<float, 3>> points;
    std::vector<std::array<int, 3>> triangles;
    int pointOffset = 0;
};


AppendResult appendMeshSample(const UsdGeomMesh& usdMesh,
                              const UsdTimeCode& timeCode,
                              UsdGeomXformCache& xformCache,
                              ShrinkwrapMesh& inputMesh,
                              std::string& error,
                              MeshSample* meshSample = nullptr)
{
    const UsdPrim& prim = usdMesh.GetPrim();
    const std::string primPath = prim.GetPath().GetAsString();

    VtVec3fArray usdPoints;
    VtIntArray faceVertexIndices;
    VtIntArray faceVertexCounts;
    if (!usdMesh.GetPointsAttr().Get(&usdPoints, timeCode) ||
        !usdMesh.GetFaceVertexIndicesAttr().Get(&faceVertexIndices, timeCode) ||
        !usdMesh.GetFaceVertexCountsAttr().Get(&faceVertexCounts, timeCode) || usdPoints.empty())
    {
        USD_OPTIMIZE_LOG_VERBOSE("Shrinkwrap: %s: skipping empty or unreadable mesh at time %s",
                                 primPath.c_str(),
                                 TfStringify(timeCode).c_str());
        return AppendResult::eSkipped;
    }

    std::string topologyMessage;
    if (!usdMesh.ValidateTopology(faceVertexIndices.AsConst(), faceVertexCounts.AsConst(), usdPoints.size(), &topologyMessage))
    {
        USD_OPTIMIZE_LOG_WARN("Shrinkwrap: %s: invalid topology at time %s: %s",
                              primPath.c_str(),
                              TfStringify(timeCode).c_str(),
                              topologyMessage.c_str());
        return AppendResult::eSkipped;
    }
    if (!arePointsFinite(usdPoints))
    {
        USD_OPTIMIZE_LOG_WARN("Shrinkwrap: %s: skipping non-finite points at time %s",
                              primPath.c_str(),
                              TfStringify(timeCode).c_str());
        return AppendResult::eSkipped;
    }

    xformCache.SetTime(timeCode);
    const GfMatrix4d localToWorld = xformCache.GetLocalToWorldTransform(prim);
    if (!isTransformFinite(localToWorld))
    {
        USD_OPTIMIZE_LOG_WARN("Shrinkwrap: %s: skipping non-finite transform at time %s",
                              primPath.c_str(),
                              TfStringify(timeCode).c_str());
        return AppendResult::eSkipped;
    }

    if (usdPoints.size() > static_cast<size_t>(INT_MAX) - inputMesh.points.size())
    {
        error = "Combined shrinkwrap input exceeds the supported point index range";
        return AppendResult::eFatal;
    }

    decltype(inputMesh.points) transformedPoints;
    transformedPoints.reserve(usdPoints.size());
    for (const GfVec3f& point : usdPoints)
    {
        const GfVec3d worldPoint = localToWorld.Transform(GfVec3d(point));
        const std::array<float, 3> transformed = { static_cast<float>(worldPoint[0]),
                                                   static_cast<float>(worldPoint[1]),
                                                   static_cast<float>(worldPoint[2]) };
        if (!std::isfinite(transformed[0]) || !std::isfinite(transformed[1]) || !std::isfinite(transformed[2]))
        {
            USD_OPTIMIZE_LOG_WARN("Shrinkwrap: %s: skipping points outside the supported float range at time %s",
                                  primPath.c_str(),
                                  TfStringify(timeCode).c_str());
            return AppendResult::eSkipped;
        }
        transformedPoints.push_back(transformed);
    }

    const int pointOffset = static_cast<int>(inputMesh.points.size());
    inputMesh.points.insert(inputMesh.points.end(), transformedPoints.begin(), transformedPoints.end());
    if (meshSample)
    {
        meshSample->points = transformedPoints;
        meshSample->triangles.clear();
        meshSample->pointOffset = pointOffset;
    }

    size_t index = 0;
    for (const int count : faceVertexCounts)
    {
        if (count == 3)
        {
            const std::array<int, 3> triangle = { faceVertexIndices[index],
                                                  faceVertexIndices[index + 1],
                                                  faceVertexIndices[index + 2] };
            inputMesh.triangles.push_back({ pointOffset + faceVertexIndices[index],
                                            pointOffset + faceVertexIndices[index + 1],
                                            pointOffset + faceVertexIndices[index + 2] });
            if (meshSample)
            {
                meshSample->triangles.push_back(triangle);
            }
        }
        else if (count == 4)
        {
            inputMesh.quads.push_back({ pointOffset + faceVertexIndices[index],
                                        pointOffset + faceVertexIndices[index + 1],
                                        pointOffset + faceVertexIndices[index + 2],
                                        pointOffset + faceVertexIndices[index + 3] });
            if (meshSample)
            {
                meshSample->triangles.push_back(
                    { faceVertexIndices[index], faceVertexIndices[index + 1], faceVertexIndices[index + 2] });
                meshSample->triangles.push_back(
                    { faceVertexIndices[index], faceVertexIndices[index + 2], faceVertexIndices[index + 3] });
            }
        }
        else
        {
            for (int vertex = 1; vertex < count - 1; ++vertex)
            {
                inputMesh.triangles.push_back({ pointOffset + faceVertexIndices[index],
                                                pointOffset + faceVertexIndices[index + vertex],
                                                pointOffset + faceVertexIndices[index + vertex + 1] });
                if (meshSample)
                {
                    meshSample->triangles.push_back({ faceVertexIndices[index],
                                                      faceVertexIndices[index + vertex],
                                                      faceVertexIndices[index + vertex + 1] });
                }
            }
        }
        index += static_cast<size_t>(count);
    }

    USD_OPTIMIZE_LOG_VERBOSE("Shrinkwrap: %s at time %s: appended %zu vertices and %zu faces",
                             primPath.c_str(),
                             TfStringify(timeCode).c_str(),
                             usdPoints.size(),
                             faceVertexCounts.size());
    return AppendResult::eAppended;
}


AppendResult appendSweptPrisms(const MeshSample& previous,
                               const MeshSample& current,
                               ShrinkwrapMesh& inputMesh,
                               const std::string& label)
{
    if (previous.points.size() != current.points.size() || previous.triangles != current.triangles)
    {
        USD_OPTIMIZE_LOG_WARN("Shrinkwrap: %s: topology changed between adjacent samples; using sampled surfaces only",
                              label.c_str());
        return AppendResult::eTopologyChanged;
    }

    size_t appendedPrisms = 0;
    for (const std::array<int, 3>& triangle : current.triangles)
    {
        bool moved = false;
        for (const int index : triangle)
        {
            const auto& start = previous.points[index];
            const auto& finish = current.points[index];
            moved |= start[0] != finish[0] || start[1] != finish[1] || start[2] != finish[2];
        }
        if (!moved)
        {
            continue;
        }

        const std::array<int, 3> previousIndices = { previous.pointOffset + triangle[0],
                                                     previous.pointOffset + triangle[1],
                                                     previous.pointOffset + triangle[2] };
        const std::array<int, 3> currentIndices = { current.pointOffset + triangle[0],
                                                    current.pointOffset + triangle[1],
                                                    current.pointOffset + triangle[2] };
        inputMesh.triangles.push_back({ previousIndices[2], previousIndices[1], previousIndices[0] });
        inputMesh.triangles.push_back(currentIndices);
        inputMesh.quads.push_back({ previousIndices[0], previousIndices[1], currentIndices[1], currentIndices[0] });
        inputMesh.quads.push_back({ previousIndices[1], previousIndices[2], currentIndices[2], currentIndices[1] });
        inputMesh.quads.push_back({ previousIndices[2], previousIndices[0], currentIndices[0], currentIndices[2] });
        ++appendedPrisms;
    }

    USD_OPTIMIZE_LOG_VERBOSE("Shrinkwrap: %s: appended %zu swept triangle prism(s)", label.c_str(), appendedPrisms);
    return appendedPrisms == 0 ? AppendResult::eSkipped : AppendResult::eAppended;
}


bool runShrinkwrap(const ShrinkwrapMesh& inputMesh,
                   unsigned int dim,
                   double voxelSize,
                   double erode,
                   double threshold,
                   double adaptivity,
                   bool extractLodPyramid,
                   const std::string& label,
                   ShrinkwrapResult& result,
                   std::string& error)
{
    constexpr float kHalfWidth = 3.0f;
    constexpr float kIsovalue = 0.0f;

    if (inputMesh.points.empty() || (inputMesh.triangles.empty() && inputMesh.quads.empty()))
    {
        error = "No valid sampled polygons were collected for " + label;
        return false;
    }

    std::array<float, 3> bboxMin = { FLT_MAX, FLT_MAX, FLT_MAX };
    std::array<float, 3> bboxMax = { -FLT_MAX, -FLT_MAX, -FLT_MAX };
    for (const auto& point : inputMesh.points)
    {
        for (int axis = 0; axis < 3; ++axis)
        {
            bboxMin[axis] = std::min(bboxMin[axis], point[axis]);
            bboxMax[axis] = std::max(bboxMax[axis], point[axis]);
        }
    }

    float maxLength = 0.0f;
    for (int axis = 0; axis < 3; ++axis)
    {
        maxLength = std::max(maxLength, bboxMax[axis] - bboxMin[axis]);
    }

    const float userVoxelSize = static_cast<float>(voxelSize);
    const float dimDenominator = static_cast<float>(dim) - 2.0f * (kHalfWidth + 1.0f);
    float dimVoxelSize = -1.0f;
    if (dimDenominator > 0.0f && maxLength > 0.0f)
    {
        dimVoxelSize = maxLength / dimDenominator;
    }

    float effectiveVoxelSize = -1.0f;
    if (dimVoxelSize > 0.0f)
    {
        effectiveVoxelSize = userVoxelSize > 0.0f ? std::max(userVoxelSize, dimVoxelSize) : dimVoxelSize;
    }
    else if (userVoxelSize > 0.0f)
    {
        effectiveVoxelSize = userVoxelSize;
    }
    else
    {
        error = "Cannot compute a valid voxel size for " + label + " (dim too small and no explicit voxelSize set)";
        return false;
    }

    const double computedDim =
        static_cast<double>(maxLength) / effectiveVoxelSize + 2.0 * static_cast<double>(kHalfWidth + 1.0f);
    const int effectiveDim = computedDim > static_cast<double>(INT_MAX) ? INT_MAX : static_cast<int>(computedDim);
    const int minDim = static_cast<int>(2.0f * (kHalfWidth + 1.0f)) + 2;
    if (effectiveDim < minDim)
    {
        error = TfStringPrintf(
            "Effective grid dimension %d for %s is below minimum %d "
            "(voxel size %.4f is too large for bounding box %.4f)",
            effectiveDim,
            label.c_str(),
            minDim,
            effectiveVoxelSize,
            maxLength);
        return false;
    }

    USD_OPTIMIZE_LOG_VERBOSE(
        "Shrinkwrap: %s: effectiveVoxelSize=%.6f, effectiveDim=%d "
        "(dimLimit=%u, dimVoxelSize=%.6f, userVoxelSize=%.6f)",
        label.c_str(),
        effectiveVoxelSize,
        effectiveDim,
        dim,
        dimVoxelSize,
        userVoxelSize);

    ShrinkwrapParams params;
    params.voxelSize = effectiveVoxelSize;
    params.halfWidth = kHalfWidth;
    params.erode = static_cast<float>(erode);
    params.threshold = static_cast<float>(threshold);
    params.isovalue = kIsovalue;
    params.adaptivity = static_cast<float>(adaptivity);
    params.extractLodPyramid = extractLodPyramid;
    result = shrinkwrapMesh(inputMesh, params);

    if (!result.error.empty())
    {
        error = result.error;
        return false;
    }
    if (result.lodMeshes.empty())
    {
        error = "Shrinkwrap produced no output meshes for " + label;
        return false;
    }

    USD_OPTIMIZE_LOG_INFO("Shrinkwrap: %s: generated %zu LOD mesh(es)", label.c_str(), result.lodMeshes.size());
    for (size_t index = 0; index < result.lodMeshes.size() && index < result.gridInfos.size(); ++index)
    {
        const auto& info = result.gridInfos[index];
        USD_OPTIMIZE_LOG_VERBOSE("Shrinkwrap: %s: LOD %zu: voxelSize=%.4f, dim=%d, activeVoxels=%zu",
                                 label.c_str(),
                                 index,
                                 info.voxelSize,
                                 info.dim,
                                 info.activeVoxels);
    }
    return true;
}


void authorMesh(const UsdStageWeakPtr& stage,
                const SdfPath& targetPath,
                const ShrinkwrapMesh& outputMesh,
                const GfMatrix4d& worldToLocal,
                bool resetXformStack,
                const std::string& label)
{
    UsdGeomMesh targetMesh = UsdGeomMesh::Define(stage, targetPath);
    targetMesh.GetSubdivisionSchemeAttr().Set(UsdGeomTokens->none);
    if (resetXformStack)
    {
        UsdGeomXformable(targetMesh).SetResetXformStack(true);
    }

    VtVec3fArray points(outputMesh.points.size());
    for (size_t index = 0; index < outputMesh.points.size(); ++index)
    {
        const auto& point = outputMesh.points[index];
        points[index] = GfVec3f(worldToLocal.Transform(GfVec3d(point[0], point[1], point[2])));
    }

    VtIntArray faceVertexCounts(outputMesh.triangles.size() + outputMesh.quads.size());
    VtIntArray faceVertexIndices;
    faceVertexIndices.reserve(outputMesh.triangles.size() * 3 + outputMesh.quads.size() * 4);
    size_t face = 0;
    for (const auto& triangle : outputMesh.triangles)
    {
        faceVertexCounts[face++] = 3;
        for (const int index : triangle)
        {
            faceVertexIndices.push_back(index);
        }
    }
    for (const auto& quad : outputMesh.quads)
    {
        faceVertexCounts[face++] = 4;
        for (const int index : quad)
        {
            faceVertexIndices.push_back(index);
        }
    }

    targetMesh.GetPointsAttr().Set(points);
    targetMesh.GetFaceVertexCountsAttr().Set(faceVertexCounts);
    targetMesh.GetFaceVertexIndicesAttr().Set(faceVertexIndices);

    USD_OPTIMIZE_LOG_INFO("Shrinkwrap: %s -> '%s': %zu vertices, %zu triangles, %zu quads (%zu total faces)",
                          label.c_str(),
                          targetPath.GetAsString().c_str(),
                          outputMesh.points.size(),
                          outputMesh.triangles.size(),
                          outputMesh.quads.size(),
                          outputMesh.triangles.size() + outputMesh.quads.size());
}

} // namespace


OperationResult ShrinkwrapOperation::executeImpl()
{
    const bool temporalCombined = m_inputMode == InputMode::eTemporalCombined;
    if (temporalCombined && m_extractLodPyramid)
    {
        return { false, getCStr("extractLodPyramid is not supported in Temporal Combined input mode") };
    }

    SdfPath temporalOutputPath;
    if (temporalCombined)
    {
        if (m_outputPath.empty() || !SdfPath::IsValidPathString(m_outputPath))
        {
            return { false, getCStr("Temporal Combined input mode requires a valid outputPath") };
        }
        temporalOutputPath = SdfPath(m_outputPath);
        if (!temporalOutputPath.IsAbsolutePath() || !temporalOutputPath.IsPrimPath() ||
            temporalOutputPath == SdfPath::AbsoluteRootPath())
        {
            return { false, getCStr("outputPath must be an absolute USD prim path") };
        }
        if (getUsdStage()->GetPrimAtPath(temporalOutputPath))
        {
            return { false, getCStr("outputPath must point to a new prim path") };
        }
    }

    constexpr bool meshesOnly = true;
    constexpr bool reverse = false;
    std::vector<UsdPrim> primsToProcess = _resolveExpressionsToPrims(getUsdStage(), m_meshPrimPaths, meshesOnly, reverse);
    if (temporalCombined)
    {
        for (const UsdPrim& prim : primsToProcess)
        {
            if (prim.GetPath().HasPrefix(temporalOutputPath) || temporalOutputPath.HasPrefix(prim.GetPath()))
            {
                return { false, getCStr("outputPath must not be an ancestor or descendant of an input mesh") };
            }
        }
    }

    USD_OPTIMIZE_LOG_INFO("Shrinkwrap: found %zu mesh prim(s) to process", primsToProcess.size());
    if (primsToProcess.empty())
    {
        if (!temporalCombined)
        {
            return { true };
        }
        std::string message = "No input meshes resolved for temporal shrinkwrap";
        if (!m_meshPrimPaths.empty())
        {
            message += " from paths [" + TfStringJoin(m_meshPrimPaths, ", ") +
                       "]. \"paths\" only matches meshes directly, not their descendants; to select every mesh "
                       "under a prim, use a path expression like \"<prim path>//*\" instead of the prim path alone";
        }
        return { false, getCStr(message) };
    }

    if (!temporalCombined)
    {
        for (const UsdPrim& prim : primsToProcess)
        {
            if (prim.IsInstanceProxy())
            {
                USD_OPTIMIZE_LOG_VERBOSE("Skipped prim %s because it is an instance proxy",
                                         prim.GetPath().GetAsString().c_str());
                continue;
            }

            UsdGeomXformCache xformCache(UsdTimeCode::Default());
            ShrinkwrapMesh inputMesh;
            std::string error;
            const AppendResult appendResult =
                appendMeshSample(UsdGeomMesh(prim), UsdTimeCode::Default(), xformCache, inputMesh, error);
            if (appendResult == AppendResult::eFatal)
            {
                return { false, getCStr(error) };
            }
            if (appendResult != AppendResult::eAppended)
            {
                continue;
            }

            ShrinkwrapResult result;
            const std::string label = prim.GetPath().GetAsString();
            if (!runShrinkwrap(inputMesh,
                               m_dim,
                               m_voxelSize,
                               m_erode,
                               m_threshold,
                               m_adaptivity,
                               m_extractLodPyramid,
                               label,
                               result,
                               error))
            {
                USD_OPTIMIZE_LOG_WARN("Shrinkwrap: %s", error.c_str());
                continue;
            }

            GfMatrix4d parentWorldToLocal(1.0);
            const UsdPrim parent = prim.GetParent();
            if (parent && parent != getUsdStage()->GetPseudoRoot())
            {
                parentWorldToLocal = xformCache.GetLocalToWorldTransform(parent).GetInverse();
            }
            for (size_t lod = 0; lod < result.lodMeshes.size(); ++lod)
            {
                const std::string suffix = lod == 0 ? "_shrinkwrap" : "_shrinkwrap_lod" + std::to_string(lod);
                const SdfPath targetPath =
                    prim.GetPath().GetParentPath().AppendChild(TfToken(prim.GetName().GetString() + suffix));
                authorMesh(getUsdStage(), targetPath, result.lodMeshes[lod], parentWorldToLocal, false, label);
            }
        }
        return { true };
    }

    const double startTime = std::isnan(m_startTime) ? getUsdStage()->GetStartTimeCode() : m_startTime;
    const double endTime = std::isnan(m_endTime) ? getUsdStage()->GetEndTimeCode() : m_endTime;
    if (!std::isfinite(startTime) || !std::isfinite(endTime) || !std::isfinite(m_timeStep) || m_timeStep <= 0.0)
    {
        return { false, getCStr("Temporal sample range and timeStep must be finite, and timeStep must be positive") };
    }
    if (endTime < startTime)
    {
        return { false, getCStr("Temporal shrinkwrap endTime must be greater than or equal to startTime") };
    }

    const double span = endTime - startTime;
    const double wholeStepsDouble = std::floor(span / m_timeStep);
    if (wholeStepsDouble > static_cast<double>(INT_MAX))
    {
        return { false, getCStr("Temporal shrinkwrap range contains too many samples") };
    }

    std::vector<double> sampleTimes;
    const size_t wholeSteps = static_cast<size_t>(wholeStepsDouble);
    sampleTimes.reserve(wholeSteps + 2);
    const double tolerance = std::max({ 1.0, std::abs(startTime), std::abs(endTime) }) * 1e-9;
    for (size_t index = 0; index <= wholeSteps; ++index)
    {
        const double time = startTime + static_cast<double>(index) * m_timeStep;
        if (time <= endTime + tolerance)
        {
            sampleTimes.push_back(std::min(time, endTime));
        }
    }
    if (sampleTimes.empty() || std::abs(sampleTimes.back() - endTime) > tolerance)
    {
        sampleTimes.push_back(endTime);
    }

    USD_OPTIMIZE_LOG_INFO("Shrinkwrap: sampling %zu mesh prim(s) at %zu time(s) from %.6f to %.6f",
                          primsToProcess.size(),
                          sampleTimes.size(),
                          startTime,
                          endTime);

    ShrinkwrapMesh combinedMesh;
    UsdGeomXformCache xformCache;
    std::string error;
    size_t appendedSamples = 0;
    size_t topologyFallbackIntervals = 0;
    std::vector<MeshSample> previousSamples(primsToProcess.size());
    std::vector<bool> hasPreviousSample(primsToProcess.size(), false);
    for (const double time : sampleTimes)
    {
        const UsdTimeCode timeCode(time);
        for (size_t primIndex = 0; primIndex < primsToProcess.size(); ++primIndex)
        {
            const UsdPrim& prim = primsToProcess[primIndex];
            if (prim.IsInstanceProxy())
            {
                hasPreviousSample[primIndex] = false;
                continue;
            }

            MeshSample currentSample;
            const AppendResult appendResult =
                appendMeshSample(UsdGeomMesh(prim), timeCode, xformCache, combinedMesh, error, &currentSample);
            if (appendResult == AppendResult::eFatal)
            {
                return { false, getCStr(error) };
            }
            if (appendResult != AppendResult::eAppended)
            {
                hasPreviousSample[primIndex] = false;
                continue;
            }

            ++appendedSamples;
            if (m_connectTimeSamples && hasPreviousSample[primIndex])
            {
                if (appendSweptPrisms(previousSamples[primIndex],
                                      currentSample,
                                      combinedMesh,
                                      prim.GetPath().GetAsString()) == AppendResult::eTopologyChanged)
                {
                    ++topologyFallbackIntervals;
                }
            }
            previousSamples[primIndex] = std::move(currentSample);
            hasPreviousSample[primIndex] = true;
        }
    }
    if (appendedSamples == 0)
    {
        return { false, getCStr("No valid sampled polygons were collected for temporal shrinkwrap") };
    }
    if (topologyFallbackIntervals > 0)
    {
        USD_OPTIMIZE_LOG_INFO(
            "Shrinkwrap: %zu interval(s) fell back from swept prisms to sampled surfaces because topology "
            "changed between adjacent samples; the envelope may have gaps across those intervals unless "
            "surrounding samples still overlap. Reduce timeStep around them, or enable verbose logging for "
            "the specific prims involved.",
            topologyFallbackIntervals);
    }

    ShrinkwrapResult result;
    if (!runShrinkwrap(combinedMesh,
                       m_dim,
                       m_voxelSize,
                       m_erode,
                       m_threshold,
                       m_adaptivity,
                       false,
                       "temporal combined input",
                       result,
                       error))
    {
        return { false, getCStr(error) };
    }

    authorMesh(getUsdStage(), temporalOutputPath, result.lodMeshes.front(), GfMatrix4d(1.0), true, "temporal combined input");
    return { true };
}


} // namespace usd_optimize
