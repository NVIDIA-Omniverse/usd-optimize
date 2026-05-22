// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/Types.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"


namespace omni::scene::optimizer
{


/// Represents an USD mesh that may or may not exist in the Stage, with functionality for splitting, merging, and
/// writing the results to a layer.
///
/// This is used for representing the results of restructuring (e.g. split meshes, merge meshes) the meshes in a
/// stage/layer without having to commit the changes to the stage/layer until all restructuring is done.
///
/// The data of a VirtualMesh can constructed from one of four sources:
/// * Derived from a prim in the stage: That data is copied from the prim as is and is not modified.
/// * A subset of another VirtualMesh: The data is a slice of another VirtualMesh's geometry data and primvars
///   (i.e. splitting).
/// * A superset of one or more VirtualMeshes: The data is a combination of the geometry data and primvars of other
///   VirtualMeshes (i.e. merging).
/// * An empty invalid mesh with no data.
///
/// VirtualMeshes are designed for performance so its important to note that this class uses reference counting - The
/// copy constructor and assignment operator do not copy the data of the VirtualMesh, they only increment the reference
/// counter to the internal data. Copy-on-write is used where possible when making subsets/supersets to avoid doing any
/// unnecessary copying of data. The geometry of VirtualMeshes (in the case of subsets and supersets) is lazily computed
/// only when `computeGeometry` is called or a function that requires computed geometry is called.
class OMNI_SO_EXPORT VirtualMesh
{
public:
    /// Scoped structure that when initialised sets up global VirtualMesh optimization structures and then cleans them
    /// up when it goes out of scope.
    class OMNI_SO_EXPORT OptLifetime
    {
    public:
        /// Constructor
        OptLifetime();

        /// Destructor
        ~OptLifetime();
    };

    /// Scoped structure used to control global configuration options for VirtualMeshes and reverts the options when it
    /// goes out of scope.
    class OMNI_SO_EXPORT ConfigLifetime
    {
    public:
        /// Constructor
        ConfigLifetime();

        /// Destructor
        ~ConfigLifetime();

        /// Sets the names of attributes to treat as primvars when merging meshes
        void setMergeAttrsAsPrimvars(const std::vector<std::string>& attrs);
    };

    /// Creates a new invalid VirtualMesh that is not derived from a prim and has not geometry or other data.
    VirtualMesh();

    /// Superset constructor.
    ///
    /// Creates a new superset VirtualMesh that is not derived from a prim and does not yet have any VirtualMesh
    /// children (added using the `addSupersetChild` function).
    ///
    /// \param destinationParentPath The path of the parent prim in the stage which this VirtualMesh will be created
    ///                              under when `createInLayer` is called.
    /// \param destinationName The name of the prim that this VirtualMesh will create when `createInLayer` is called.
    /// \param appliedSchemas List of the Api schemas that will be applied to the new resulting prim.
    /// \param spatialClusterId Used by spatial clustering code to track identify which cluster this VirtualMesh belongs
    ///                         to during clustering algorithms.
    VirtualMesh(const PXR_NS::SdfPath& destinationParentPath,
                const std::string& destinationName,
                const PXR_NS::TfTokenVector& appliedSchemas,
                const PXR_NS::UsdStageWeakPtr& stage,
                PXR_NS::UsdGeomXformCache& xformCache,
                int spatialClusterId = 0);

    /// Derived constructor.
    ///
    /// Creates a new VirtualMesh that is derived from the given prim.
    ///
    /// \param prim The Usd prim that this VirtualMesh will be derived from.
    /// \param xformCache The xform cache to use when computing the local-to-world transform of the prim.
    /// \param bindingsCache The bindings cache to use when computing the bound material of the prim
    /// \param collQueryCache The collection query cache to use when computing the bound material of the prim
    VirtualMesh(const PXR_NS::UsdPrim& prim,
                PXR_NS::UsdGeomXformCache& xformCache,
                PXR_NS::UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                PXR_NS::UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache);

    /// Shallow copy constructor
    ///
    /// Takes a reference count of the internal data of the other VirtualMesh.
    VirtualMesh(const VirtualMesh& other);

    /// Copy assignment operator
    ///
    /// Decreases the reference count of the current internal data of this VirtualMesh and takes a reference count of
    /// the internal data of the given VirtualMesh.
    VirtualMesh& operator=(const VirtualMesh& other);

    /// Destructor
    ~VirtualMesh();

    /// Return the unique identifier of this VirtualMesh - each VirtualMesh is assigned a unique identifier at
    /// construction.
    size_t getId() const;

    /// Returns the prim path that this VirtualMesh is currently or originally derived from.
    const PXR_NS::SdfPath& getSourcePath() const;

    /// Returns the prim path that this VirtualMesh will be created under when `createInLayer` is called.
    PXR_NS::SdfPath getDestinationPath() const;

    /// Returns the parent path this VirtualMesh will be created under when `createInLayer` is called.
    const PXR_NS::SdfPath& getDestinationParentPath() const;

