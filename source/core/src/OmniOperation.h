// SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Operation.h"
#include "omni/scene.optimizer/core/RemovePrims.h"
#include "omni/scene.optimizer/core/ResolveSdfPaths.h"

// OmniMeshOps
#include <OmniMeshOps/usd/Mesh.h>
#include <OmniMeshOps/usd/MeshData.h>
#include <OmniMeshOps/usd/Prim.h>

// TBB
#include <tbb/task_group.h>

// C++
#include <queue>


namespace omni::scene::optimizer
{

/// Struct to track mesh counts
struct MeshStats
{
    size_t vertexCount = 0;
    size_t faceCount = 0;
};

/// Before/After stats
struct TotalStats
{
    MeshStats before;
    MeshStats after;
};

/// Struct to contain OMO result data
struct ProcessedData
{
    ProcessedData(const PXR_NS::UsdPrim& usdPrim, bool write)
        : m_usdPrim(usdPrim)
        , m_write(write)
    {
    }

    virtual ~ProcessedData()
    {
    }

    PXR_NS::UsdPrim usdPrim() const
    {
        return m_usdPrim;
    }

    bool shouldWrite() const
    {
        return m_write;
    }

    /// This will be called by the pipeline to gather stats
    virtual void updateStats(TotalStats& stats) const
    {
        PXR_NS::VtArray<PXR_NS::GfVec3f> points;
        PXR_NS::VtIntArray face_vertex_counts;

        PXR_NS::UsdGeomMesh usdMesh(usdPrim());

        if (usdMesh)
        {
            if (usdMesh.GetPointsAttr().Get(&points))
            {
                stats.before.vertexCount += points.size();
            }
            if (usdMesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts))
            {
                stats.before.faceCount += face_vertex_counts.size();
            }
        }

        if (shouldWrite())
        {
            stats.after.vertexCount += vertexCount();
            stats.after.faceCount += faceCount();
        }
        else
        {
            // if not writing, the after results will be equal to before
            stats.after.vertexCount += points.size();
            stats.after.faceCount += face_vertex_counts.size();
        }
    }

    virtual bool shouldDelete() const
    {
        return false;
    }

    /// \brief The number of corners in the resulting mesh, if implemented by a mesh-specific derived class
    virtual size_t cornerCount() const
    {
        return 0;
    }

    /// \brief The number of faces in the resulting mesh, if implemented by a mesh-specific derived class
    virtual size_t faceCount() const
    {
        return 0;
    }

    /// \brief The number of vertices in the resulting mesh, if implemented by a mesh-specific derived class
    virtual size_t vertexCount() const
    {
        return 0;
    }

    /// \brief If implemented by a derived class, can write its data to the provided prim path and UsdStage.
    /// \param primPath The prim path
    /// \param stage The output stage
    virtual void writeToUsd([[maybe_unused]] const std::string& primPath,
                            [[maybe_unused]] const PXR_NS::UsdStageWeakPtr& stage)
    {
    }

protected:
    PXR_NS::UsdPrim m_usdPrim;
    bool m_write = true; // write to USD
};

struct ProcessedHostMesh : public ProcessedData
{
    ProcessedHostMesh(const omo::usd::HostMesh& hostMesh, const PXR_NS::UsdPrim& usdPrim, bool write = true)
        : ProcessedData(usdPrim, write)
        , m_hostMesh(hostMesh)
    {
    }

    size_t cornerCount() const override
    {
        return m_hostMesh.cornerCount();
    }

    size_t faceCount() const override
    {
        return m_hostMesh.faceCount();
    }

    size_t vertexCount() const override
    {
        return m_hostMesh.vertexCount();
    }

    void writeToUsd(const std::string& primPath, const PXR_NS::UsdStageWeakPtr& stage) override
    {
        m_hostMesh.writeToUsd(primPath, stage);
    }

protected:
    omo::usd::HostMesh m_hostMesh;
};

struct ProcessedHostMeshData : public ProcessedData
{
    ProcessedHostMeshData(const omo::usd::HostMeshData& meshData, const PXR_NS::UsdPrim& usdPrim, bool write = true)
        : ProcessedData(usdPrim, write)
        , m_meshData(meshData)
    {
    }

