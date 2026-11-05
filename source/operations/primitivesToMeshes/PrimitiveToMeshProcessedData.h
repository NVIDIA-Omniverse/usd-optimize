// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>

// OmniMesh
#include <OmniMeshOps/MeshAttributeXfer.h>
#include <OmniMeshOps/Primitive.h>
#include <OmniMeshOps/UsdIO.h>

// Usd Optimize
#include <usd_optimize/core/ComputeExtents.h>
#include <usd_optimize/core/Defs.h>
#include <usd_optimize/core/OmniOperation.h>
#include <usd_optimize/core/Utils.h>

namespace usd_optimize
{

/// OmniMesh counts faces and corners in 32 bits, which huge division counts overflow.
constexpr int s_maxPrimitiveDivisions = 1000;

/// Override ProcessedData's writeToUsd function to create prototypes
struct PrimitiveToMeshProcessedData : public ProcessedData
{
    PrimitiveToMeshProcessedData(const PXR_NS::UsdPrim& usd_prim,
                                 const omo::Primitive& primitive,
                                 omo::PrimitiveUpAxis up_axis,
                                 const omo::PrimitiveMeshParameters& params,
                                 HashCache* hash_cache,
                                 bool write = true,
                                 const omo::HostMesh& src_mesh = omo::HostMesh(),
                                 double capsule_height_radius_ratio = 2.0)
        : ProcessedData(usd_prim, write)
        , m_primitive(primitive)
        , m_up_axis(up_axis)
        , m_params(params)
        , m_hash_cache(hash_cache)
        , m_src_mesh(src_mesh)
        , m_capsule_height_radius_ratio(capsule_height_radius_ratio)
    {
    }