    /// Returns the name of the prim that this VirtualMesh will create when `createInLayer` is called.
    const std::string& getDestinationName() const;

    /// Sets the parent path this VirtualMesh will be created under when `createInLayer` is called.
    void setDestinationPath(const PXR_NS::SdfPath& destinationPath);

    /// Sets the name of the prim that this VirtualMesh will create when `createInLayer` is called.
    void setDestinationName(const std::string& name);

    /// If this VirtualMesh represents a valid prim.
    bool isValid() const;

    /// If this VirtualMesh represents an active prim.
    bool isActive() const;

    /// If this VirtualMesh is directly derived from a prim in the stage.
    bool isDerivedFromPrim() const;

    /// If this VirtualMesh is a subset of another VirtualMesh.
    bool isSubset() const;

    /// If this VirtualMesh is a superset of one or more VirtualMeshes.
    bool isSuperset() const;

    /// Returns the prim that this VirtualMesh is currently or originally derived from.
    const PXR_NS::UsdPrim& getPrim() const;

    /// Returns the Sdf specifier of the prim this represents.
    PXR_NS::SdfSpecifier getSpecifier() const;

    /// Returns the TypeName of the prim this represents.
    PXR_NS::TfToken getTypeName() const;

    /// Returns the names of the attributes of this VirtualMesh.
    std::vector<PXR_NS::TfToken> getAttributeNames() const;

    /// Extends the given set with the names of the attributes of this VirtualMesh
    void extendAttributeNameSet(PXR_NS::TfToken::HashSet& nameSet) const;

    /// Returns the VtValue for the attribute or primvar if it exists as either an override or as a base value
    ///
    /// \param name The name of the attribute/primvar to get the value for
    /// \param value Output value to populate
    /// \return Whether this VirtualMesh has a value for the given name
    bool getValue(const PXR_NS::TfToken& name, PXR_NS::VtValue& value) const;

    /// Sets an attribute override on this VirtualMesh
    void setAttributeOverride(const PXR_NS::TfToken& name,
                              const PXR_NS::SdfValueTypeName& typeName,
                              const PXR_NS::SdfVariability& variability,
                              bool custom,
                              PXR_NS::VtValue&& defaultValue);

    /// Returns the applied schemas of the prim this represents.
    ///
    /// \param appliedSchemas Used to return the applied schemas.
    /// \param includeMaterialBinding If true, the material binding schema will be included in the list of applied
    ///                               schemas.
    void getAppliedSchemas(PXR_NS::TfTokenVector& appliedSchemas, bool includeMaterialBinding = true) const;

    /// Returns whether this VirtualMesh has a SkelBindingAPI applied schema
    bool isSkeleton() const;

    /// Computes the geometry data of this VirtualMesh, if it has not already been computed, otherwise does nothing.
    ///
    /// You should avoid calling this function until the geometry of the VirtualMesh is required as it can be expensive
    /// to compute. Once called the VirtualMesh will hold real in-memory data that represents the geometry that the
    /// VirtualMesh represents.
    ///
    /// \note This function is SdfChangeBlock safe.
    bool computeGeometry();

    /// Computes the geometry data, and extent of this VirtualMesh, if it has not already been computed, and returns
    /// whether this VirtualMesh is valid geometry.
    bool validateAndComputeExtent();

    /// Returns the face vertex counts geometry data of this VirtualMesh.
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtIntArray& getFaceVertexCounts() const;

    /// Returns the face vertex indices geometry data of this VirtualMesh.
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtIntArray& getFaceVertexIndices() const;

    /// Returns the points geometry data of this VirtualMesh.
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtVec3fArray& getPoints() const;

    /// Returns the indices of faces that should be treated as holes
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtIntArray& getHoleIndices();

    /// Returns the indices of the vertices that should be treated as corners for subdivision surfaces
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtIntArray& getCornerIndices();

    /// Returns the sharpnesses of subdivision surfaces corners defined by corner indices
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtFloatArray& getCornerSharpnesses();

    /// Returns the pairs indices of the vertices that should be treated as creases for subdivision surfaces
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtIntArray& getCreaseIndices();

    /// Returns the length in indices of each creases defined by crease indices
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtIntArray& getCreaseLengths();

    /// Returns the sharpnesses of each subdivision surface crease
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtFloatArray& getCreaseSharpnesses();

    /// Returns the skeleton joint names array
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtTokenArray& getJointNames();

    /// Returns the skeleton joint indices array
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtIntArray& getJointIndices();

    /// Returns the skeleton joint weights array
    ///
    /// \warning If geometry has not yet been computed, this function will not return valid data.
    const PXR_NS::VtFloatArray& getJointWeights();

    /// Returns a hash of this VirtualMesh mesh that represents its uniqueness for RTX deduplication purposes.
    size_t getRtxDuplicateHash() const;