    bool shouldDelete() const override
    {
        return m_meshData.vertexCount() == 0;
    }

    size_t cornerCount() const override
    {
        return m_meshData.cornerCount();
    }

    size_t faceCount() const override
    {
        return m_meshData.faceCount();
    }

    size_t vertexCount() const override
    {
        return m_meshData.vertexCount();
    }

    void writeToUsd(const std::string& primPath, const PXR_NS::UsdStageWeakPtr& stage) override
    {
        m_meshData.writeToUsd(primPath, stage);
    }

protected:
    omo::usd::HostMeshData m_meshData;
};

struct ProcessedHostPrim : public ProcessedData
{
    ProcessedHostPrim(const omo::usd::HostPrim& hostPrim, const PXR_NS::UsdPrim& usdPrim, bool write = true)
        : ProcessedData(usdPrim, write)
        , m_hostPrim(hostPrim)
    {
    }

    void updateStats(TotalStats& stats) const override
    {
        PXR_NS::VtArray<PXR_NS::GfVec3f> points;
        PXR_NS::VtIntArray face_vertex_counts;

        PXR_NS::UsdGeomMesh usdMesh(usdPrim());

        if (usdMesh)
        {
            if (usdMesh.GetPointsAttr().Get(&points))
            {
                stats.before.vertexCount += points.size();
            }
            if (usdMesh.GetFaceVertexCountsAttr().Get(&face_vertex_counts))
            {
                stats.before.faceCount += face_vertex_counts.size();
            }
        }

        // if not writing or a prim wasn't fit, the after results will be equal to before
        if (!shouldWrite() || m_hostPrim.type == omo::PrimitiveType::None)
        {
            stats.after.vertexCount += points.size();
            stats.after.faceCount += face_vertex_counts.size();
        }
    }

    const omo::usd::HostPrim& hostPrim() const
    {
        return m_hostPrim;
    }

    void writeToUsd(const std::string& primPath, const PXR_NS::UsdStageWeakPtr& stage) override
    {
        m_hostPrim.writeToUsd(primPath, stage);
    }

protected:
    omo::usd::HostPrim m_hostPrim;
};

/// OmniMesh Operation
///
/// Derived operation that creates a TBB pipeline to read USD data, perform an omni mesh operation on it,
/// and then author it back to a stage.
///
/// Wraps up the TBB pipeline handling in to a single place so the omni plugins can be simpler, only really
/// needing to implement their primary logic.
class OMNI_SO_EXPORT OmniOperation : public Operation
{
public:
    /// Standard Constructor
    ///
    /// \param name The internal name of the operation
    /// \param displayName The display name of the operation
    /// \param description A description of what the operation does
    OmniOperation(const std::string& name, const std::string& displayName, const std::string& description);

    ~OmniOperation() override;

protected:
    /// Used for the call to _resolveExpressionsToPrims (to get the prims to operate on)
    virtual bool meshesOnly();

    /// The predicate used to resolve prim expressions in executeImpl()
    virtual PXR_NS::Usd_PrimFlagsPredicate primFlagsPredicate() const;

    /// The resolve filter to use for prims to process
    virtual ResolveFilter resolveFilter();

    /// Called prior to the TBB pipeline, to allow a class to do any last minute setup.
    virtual OperationResult executePre();

    /// Called after executePre, to allow a class to filter and/or mutate the input prims
    virtual void preProcessPrims(std::vector<PXR_NS::UsdPrim>&);

    /// Called after the pipeline is finished, so cleanup can be done or stats can be
    /// printed. The default implementation just prints out the values in totalStats.
    virtual void executePost(const TotalStats& totalStats);

    /// Standard SO operation entry point
    OperationResult executeImpl() override;

    /// Derived Omni operations must implement this function.
    ///
    /// This function can be called on multiple threads.
    ///
    /// \param prim The USD prim to process
    /// \param taskGroupContext TBB task group for execution control
    virtual ProcessedData* processMesh(const PXR_NS::UsdPrim& prim, tbb::task_group_context& taskGroupContext) = 0;

    std::vector<std::string> m_meshPrimPaths;
};


} // namespace omni::scene::optimizer