    void writeToUsd(const std::string& prim_path, const PXR_NS::UsdStageWeakPtr& stage) override
    {
        PXR_NS::SdfPath path(prim_path);
        PXR_NS::UsdPrim prim = stage->GetPrimAtPath(path);

        // Get transform and remove transform ops from current gprim
        PXR_NS::UsdGeomGprim usd_gprim(prim);
        PXR_NS::GfMatrix4d mesh_local_tm(1.0);
        bool resetXformStack = false;
        usd_gprim.GetLocalTransformation(&mesh_local_tm, &resetXformStack);
        for (auto op : usd_gprim.GetOrderedXformOps(&resetXformStack))
        {
            if (op.GetOpType() != PXR_NS::UsdGeomXformOp::TypeTransform)
            {
                op.GetAttr().Block();
            }
        }
        usd_gprim.ClearXformOpOrder();

        // Remove any Gprim shape-specific attributes, in case the source prim was a gprim shape
        omo::removeShapeSpecificAttributes(usd_gprim);

        // Change type to Mesh if it was not before
        PXR_NS::UsdGeomMesh usd_mesh = PXR_NS::UsdGeomMesh::Define(stage, path);

        // Create abstract prototype prim root, if it doesn't exist already
        std::string prototype_base_name(omo::PrimitiveType::name(m_primitive.type));
        prototype_base_name += "_";
        prototype_base_name += "XYZ"[int(m_up_axis)];
        switch (m_primitive.type)
        {
        case omo::PrimitiveType::Capsule:
        {
            // Capsules share a radius-one prototype for each height/radius ratio.
            // Convert to float and encode all eight exponent bits and the high 20 fraction bits.
            // Use the same quantized ratio for geometry so reuse does not depend on conversion order.
            float ratio = static_cast<float>(m_capsule_height_radius_ratio);
            uint32_t ratio_bits;
            static_assert(sizeof(ratio_bits) == sizeof(ratio));
            std::memcpy(&ratio_bits, &ratio, sizeof(ratio_bits));
            constexpr int discarded_bits = 23 - 20;
            const uint32_t ratio_key = (ratio_bits & 0x7FFFFFFFU) >> discarded_bits;
            std::ostringstream ratio_code;
            ratio_code << std::uppercase << std::hex << std::setfill('0') << std::setw(7) << ratio_key;
            prototype_base_name += "_" + std::to_string(m_params.n_radial) + "_" + std::to_string(m_params.n_axial) +
                                   "_" + std::to_string(m_params.n_cap) + "_hr" + ratio_code.str();
            ratio_bits = ratio_key << discarded_bits;
            std::memcpy(&ratio, &ratio_bits, sizeof(ratio_bits));
            m_capsule_height_radius_ratio = ratio;
            break;
        }
        case omo::PrimitiveType::Sphere:
        case omo::PrimitiveType::Cylinder:
        case omo::PrimitiveType::Cone:
            prototype_base_name += "_" + std::to_string(m_params.n_radial) + "_" + std::to_string(m_params.n_axial);
            if (m_primitive.type != omo::PrimitiveType::Sphere && !m_params.capped)
            {
                prototype_base_name += "_open";
            }
            break;
        case omo::PrimitiveType::Cube:
            break;
        default:
            throw std::runtime_error("Not a valid prim type");
        }

        PXR_NS::UsdPrim prototype_prim;
        PXR_NS::SdfPath prototype_path;
        uint32_t N = 0;
        for (; N != ~(uint32_t)0; ++N)
        {
            // Search through names that start with the based name and then start adding consecutive positive integers
            std::string prototype_name = prototype_base_name;
            if (N)
            {
                prototype_name += "_" + std::to_string(N);
            }
            prototype_path = s_prototypeRootPath.AppendChild(PXR_NS::TfToken(prototype_name));

            // See if there's a primitive at this path
            prototype_prim = stage->GetPrimAtPath(prototype_path);
            if (!prototype_prim)
            {
                break; // No prim, break and we'll build it
            }

            // If there is a prim check its hash against its stored hash
            if (prototype_prim.HasCustomDataKey(s_primHashToken))
            {
                PXR_NS::VtValue hash_val = prototype_prim.GetCustomDataByKey(s_primHashToken);
                if (hash_val.Get<size_t>() == _hashPrim(stage, prototype_prim, m_hash_cache, nullptr))
                {
                    break; // This prim's hash matches its hash when it was created.  Use it.
                }
            }
        }

        // This should never happen
        if (N == ~(uint32_t)0)
        {
            throw std::runtime_error("Ran out of integers for disambiguation");
        }

        // Create a prototype prim with disambiguation
        if (!prototype_prim)
        {
            static constexpr std::array<double, 16> up_rot[3] = { { 0, 0, 1, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1 },
                                                                  { 0, 1, 0, 0, 0, 0, 1, 0, 1, 0, 0, 0, 0, 0, 0, 1 },
                                                                  { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 } };

            omo::HostMesh mesh = omo::createPrimitiveMesh<omo::Host>(m_primitive.type, m_params, up_rot[int(m_up_axis)]);

            // Make sure the root path is valid
            if (!stage->GetPrimAtPath(s_prototypeRootPath))
            {
                PXR_NS::UsdPrim prototype_root = stage->DefinePrim(s_prototypeRootPath);
                if (!prototype_root)
                {
                    throw std::runtime_error("Failed to create prototype root");
                }
            }

            // Write the prototype to the stage
            exportUsdGeomMesh(mesh, stage, prototype_path.GetString());

            prototype_prim = stage->GetPrimAtPath(prototype_path);
            if (!prototype_prim)
            {
                throw std::runtime_error("Failed to create prototype mesh");
            }

            if (m_primitive.type == omo::PrimitiveType::Capsule)
            {
                // OmniMesh's unit capsule has radius 1, body endpoints at +/-1, and poles at +/-2.
                // Stretch only the body to match height/radius, leaving the caps at radius one.
                PXR_NS::UsdGeomMesh prototype_mesh(prototype_prim);
                PXR_NS::VtVec3fArray points;
                prototype_mesh.GetPointsAttr().Get(&points);
                const int axis = int(m_up_axis);
                for (auto& point : points)
                {
                    const double axial = point[axis];
                    const double body = std::clamp(axial, -1.0, 1.0);
                    point[axis] = float(axial - body + body * 0.5 * m_capsule_height_radius_ratio);
                }
                prototype_mesh.GetPointsAttr().Set(points);
                _computeExtents({ prototype_prim });
            }

            // Set its hash metadata
            const size_t hash = _hashPrim(stage, prototype_prim, m_hash_cache, nullptr);
            prototype_prim.SetCustomDataByKey(s_primHashToken, PXR_NS::VtValue(hash));
        }

        // Add reference to prototype and set the type from it (clearing ours would keep a sublayer's primitive type)
        PXR_NS::UsdReferences refs = usd_mesh.GetPrim().GetReferences();
        refs.AddReference(PXR_NS::SdfReference("", prototype_prim.GetPath()));
        usd_mesh.GetPrim().SetTypeName(prototype_prim.GetTypeName());

        // If source is a mesh, remove faceVertexCounts, faceVertexIndices, normals, and points from referencing mesh
        // and transfer attributes
        if (m_src_mesh)
        {
            // Transfer attributes based on prototype geometry
            omo::HostMeshAttributeXfer attrXfer(m_src_mesh);
            auto prototype_mesh = omo::importMesh(PXR_NS::UsdGeomMesh(prototype_prim));
            prototype_mesh = attrXfer(static_cast<omo::HostMesh&>(prototype_mesh), m_primitive.transform);
            omo::exportUsdGeomMesh(prototype_mesh, stage, prim_path); // Write at instance path

            // Remove geometry
            usd_mesh.GetExtentAttr().Clear();
            usd_mesh.GetFaceVertexCountsAttr().Clear();
            usd_mesh.GetFaceVertexIndicesAttr().Clear();
            usd_mesh.GetPointsAttr().Clear();
            if (usd_mesh.GetNormalsAttr().IsAuthored())
            {
                usd_mesh.GetNormalsAttr().Clear();
            }
        }

        // Write transform as full 4x4 matrix, appending frame adjustment transform
        auto xformOp = usd_mesh.AddTransformOp();
        usd_mesh.SetResetXformStack(resetXformStack);

        // Append scaling (used if source was a Gprim shape)
        xformOp.Set(PXR_NS::VtValue(omo::gfMatrixTransform(m_primitive) * mesh_local_tm));

        // Author the extent from the prototype's points. It is in the prim's local space, so it matches the prototype's
        // and the transform above carries the scale. This also replaces any extent the source gprim had, which could
        // otherwise come from a weaker layer and win over the reference. Blocking it instead left the prim with no
        // extent at all, because a block hides the prototype's extent too.
        _computeExtents({ usd_mesh.GetPrim() });

        // Make sure the root is an over
        stage->GetPrimAtPath(s_prototypeRootPath).SetSpecifier(PXR_NS::SdfSpecifierOver);
    }

private:
    // Payload set in constructor
    omo::Primitive m_primitive;
    omo::PrimitiveUpAxis m_up_axis;
    omo::PrimitiveMeshParameters m_params;
    HashCache* m_hash_cache;
    omo::HostMesh m_src_mesh;
    double m_capsule_height_radius_ratio;

    // The root path for all prototypes
    const static inline PXR_NS::SdfPath s_prototypeRootPath = PXR_NS::SdfPath("/NV_SO_Mesh_Primitive_Prototypes");

    // Custom data key to store hash
    const static inline PXR_NS::TfToken s_primHashToken = PXR_NS::TfToken("USD_OPTIMIZE_Prim_Hash");
};

} // namespace usd_optimize