    /// Returns the matrix that transforms the points of this VirtualMesh from local space to world space.
    const PXR_NS::GfMatrix4d& getLocalToWorldTransform() const;

    /// Returns the extent of this VirtualMesh in local space.
    const PXR_NS::VtVec3fArray& getLocalExtent() const;

    /// Returns the extent of this VirtualMesh in world space.
    const PXR_NS::VtVec3fArray& getWorldExtent() const;

    /// Computes the max dimension size of the extent of this VirtualMesh in world space.
    ///
    /// \note validateAndComputeExtent must be called before this function to ensure the extent is valid.
    float getExtentMaxSize() const;

    /// Computes the volume of the extent of this VirtualMesh in world space.
    ///
    /// \note validateAndComputeExtent must be called before this function to ensure the extent is valid.
    float getExtentVolume() const;

    /// Computes the volume of the geometry of this VirtualMesh in world space.
    ///
    /// \note computeGeometry must be called before this function to ensure the geometry is valid.
    float getGeometryVolume() const;

    /// Returns if this VirtualMesh has an explicitly bound or explicitly unbound material
    bool hasExplicitMaterialBinding() const;

    /// Returns the bound material path of this VirtualMesh (if set)
    const PXR_NS::SdfPath& getBoundMaterialPath() const;

    /// Binds a material to this VirtualMesh by updating the applied schemas.
    ///
    /// \param materialPath The path in the stage of the material to bind to.
    /// \param metadata The metadata to apply to the material binding.
    void bindMaterial(const PXR_NS::SdfPath& materialPath,
                      const PXR_NS::UsdMetadataValueMap& metadata = PXR_NS::UsdMetadataValueMap());

    /// Explicitly removes any material binding this VirtualMesh has
    void unbindMaterial();

    /// Returns the material subset information of this VirtualMesh (material paths mapped to face indices)
    const std::map<PXR_NS::SdfPath, PXR_NS::VtIntArray>& getMaterialSubsets() const;

    /// Replaces the bound material of this VirtualMesh with constant display color primvar using the given color
    void replaceMaterialWithDisplayColor(const ColorValue& baseColor);

    /// Updates this VirtualMesh to use have a spatial debug color set (and no material)
    void useSpatialDebug();

    /// Creates and returns a new VirtualMesh that is a deep copy of this VirtualMesh with a different xform and
    /// different points
    ///
    /// \param xform Matrix that will be used as the transform for this new mesh
    /// \param points Point data that will be used as the vertices of this new mesh, note it is expected that the number
    ///               of new points is the same as the number of points from the original VirtualMesh
    VirtualMesh newModifiedCopy(const PXR_NS::GfMatrix4d& xform, PXR_NS::VtVec3fArray&& points) const;

    /// Creates and returns a new VirtualMesh that is a subset of this VirtualMesh.
    ///
    /// \note The new subset will not have geometry computed yet.
    ///
    /// \param faceVertexCountIndices The indices of the face vertex counts of this mesh to include in the subset.
    VirtualMesh newSubset(const std::vector<int>&& faceVertexCountIndices);

    /// Returns the indices into the original mesh's face vertex counts that this subset VirtualMesh represents.
    ///
    /// The function is useful for creating a GeomSubset for this VirtualMesh rather than authoring a new prim for it.
    ///
    /// \note If this VirtualMesh is not a subset this will return an empty vector.
    const std::vector<int>& getSubsetFaceVertexCountIndices() const;

    /// Makes this VirtualMesh a superset (if its not already) by adding a child VirtualMesh to it that will be used
    /// to make one part of the Geometry of the resulting mesh.
    ///
    /// This function requires that the geometry of this VirtualMesh has not yet been computed and that this is not
    /// already a subset.
    ///
    /// \param child The VirtualMesh that will be added as part of the superset.
    void addSupersetChild(VirtualMesh& child);

    /// Returns the children VirtualMeshes that are part of the superset of this VirtualMesh.
    const std::vector<VirtualMesh>& getSupersetChildren() const;

    /// Returns the current data volume (i.e. the number of points) that of the superset that this VirtualMesh
    /// represents.
    ///
    /// \note This function will return 0 if this is not a superset.
    size_t getSupersetDataVolume() const;

    /// Returns the spatial cluster id that has been assigned to this VirtualMesh. Used by clustering algorithms to
    /// identify which cluster this VirtualMesh belongs to.
    int getSpatialClusterId() const;

    /// Sets the spatial cluster id that has been assigned to this VirtualMesh.
    void setSpatialClusterId(int id);

    /// Creates this VirtualMesh as a geometry prim in the given layer of the stage.
    ///
    /// \note This function is SdfChangeBlock safe.
    void createInLayer(const PXR_NS::UsdStageWeakPtr& stage, PXR_NS::SdfLayerHandle& layer);

private:
    class Impl;

    Impl* pImpl;

    VirtualMesh(Impl* impl);
};


} // namespace omni::scene::optimizer
