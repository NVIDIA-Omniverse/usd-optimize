// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// OmniMesh
#include <OmniMeshOps/MeshAttributeXfer.h>
#include <OmniMeshOps/Primitive.h>
#include <OmniMeshOps/usd/Mesh.h>

// Scene Optimizer
#include <omni/scene.optimizer/core/Defs.h>
#include <omni/scene.optimizer/core/OmniOperation.h>
#include <omni/scene.optimizer/core/Utils.h>

namespace omni::scene::optimizer
{

/// Override ProcessedData's writeToUsd function to create prototypes
struct PrimitiveToMeshProcessedData : public ProcessedData
{
    PrimitiveToMeshProcessedData(const PXR_NS::UsdPrim& usd_prim,
                                 const omo::Primitive& primitive,
                                 omo::PrimitiveUpAxis up_axis,
                                 const omo::PrimitiveMeshParameters& params,
                                 HashCache* hash_cache,
                                 bool write = true,
                                 const omo::HostMesh& src_mesh = omo::HostMesh())
        : ProcessedData(usd_prim, write)
        , m_primitive(primitive)
        , m_up_axis(up_axis)
        , m_params(params)
        , m_hash_cache(hash_cache)
        , m_src_mesh(src_mesh)
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

        // Also remove the local space extent; it should be obtained from the referenced prototype
        auto bounds = usd_gprim.GetExtentAttr();
        if (bounds)
        {
            bounds.Block();
        }

        // Remove any Gprim shape-specific attributes, in case the source prim was a gprim shape
        omo::usd::HostPrim::removeShapeSpecificAttributesFromGprim(prim_path, stage);

        // Change type to Mesh if it was not before
        PXR_NS::UsdGeomMesh usd_mesh = PXR_NS::UsdGeomMesh::Define(stage, path);

        // Create abstract prototype prim root, if it doesn't exist already
        std::string prototype_base_name(omo::PrimitiveType::name(m_primitive.type));
        prototype_base_name += "_";
        prototype_base_name += "XYZ"[int(m_up_axis)];
        switch (m_primitive.type)
        {
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

            omo::usd::HostMesh mesh =
                omo::createPrimitiveMesh<omo::Host>(m_primitive.type, m_params, up_rot[int(m_up_axis)]);

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
            mesh.writeToUsd(prototype_path.GetString(), stage);

            prototype_prim = stage->GetPrimAtPath(prototype_path);
            if (!prototype_prim)
            {
                throw std::runtime_error("Failed to create prototype mesh");
            }

            // Set its hash metadata
            const size_t hash = _hashPrim(stage, prototype_prim, m_hash_cache, nullptr);
            prototype_prim.SetCustomDataByKey(s_primHashToken, PXR_NS::VtValue(hash));
        }

        // Add reference to prototype and clear type name (type name will come from reference)
        PXR_NS::UsdReferences refs = usd_mesh.GetPrim().GetReferences();
        refs.AddReference(PXR_NS::SdfReference("", prototype_prim.GetPath()));
        usd_mesh.GetPrim().ClearTypeName();

        // If source is a mesh, remove faceVertexCounts, faceVertexIndices, normals, and points from referencing mesh
        // and transfer attributes
        if (m_src_mesh)
        {
            // Transfer attributes based on prototype geometry
            omo::HostMeshAttributeXfer attrXfer(m_src_mesh);
            omo::usd::HostMesh prototype_mesh = omo::usd::HostMesh(PXR_NS::UsdGeomMesh(prototype_prim));
            prototype_mesh = attrXfer(static_cast<omo::HostMesh&>(prototype_mesh), m_primitive.transform);
            prototype_mesh.writeToUsd(prim_path, stage); // Write at instance path

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
        xformOp.Set(PXR_NS::VtValue(static_cast<omo::usd::HostPrim>(m_primitive).gfMatrixTransform() * mesh_local_tm));

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

    // The root path for all prototypes
    const static inline PXR_NS::SdfPath s_prototypeRootPath = PXR_NS::SdfPath("/NV_SO_Mesh_Primitive_Prototypes");

    // Custom data key to store hash
    const static inline PXR_NS::TfToken s_primHashToken = PXR_NS::TfToken("SO_Prim_Hash");
};

} // namespace omni::scene::optimizer
