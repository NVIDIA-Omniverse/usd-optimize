// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/geometry/VirtualMesh.h"

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Core.h"
#include "omni/scene.optimizer/core/Utils.h"

// USD
#include <pxr/usd/usdGeom/primvarsAPI.h>
#include <pxr/usd/usdSkel/bindingAPI.h>
#include <pxr/usd/usdSkel/utils.h>

PXR_NAMESPACE_USING_DIRECTIVE


// clang-format off
// LCOV_EXCL_START
// Internal tokens
TF_DEFINE_PRIVATE_TOKENS(
    _tokens,
    ((apiSchemas, "apiSchemas"))
    (debugId)
    ((xform, "Xform"))
    ((mesh, "Mesh"))
    ((geomSubset, "GeomSubset"))
    ((materialBindingAPI, "MaterialBindingAPI"))
    ((skelBindingAPI, "SkelBindingAPI"))
    ((primvarsNormals, "primvars:normals"))
    ((primvarsSt, "primvars:st"))
    ((primvarsSt0, "primvars:st0"))
    ((primvarsSt1, "primvars:st1"))
    ((primvarsSt2, "primvars:st2"))
    ((subsetFamily, "subsetFamily:materialBind:familyType"))
    ((nonOverlapping, "nonOverlapping"))
    ((elementType, "elementType"))
    ((face, "face"))
    ((familyName, "familyName"))
    ((materialBind, "materialBind"))
    ((indices, "indices"))
    ((xformOpTransform, "xformOp:transform"))
    ((primvarsDoNotCastShadows, "primvars:doNotCastShadows"))
);
// LCOV_EXCL_STOP
// clang-format on


namespace omni::scene::optimizer
{


// static thread-safe memory pool (inactive by default)
typedef ConcurrentVectorMemoryPool<int> IntPool;
static IntPool s_intPool(false);

// static configurable list of attribute names to treat as primvars when merging
static std::set<TfToken> s_mergeAttrsAsPrimvars;


// Simple optimization structure that uses a pre-allocated array of integers to map vertex indices to their new indices
// when slicing geometry
class VertexIndexFastMap
{
public:
    VertexIndexFastMap(size_t range, size_t offset)
        : m_map(s_intPool.allocate(range))
        , m_offset(offset)
    {
        m_map.get().resize(range, -1);
    }

    int& operator[](size_t index)
    {
        return m_map.get()[index - m_offset];
    }

private:
    IntPool::Vec m_map;
    size_t m_offset;
};


// Internal object used by VirtualMesh to store Sdf safe data for a Usd Attribute.
class AttributeData
{
public:
    // non-value data that represents the attribute
    TfToken m_name;
    SdfValueTypeName m_typeName;
    SdfVariability m_variability;
    bool m_custom;
    UsdMetadataValueMap m_metadata;
    // value data
    VtValue m_defaultValue;
    SdfTimeSampleMap m_timeSamples;

    // Represents an empty invalid attribute
    AttributeData()
        : m_variability(SdfVariabilityVarying)
        , m_custom(false)
    {
    }

    // Constructs an AttributeData object by using the data from the given UsdAttribute.
    AttributeData(const UsdAttribute& attr)
        : m_name(attr.GetName())
        , m_typeName(attr.GetTypeName())
        , m_variability(attr.GetVariability())
        , m_custom(attr.IsCustom())
    {
        if (attr.HasAuthoredValue())
        {
            // get default value
            if (!attr.Get(&m_defaultValue, UsdTimeCode::Default()))
            {
                m_defaultValue = VtValue();
            }

            // get time samples
            std::vector<double> timeSamples;
            attr.GetTimeSamples(&timeSamples);
            for (double timeSample : timeSamples)
            {
                VtValue value;
                if (attr.Get(&value, timeSample))
                {
                    m_timeSamples.insert(std::make_pair(timeSample, value));
                }
            }
        }

        // copy metadata other than typename, variability, and custom since we store these explicitly
        for (const auto& [key, value] : attr.GetAllAuthoredMetadata())
        {
            if (key == SdfFieldKeys->TypeName || key == SdfFieldKeys->Variability || key == SdfFieldKeys->Custom)
            {
                continue;
            }
            m_metadata.insert(std::make_pair(key, value));
        }
    }

    // Constructs an AttributeData object using the given data
    //
    // TODO: support metadata here
    // TODO: support timesamples here
    AttributeData(const TfToken& name,
                  const SdfValueTypeName& typeName,
                  const SdfVariability& variability,
                  bool custom,
                  VtValue&& defaultValue)
        : m_name(name)
        , m_typeName(typeName)
        , m_variability(variability)
        , m_custom(custom)
        , m_defaultValue(defaultValue)
    {
    }

    // Returns whether this AttributeData object is valid.
    bool isValid() const
    {
        return !m_name.IsEmpty() || m_typeName;
    }
};
// typedef for a map from attribute names to their data
typedef std::unordered_map<TfToken, AttributeData, TfToken::HashFunctor> AttributeDataMap;


// Helper class used for constructing new indexed primvar values when either slicing or merging primvars.
template <typename T>
class Reindexer
{
private:
    // Helper hash function to allow getting at the real hash functions, for use in unordered_map
    struct HashFunc
    {
        size_t operator()(const T& val) const
        {
            return VtHashValue(val);
        }
    };

public:
    // The default value to use as a fallback
    T m_default;
    // The resulting values after being indexed
    VtArray<T> m_values;
    // A map from values to their indices
    std::unordered_map<T, int, HashFunc> m_indexMap;

    Reindexer()
    {
    }

    Reindexer(const VtValue& defaultValue)
        : m_default(defaultValue.UncheckedGet<T>())
    {
    }

    // Indexes the value at the given offset in the given array, returning the index of the value after reindexing.
    int indexPrimvarValue(const VtArray<T>& values, size_t offset)
    {
        // resolve the value
        T value = m_default;
        if (offset < values.size())
        {
            value = values[offset];
        }

        // already indexed?
        auto findIt = m_indexMap.find(value);
        if (findIt != m_indexMap.end())
        {
            return findIt->second;
        }

        // If not found, then we need to add a new value, get its index and insert it.
        int index = int(m_values.size());
        m_values.push_back(value);

        // Insert. Note it is possible that we may have inserted a duplicate value, so
        // we will still use the returned iterator value.
        //
        // The worst case here is we end up with some extra values and are not 100%
        // de-duplicated.
        auto insertIt = m_indexMap.insert(std::make_pair(value, index));
        return insertIt.first->second;
    }

    // Resolves and returns the values of this Reindexer, if flattened is true then the indices will be used to return a
    // flattened version of the values.
    void resolveValues(bool flattened, const VtIntArray& indices, VtArray<T>& values)
    {
        // if we don't need to flatten the values, just return as is
        if (!flattened)
        {
            values = m_values;
            return;
        }

        // otherwise flatten using the indices
        values.clear();
        values.reserve(indices.size());
        for (int index : indices)
        {
            values.push_back(m_values[index]);
        }
    }
};


// Internal object used by VirtualMesh to store Sdf safe data for a Usd Primvar.
class PrimvarData
{
public:
    // typedef for a vector of primvar data paired with the VirtualMesh it belongs to
    typedef std::vector<std::pair<PrimvarData, VirtualMesh>> PrimvarMeshVector;

    // the data for the primvar attribute
    AttributeData m_attrData;
    // the method used for interpolating this primvars values
    TfToken m_interpolation;
    // the indices used for indexing the primvar values - will be empty if the primvar is not indexed.
    VtIntArray m_indices;
    // signifies if this Primvar data represents normals
    bool m_isNormals = false;

private:
    // cache of the flattened values of this privmar as its important for performance to avoid recomputing the flattened
    // values many times
    mutable VtValue m_flattenedCache;

public:
    // Constructs empty and invalid PrimvarData.
    PrimvarData()
        : m_interpolation(UsdGeomTokens->none)
    {
    }

    // Constructs an PrimvarData object by using the data from the given UsdGeomPrimvar.
    PrimvarData(const UsdGeomPrimvar& primvar)
        : m_attrData(primvar.GetAttr())
        , m_interpolation(primvar.GetInterpolation())
        , m_isNormals(primvar.GetName() == _tokens->primvarsNormals)
    {
        if (primvar.IsIndexed())
        {
            UsdAttribute indicesAttr = primvar.GetIndicesAttr();
            if (indicesAttr)
            {
                indicesAttr.Get(&m_indices);
            }
        }
    }

    // Constructs an PrimvarData object by using the data from the given UsdAttribute and interpolation method.
    PrimvarData(const UsdAttribute& attr, const TfToken& interpolation, bool isNormals = false)
        : m_attrData(attr)
        , m_interpolation(interpolation)
        , m_isNormals(isNormals)
    {
    }

    // Constructs a PrimvarData object using the given data
    PrimvarData(const TfToken& name,
                const SdfValueTypeName& typeName,
                VtValue&& defaultValue,
                const TfToken& interpolation,
                const VtIntArray& indices = VtIntArray())
        : m_attrData(name, typeName, SdfVariabilityVarying, false, std::move(defaultValue))
        , m_interpolation(interpolation)
        , m_indices(indices)
    {
    }

    // Static method for merging a vector of PrimvarData objects into a single PrimvarData object.
    static PrimvarData mergePrimvars(const TfToken& name, const VtValue& defaultValue, const PrimvarMeshVector& primvarData)
    {
        // no data
        if (primvarData.empty())
        {
            return PrimvarData();
        }

        // TODO: handle primvars with only timesamples and no default value
        if (defaultValue.IsHolding<bool>())
        {
            return mergePrimvarsTyped<bool>(name, defaultValue, primvarData);
        }
        else if (defaultValue.IsHolding<int>())
        {
            return mergePrimvarsTyped<int>(name, defaultValue, primvarData);
        }
        else if (defaultValue.IsHolding<float>())
        {
            return mergePrimvarsTyped<float>(name, defaultValue, primvarData);
        }
        else if (defaultValue.IsHolding<TfToken>())
        {
            return mergePrimvarsTyped<TfToken>(name, defaultValue, primvarData);
        }
        else if (defaultValue.IsHolding<std::string>())
        {
            return mergePrimvarsTyped<std::string>(name, defaultValue, primvarData);
        }
        else if (defaultValue.IsHolding<GfVec2f>())
        {
            return mergePrimvarsTyped<GfVec2f>(name, defaultValue, primvarData);
        }
        else if (defaultValue.IsHolding<GfVec3f>())
        {
            return mergePrimvarsTyped<GfVec3f>(name, defaultValue, primvarData);
        }
        else
        {
            std::ostringstream oss;
            oss << "Skipping primvar attribute " << name
                << " while merging mesh subsets with unsupported primvar type: " << defaultValue.GetTypeName();
            SO_LOG_WARN(oss.str().c_str());
        }

        return PrimvarData();
    }

    // returns whether this PrimvarData object is valid.
    bool isValid() const
    {
        return m_attrData.isValid();
    }

    // Returns the flattened values of this primvar as a VtArray of the given type.
    // The values are internally cached by this function for performance reasons.
    template <typename T>
    bool getFlattenedValues(VtArray<T>& values) const
    {
        // already cached?
        if (!m_flattenedCache.IsEmpty())
        {
            values = m_flattenedCache.UncheckedGet<VtArray<T>>();
            return true;
        }

        if (m_attrData.m_defaultValue.IsEmpty())
        {
            return false;
        }

        VtArray<T> originalValues = m_attrData.m_defaultValue.UncheckedGet<VtArray<T>>();
        auto originValuesRaw = originalValues.cdata();

        // no indices? - just return raw values
        if (m_indices.empty())
        {
            values = originalValues;
            return true;
        }

        // note: we can't use the UsdGeomPrimvar ComputeFlattened method here as we're bound to Sdf at this point.
        // flatten indices
        values.reserve(m_indices.size());
        bool hasOutOfBoundsIndex = false;
        for (int index : m_indices)
        {
            // Add bounds checking to prevent accessing out-of-bounds data
            if (index >= 0 && index < static_cast<int>(originalValues.size()))
            {
                values.push_back(originValuesRaw[index]);
            }
            else
            {
                // Register the problem and use first available value to prevent crash
                hasOutOfBoundsIndex = true;
                if (!originalValues.empty())
                {
                    values.push_back(originValuesRaw[0]);
                }
                else
                {
                    // If no values available, create a default value
                    values.push_back(T());
                }
            }
        }

        // Log warning outside the loop if there were out of bounds indices
        if (hasOutOfBoundsIndex)
        {
            SO_LOG_WARN("Index out of bounds in getFlattenedValues, using first available value");
        }

        // cache
        m_flattenedCache = VtValue(values);

        // TODO: support flattening time-sampled values

        return true;
    }

    // Returns a new instance of Primvar data that is rotated if it represents normals
    PrimvarData rotateNormals(const GfMatrix4d& rotationMatrix) const
    {
        // make a copy of this data but ensure the flattened cache is cleared
        PrimvarData rotatedPrimvar = *this;
        rotatedPrimvar.m_flattenedCache = VtValue();

        // only do any rotation for normals with a valid type
        if (m_isNormals && m_attrData.m_typeName.GetType().IsA<VtVec3fArray>())
        {
            // TODO: support for time-sampled normals
            // create new data for the rotated default normals
            if (!m_attrData.m_defaultValue.IsEmpty())
            {
                VtVec3fArray normals = m_attrData.m_defaultValue.UncheckedGet<VtVec3fArray>();
                VtVec3fArray rotatedNormals;
                rotatedNormals.reserve(normals.size());
                for (const GfVec3f& normal : normals)
                {
                    GfVec3f rotated(rotationMatrix.Transform(normal));
                    rotated.Normalize();
                    rotatedNormals.push_back(rotated);
                }
                // update the values on the new primvar
                rotatedPrimvar.m_attrData.m_defaultValue = VtValue(rotatedNormals);
            }

            // Time-sampled normals rotation is not yet implemented (see TODO above).
        }

        return rotatedPrimvar;
    }

    // Resets the data of this PrimvarData object to the given values - this clears the flattened cache.
    void reset(VtValue&& value, VtIntArray&& indices)
    {
        m_attrData.m_defaultValue = value;
        m_indices = indices;
        m_flattenedCache = VtValue();
    }

private:
    // Returns a weighting for the given interpolation method.
    static int getInterpolationWeight(const TfToken& interpolation)
    {
        if (interpolation == UsdGeomTokens->constant)
        {
            return 0;
        }
        else if (interpolation == UsdGeomTokens->uniform)
        {
            return 1;
        }
        else if (interpolation == UsdGeomTokens->faceVarying)
        {
            return 3;
        }

        return -1;
    }

    // Typed function for merging a vector of PrimvarData objects into a single PrimvarData object.
    template <typename T>
    static PrimvarData mergePrimvarsTyped(const TfToken& name,
                                          const VtValue& defaultValue,
                                          const PrimvarMeshVector& primvarData)
    {
        // resolve the type of the primvar
        const PrimvarData& firstPrimvar = primvarData.front().first;
        const SdfValueTypeName& typeName = firstPrimvar.m_attrData.m_typeName;

        // reindexer helper class
        Reindexer<T> reindexer(defaultValue);

        // resolve the interpolation of the new primvar and count the total number of faces, indices, and points
        TfToken interpolation = UsdGeomTokens->none;
        int interpolationWeight = -1;
        size_t totalFaces = 0;
        size_t totalIndices = 0;
        size_t totalPoints = 0;
        for (const auto& it : primvarData)
        {
            const PrimvarData& primvar = it.first;
            const VirtualMesh& virtualMesh = it.second;

            // count totals
            totalFaces += virtualMesh.getFaceVertexCounts().size();
            totalIndices += virtualMesh.getFaceVertexIndices().size();
            totalPoints += virtualMesh.getPoints().size();

            // first time check - no interpolation set yet
            if (interpolation == UsdGeomTokens->none)
            {
                interpolation = primvar.m_interpolation;
            }

            // interpolation is the same
            if (interpolation == primvar.m_interpolation)
            {
                // For constant, we have a special case. See if the constant values match. If so we don't need to
                // expand. If they don't match then we push to uniform.
                if (interpolation == UsdGeomTokens->constant)
                {
                    // get the value from the primvar being process
                    VtArray<T> values;
                    primvar.getFlattenedValues(values);
                    if (!values.empty())
                    {
                        const T& value = values[0];
                        // If we haven't seen a value before then use this one
                        if (reindexer.m_values.empty())
                        {
                            reindexer.m_values.push_back(value);
                        }
                        else if (reindexer.m_values[0] != value)
                        {
                            // If we've seen a value, and it doesn't match, we need to bump the output interpolation to
                            // uniform
                            interpolation = UsdGeomTokens->uniform;
                        }
                    }
                }
            }
            // if we are currently vertex/varying, and the next interpolation is something else, we just go straight to
            // faceVarying as we don't support mixing/upgrading those types
            else if (interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying)
            {
                interpolation = UsdGeomTokens->faceVarying;
            }
            // interpolation is different
            else if (interpolation != primvar.m_interpolation)
            {
                int weight = getInterpolationWeight(primvar.m_interpolation);
                // if the interpolation is not found it's not one we support for output, which basically means "use
                // faceVarying"
                if (weight == -1)
                {
                    interpolation = UsdGeomTokens->faceVarying;
                }
                else if (weight > interpolationWeight)
                {
                    interpolation = primvar.m_interpolation;
                }
            }

            // update interpolation weight
            interpolationWeight = getInterpolationWeight(interpolation);
        }

        // no work to do if this constant
        if (interpolation == UsdGeomTokens->constant)
        {
            return PrimvarData(name, typeName, VtValue(reindexer.m_values), interpolation);
        }
        // otherwise if not constant make sure the values are cleared (a constant value may have been added while
        // processing)
        reindexer.m_values.clear();

        // create a correctly sized indices array and reserve the memory for values
        VtIntArray indices;
        if (interpolation == UsdGeomTokens->uniform)
        {
            indices.resize(totalFaces);
            reindexer.m_values.reserve(totalFaces);
        }
        else if (interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying)
        {
            indices.resize(totalPoints);
            reindexer.m_values.reserve(totalPoints);
        }
        else if (interpolation == UsdGeomTokens->faceVarying)
        {
            indices.resize(totalIndices);
            reindexer.m_values.reserve(totalIndices);
        }
        // interpolation wasn't resolved?
        else
        {
            return PrimvarData();
        }

        // Validate that the indices array is properly sized
        if (indices.empty())
        {
            SO_LOG_WARN("Empty indices array in mergePrimvarsTyped.");
            return PrimvarData();
        }

        // merge the primvars
        size_t offset = 0;
        bool hasBufferOverflow = false;
        for (const auto& it : primvarData)
        {
            const PrimvarData& primvar = it.first;
            const VirtualMesh& virtualMesh = it.second;
            const VtIntArray& faceVertexCounts = virtualMesh.getFaceVertexCounts();
            size_t nFaces = faceVertexCounts.size();
            size_t nIndices = virtualMesh.getFaceVertexIndices().size();
            size_t nPoints = virtualMesh.getPoints().size();

            // compute the flattened values
            VtArray<T> values;
            primvar.getFlattenedValues(values);

            // map values into new interpolation
            if (interpolation == UsdGeomTokens->uniform)
            {
                // map a constant primvar to the new uniform primvar
                if (primvar.m_interpolation == UsdGeomTokens->constant)
                {
                    int index = reindexer.indexPrimvarValue(values, 0);
                    for (size_t i = 0; i < nFaces; ++i)
                    {
                        if (offset < indices.size())
                        {
                            indices[offset++] = index;
                        }
                        else
                        {
                            hasBufferOverflow = true;
                            break;
                        }
                    }
                }
                // else its already uniform
                else
                {
                    const size_t valuesToProcess = std::min(values.size(), nFaces);
                    for (size_t i = 0; i < valuesToProcess; ++i)
                    {
                        if (offset < indices.size())
                        {
                            indices[offset++] = reindexer.indexPrimvarValue(values, i);
                        }
                        else
                        {
                            hasBufferOverflow = true;
                            break;
                        }
                    }
                    // Pad remaining positions with default value if we have insufficient values
                    for (size_t i = valuesToProcess; i < nFaces && offset < indices.size(); ++i)
                    {
                        indices[offset++] = reindexer.indexPrimvarValue(values, i); // This will use default value
                    }
                }
            }
            else if (interpolation == UsdGeomTokens->vertex || interpolation == UsdGeomTokens->varying)
            {
                const size_t valuesToProcess = std::min(values.size(), nPoints);
                for (size_t i = 0; i < valuesToProcess; ++i)
                {
                    if (offset < indices.size())
                    {
                        indices[offset++] = reindexer.indexPrimvarValue(values, i);
                    }
                    else
                    {
                        hasBufferOverflow = true;
                        break;
                    }
                }
                // Pad remaining positions with default value if we have insufficient values
                for (size_t i = valuesToProcess; i < nPoints && offset < indices.size(); ++i)
                {
                    indices[offset++] = reindexer.indexPrimvarValue(values, i); // This will use default value
                }
            }
            else if (interpolation == UsdGeomTokens->faceVarying)
            {
                if (primvar.m_interpolation == UsdGeomTokens->constant)
                {
                    int index = reindexer.indexPrimvarValue(values, 0);
                    for (size_t i = 0; i < nIndices; ++i)
                    {
                        if (offset < indices.size())
                        {
                            indices[offset++] = index;
                        }
                        else
                        {
                            hasBufferOverflow = true;
                            break;
                        }
                    }
                }
                else if (primvar.m_interpolation == UsdGeomTokens->uniform)
                {
                    // get one value per face
                    const size_t valuesToProcess = std::min(values.size(), nFaces);
                    for (size_t i = 0; i < valuesToProcess; ++i)
                    {
                        int index = reindexer.indexPrimvarValue(values, i);
                        // insert the per-face value for each point
                        for (int j = 0; j < faceVertexCounts[i] && offset < indices.size(); ++j)
                        {
                            indices[offset++] = index;
                        }
                        if (offset >= indices.size())
                        {
                            hasBufferOverflow = true;
                            break;
                        }
                    }
                    // Pad remaining faces with default value if we have insufficient values
                    for (size_t i = valuesToProcess; i < nFaces; ++i)
                    {
                        int index = reindexer.indexPrimvarValue(values, i); // This will use default value
                        // insert the per-face value for each point
                        for (int j = 0; j < faceVertexCounts[i] && offset < indices.size(); ++j)
                        {
                            indices[offset++] = index;
                        }
                        if (offset >= indices.size())
                        {
                            hasBufferOverflow = true;
                            break;
                        }
                    }
                }
                else if (primvar.m_interpolation == UsdGeomTokens->vertex ||
                         primvar.m_interpolation == UsdGeomTokens->varying)
                {
                    // add values per index - indexPrimvarValue will use default value for out-of-bounds indices
                    for (int index : virtualMesh.getFaceVertexIndices())
                    {
                        if (offset < indices.size())
                        {
                            indices[offset++] = reindexer.indexPrimvarValue(values, index);
                        }
                        else
                        {
                            hasBufferOverflow = true;
                            break;
                        }
                    }
                }
                else if (primvar.m_interpolation == UsdGeomTokens->faceVarying)
                {
                    // already face varying, just insert new indices
                    const size_t valuesToProcess = std::min(values.size(), nIndices);
                    for (size_t i = 0; i < valuesToProcess; ++i)
                    {
                        if (offset < indices.size())
                        {
                            indices[offset++] = reindexer.indexPrimvarValue(values, i);
                        }
                        else
                        {
                            // Register buffer overflow and break to prevent buffer overflow
                            hasBufferOverflow = true;
                            break;
                        }
                    }

                    // Pad remaining positions with default value if we have insufficient values
                    for (size_t i = valuesToProcess; i < nIndices && offset < indices.size(); ++i)
                    {
                        indices[offset++] = reindexer.indexPrimvarValue(values, i); // This will use default value
                    }
                }
            }
        }

        // Log warning outside the loop if there were buffer overflows
        if (hasBufferOverflow)
        {
            SO_LOG_WARN("Buffer overflow prevented in mergePrimvarsTyped");
        }

        // resolve the data for the new primvar
        VtArray<T> values;
        reindexer.resolveValues(false, indices, values);

        // return new primvar data
        return PrimvarData(name, typeName, VtValue(values), interpolation, indices);
    }
};
// typedef for a map from primvar names to their data
typedef std::unordered_map<TfToken, PrimvarData, TfToken::HashFunctor> PrimvarDataMap;


// Internal object used by VirtualMesh to store Sdf safe data for a Usd Relationship.
class RelationshipData
{
public:
    TfToken m_name;
    SdfPathVector m_targets;
    UsdMetadataValueMap m_metadata;

    // Creates new RelationshipData object with the given name but no target.
    RelationshipData(const TfToken& name)
        : m_name(name)
    {
    }

    // Constructs an RelationshipData object by using the data from the given UsdRelationship.
    RelationshipData(const UsdRelationship& rel)
        : m_name(rel.GetName())
    {
        // get targets
        rel.GetTargets(&m_targets);
        // get metadata
        m_metadata = rel.GetAllAuthoredMetadata();
    }
};
// typedef for a map from relationship names to their data
typedef std::unordered_map<TfToken, RelationshipData, TfToken::HashFunctor> RelationshipDataMap;


// (Mostly) immutable data structure that is used to implement copy-on-write semantics for data that is shared between
// VirtualMeshes.
// SharedData may be shared by multiple VirtualMesh instances, but in order to modify the data, a new instance must be
// created.
// However faceVertexIndicesOffsets and subsetAttributeBlacklist are mutable as they are intended
// to be lazily computed and cached.
class SharedData
{
public:
    // The source prim path this data was derived from - can be empty if the data was not derived from a prim
    const SdfPath m_sourcePath;

    const UsdMetadataValueMap m_metadata;
    const AttributeDataMap m_attributes;
    const PrimvarDataMap m_primvars;

    // whether the geometry data was derived from a prim that geometry time-sampled data. This can be removed at a later
    // date when we support time-sampled geometry.
    const bool m_hasTimeSamples = false;
    // The number of vertices per face in the geometry
    const VtIntArray m_faceVertexCounts;
    // The index of each vertex in the geometry into the points array
    const VtIntArray m_faceVertexIndices;
    // The points of the geometry
    const VtVec3fArray m_points;
    // Indices of faces that should be treated as holes in the mesh
    const VtIntArray m_holeIndices;
    // subdivision surface data
    const VtIntArray m_cornerIndices;
    const VtFloatArray m_cornerSharpnesses;
    const VtIntArray m_creaseIndices;
    const VtIntArray m_creaseLengths;
    const VtFloatArray m_creaseSharpnesses;

    // the local-space extent of the geometry
    const VtVec3fArray m_extent;

    // skeleton data
    const VtTokenArray m_jointNames;
    const VtIntArray m_jointIndices;
    const int m_jointIndicesElementSize = 1;
    const VtFloatArray m_jointWeights;
    const int m_jointWeightsElementSize = 1;

    // mapping from material paths to face indices that make up material GeomSubsets
    std::map<SdfPath, VtIntArray> m_materialSubsets;
    // signifies the material path in the material subsets map that actually represents the unassigned faces
    SdfPath m_unassignedSubset;

    // offsets of the faceVertexIndices array for each face in the subset - this may be pre-computed by the original
    // VirtualMesh
    std::unique_ptr<std::vector<int>> m_faceVertexIndicesOffsets;
    // set of attributes to ignore when creating a subset from this VirtualMesh / or computing the subset of this mesh
    std::unique_ptr<TfToken::HashSet> m_subsetAttributeBlacklist;

    // Creates new empty data
    SharedData()
    {
    }

    // Creates new SharedData without faceVertexIndicesOffsets or subsetAttributeBlacklist set yet
    SharedData(const SdfPath& sourcePath,
               UsdMetadataValueMap&& metadata,
               AttributeDataMap&& attributes,
               PrimvarDataMap&& primvars,
               bool hasTimeSamples,
               VtIntArray&& faceVertexCounts,
               VtIntArray&& faceVertexIndices,
               VtVec3fArray&& points,
               VtIntArray&& holeIndices,
               VtIntArray&& cornerIndices,
               VtFloatArray&& cornerSharpnesses,
               VtIntArray&& creaseIndices,
               VtIntArray&& creaseLengths,
               VtFloatArray&& creaseSharpnesses,
               VtVec3fArray&& extent,
               VtTokenArray&& jointNames,
               VtIntArray&& jointIndices,
               int jointIndicesElementSize,
               VtFloatArray&& jointWeights,
               int jointWeightsElementSize)
        : m_sourcePath(sourcePath)
        , m_metadata(std::move(metadata))
        , m_attributes(std::move(attributes))
        , m_primvars(std::move(primvars))
        , m_hasTimeSamples(hasTimeSamples)
        , m_faceVertexCounts(std::move(faceVertexCounts))
        , m_faceVertexIndices(std::move(faceVertexIndices))
        , m_points(std::move(points))
        , m_holeIndices(std::move(holeIndices))
        , m_cornerIndices(std::move(cornerIndices))
        , m_cornerSharpnesses(std::move(cornerSharpnesses))
        , m_creaseIndices(std::move(creaseIndices))
        , m_creaseLengths(std::move(creaseLengths))
        , m_creaseSharpnesses(std::move(creaseSharpnesses))
        , m_extent(std::move(extent))
        , m_jointNames(std::move(jointNames))
        , m_jointIndices(std::move(jointIndices))
        , m_jointIndicesElementSize(jointIndicesElementSize)
        , m_jointWeights(std::move(jointWeights))
        , m_jointWeightsElementSize(jointWeightsElementSize)
    {
    }

    // delete copy constructor and assignment operator
    SharedData(const SharedData&) = delete;
    SharedData& operator=(const SharedData&) = delete;

    // Computes the face vertex index offsets and the blacklist of attributes to not be included in the subset of this
    // data.
    void precomputeSubsetCache()
    {
        if (m_faceVertexIndicesOffsets == nullptr)
        {
            m_faceVertexIndicesOffsets.reset(new std::vector<int>);
            m_faceVertexIndicesOffsets->reserve(m_faceVertexCounts.size());

            int offset = 0;
            for (int faceCount : m_faceVertexCounts)
            {
                m_faceVertexIndicesOffsets->push_back(offset);
                offset += faceCount;
            }
        }

        if (m_subsetAttributeBlacklist == nullptr)
        {
            m_subsetAttributeBlacklist.reset(new TfToken::HashSet());
            for (auto& [attrName, attrData] : m_attributes)
            {
                if (attrData.m_custom && attrData.m_typeName.IsArray())
                {
                    // For single-values, eg maybe a constant value, just copy them. For anything larger skip
                    // and log a warning. At least this means the decision is discoverable in the report
                    size_t arraySize = attrData.m_defaultValue.GetArraySize();
                    if (arraySize > 1)
                    {
                        m_subsetAttributeBlacklist->insert(attrName);
                        std::ostringstream oss;
                        oss << "Skipping unknown array attribute " << m_sourcePath.GetAsString() << "." << attrName
                            << " while splitting mesh into subsets (size=" << arraySize << ")";
                        SO_LOG_WARN(oss.str().c_str());
                    }
                }
            }
        }
    }
};


// single shared instance of empty SharedData (for invalid VirtualMeshes)
static std::shared_ptr<SharedData> s_emptySharedData(new SharedData());


// templated function to slice a primvar - internally used by _slicePrimvar
template <typename T>
static PrimvarData _slicePrimvarTyped(const TfToken& name,
                                      const PrimvarData& primvar,
                                      const SharedData* sharedData,
                                      size_t newPointsSize,
                                      VertexIndexFastMap& vertexIndicesMap,
                                      const std::vector<int>& faceVertexCountIndices,
                                      const T& defaultValue)
{
    // start with a copy of the original primvar and we'll modify it as needed
    PrimvarData slicedPrimvar(primvar);
    slicedPrimvar.m_attrData.m_name = name;

    // nothing to do for constant primvars - return a copy of the original primvar
    if (primvar.m_interpolation == UsdGeomTokens->constant)
    {
        return slicedPrimvar;
    }

    auto faceVertexCountsRaw = sharedData->m_faceVertexCounts.cdata();
    auto faceVertexIndicesRaw = sharedData->m_faceVertexIndices.cdata();

    // get current values
    VtArray<T> oldValues;
    if (!primvar.getFlattenedValues(oldValues))
    {
        return slicedPrimvar;
    }

    // reindexer helper class
    Reindexer<T> reindexer;
    VtIntArray indices;

    // handle by interpolation type
    if (primvar.m_interpolation == UsdGeomTokens->uniform)
    {
        // one value per face, so just look them up based on face count indices
        indices.reserve(faceVertexCountIndices.size());
        reindexer.m_values.reserve(faceVertexCountIndices.size());
        for (int faceIndex : faceVertexCountIndices)
        {
            indices.push_back(reindexer.indexPrimvarValue(oldValues, faceIndex));
        }
    }
    else if (primvar.m_interpolation == UsdGeomTokens->vertex || primvar.m_interpolation == UsdGeomTokens->varying)
    {
        // slice using the vertex indices - but retain the indices order
        indices.resize(newPointsSize);
        reindexer.m_values.reserve(newPointsSize);
        for (int faceIndex : faceVertexCountIndices)
        {
            int offset = (*sharedData->m_faceVertexIndicesOffsets)[faceIndex];
            for (int i = 0; i < faceVertexCountsRaw[faceIndex]; ++i)
            {
                int faceVertexIndex = faceVertexIndicesRaw[offset + i];
                int index = reindexer.indexPrimvarValue(oldValues, faceVertexIndex);
                indices[vertexIndicesMap[faceVertexIndex]] = index;
            }
        }
    }
    else if (primvar.m_interpolation == UsdGeomTokens->faceVarying)
    {
        // first pass to count the number of indices for memory allocation
        size_t totalIndices = 0;
        for (int faceIndex : faceVertexCountIndices)
        {
            totalIndices += faceVertexCountsRaw[faceIndex];
        }
        indices.reserve(totalIndices);
        reindexer.m_values.reserve(totalIndices);

        // slice using the index offsets
        for (int faceIndex : faceVertexCountIndices)
        {
            int offset = (*sharedData->m_faceVertexIndicesOffsets)[faceIndex];
            for (int i = 0; i < faceVertexCountsRaw[faceIndex]; ++i)
            {
                indices.push_back(reindexer.indexPrimvarValue(oldValues, offset + i));
            }
        }
    }

    // resolve the data for the new primvar
    VtArray<T> values;
    reindexer.resolveValues(false, indices, values);

    // update the primvar data
    slicedPrimvar.reset(VtValue(values), VtIntArray(indices));
    return slicedPrimvar;
}


// Returns a new sliced primvar based on the given faceVertexCountIndices
static PrimvarData _slicePrimvar(const TfToken& name,
                                 const PrimvarData& primvar,
                                 const SharedData* sharedData,
                                 size_t newPointsSize,
                                 VertexIndexFastMap& vertexIndicesMap,
                                 const std::vector<int>& faceVertexCountIndices)
{
    // TODO: handle primvars with only timesamples and no default value
    // resolve type
    if (primvar.m_attrData.m_defaultValue.IsHolding<VtArray<float>>())
    {
        return _slicePrimvarTyped<float>(name,
                                         primvar,
                                         sharedData,
                                         newPointsSize,
                                         vertexIndicesMap,
                                         faceVertexCountIndices,
                                         0.0f);
    }
    else if (primvar.m_attrData.m_defaultValue.IsHolding<VtArray<GfVec2f>>())
    {
        return _slicePrimvarTyped<GfVec2f>(name,
                                           primvar,
                                           sharedData,
                                           newPointsSize,
                                           vertexIndicesMap,
                                           faceVertexCountIndices,
                                           GfVec2f(0.0f));
    }
    else if (primvar.m_attrData.m_defaultValue.IsHolding<VtArray<GfVec3f>>())
    {
        return _slicePrimvarTyped<GfVec3f>(name,
                                           primvar,
                                           sharedData,
                                           newPointsSize,
                                           vertexIndicesMap,
                                           faceVertexCountIndices,
                                           GfVec3f(0.0f));
    }
    // warn if this is a primvar type we don't expect to just flat copy
    else if (!primvar.m_attrData.m_defaultValue.IsHolding<bool>())
    {
        std::ostringstream oss;
        oss << "Skipping primvar attribute " << primvar.m_attrData.m_name << " on " << sharedData->m_sourcePath
            << " while slicing mesh subset with unsupported primvar type: "
            << primvar.m_attrData.m_defaultValue.GetTypeName();
        SO_LOG_WARN(oss.str().c_str());
    }

    // can't slice, just return a copy of the original primvar
    return PrimvarData(primvar);
}


/// Get any material UsdGeomSubsets for the specified prim.
///
/// Querying via UsdShadeMaterialBindingAPI unfortunately checks for a specific familyName, which is
/// optional. This function prefers to find that familyName, however, if there is _no_ familyName
/// then will also check for an explicit material:binding relationship.
static std::vector<UsdGeomSubset> _getMaterialSubsets(const UsdPrim& prim)
{

    std::vector<UsdGeomSubset> result;

    UsdGeomImageable imageable(prim);
    const std::vector<UsdGeomSubset>& subsets = UsdGeomSubset::GetAllGeomSubsets(imageable);

    for (const auto& subset : subsets)
    {
        TfToken familyName;
        if (subset.GetFamilyNameAttr().Get(&familyName))
        {
            if (!familyName.IsEmpty())
            {
                if (familyName == UsdShadeTokens->materialBind)
                {
                    result.push_back(subset);
                }

                continue;
            }
        }

        // If there was no family name found, check for an explicit material binding.
        if (subset.GetPrim().HasRelationship(UsdShadeTokens->materialBinding))
        {
            result.push_back(subset);
        }
    }

    return result;
}


/// Updates the running extents of the mesh based on the given point.
static void _updateExtent(const GfVec3f& point, GfVec3f& min, GfVec3f& max)
{
    for (size_t i = 0; i < 3; ++i)
    {
        if (point[i] < min[i])
        {
            min[i] = point[i];
        }
        if (point[i] > max[i])
        {
            max[i] = point[i];
        }
    }
}


/// Returns the signed volume of the tetrahedron formed by the given points.
static float _signedTetrahedronVolume(const GfVec3d& p1, const GfVec3d& p2, const GfVec3d& p3)
{
    const double p321 = p3[0] * p2[1] * p1[2];
    const double p231 = p2[0] * p3[1] * p1[2];
    const double p312 = p3[0] * p1[1] * p2[2];
    const double p132 = p1[0] * p3[1] * p2[2];
    const double p213 = p2[0] * p1[1] * p3[2];
    const double p123 = p1[0] * p2[1] * p3[2];
    return (1.0f / 6.0f) * static_cast<float>((-p321 + p231 + p312 - p132 - p213 + p123));
}


/// Removes the MaterialBindingAPI schema from the given list of schemas
static bool _removeMaterialBindingSchema(const TfTokenVector& oldList, TfTokenVector& newList)
{
    newList.reserve(oldList.size());
    bool schemaRemoved = false;
    for (const TfToken& schema : oldList)
    {
        if (schema != _tokens->materialBindingAPI)
        {
            newList.push_back(schema);
        }
        else
        {
            schemaRemoved = true;
        }
    }
    return schemaRemoved;
}


/// Extracts the absolute scale from the given transform
void _extractScaleAbs(const GfMatrix4d& transform, GfVec3d& scale)
{
    GfMatrix4d r; // unused
    GfMatrix4d u; // unused
    GfVec3d t; // unused
    GfMatrix4d p; // unused
    transform.Factor(&r, &scale, &u, &t, &p);
    scale[0] = std::abs(scale[0]);
    scale[1] = std::abs(scale[1]);
    scale[2] = std::abs(scale[2]);
}


class VirtualMesh::Impl
{
public:
    // reference counter
    size_t m_refCount = 1;

    // Unique id for this VirtualMesh
    size_t m_id;

    // Whether this VirtualMesh is directly derived from a prim
    bool m_derivedFromPrim;
    bool m_isSubset = false;
    bool m_isSuperset = false;

    // The UsdPrim this VirtualMesh is directly or originally derived from
    UsdPrim m_prim;
    // The parent path that the resulting prim should be created under
    SdfPath m_destinationParentPath;
    // The name of the resulting prim
    TfToken m_destinationName;

    bool m_valid;
    bool m_active;

    // prim data
    SdfSpecifier m_specifier;
    TfToken m_typeName;

    // shared data about the prim
    std::shared_ptr<SharedData> m_data;

    // overrides on top of the SharedData
    AttributeDataMap m_attributeOverrides;
    PrimvarDataMap m_primvarOverrides;

    // api schemas and relationships are not part of the SharedData because they need to be modifiable for material
    // bindings
    SdfListOp<TfToken> m_apiSchemas;
    RelationshipDataMap m_relationships;

    // whether this mesh has an explicitly bound (or unbound) material
    bool m_explicitMaterialBinding = false;
    // the bound material path of this mesh
    SdfPath m_materialPath;

    bool m_geometryComputed = false;

    // world data
    GfMatrix4d m_localToWorldTransform;
    GfMatrix4d m_rootLocalToWorldTransform;
    GfMatrix4d m_rotationMatrix;
    bool m_extentComputed = false;
    VtVec3fArray m_worldExtent;

    // subset data - indices into the faceVertexCounts array for each face in the subset
    std::vector<int> m_faceVertexCountIndices;

    // superset data
    std::vector<VirtualMesh> m_supersetChildren;
    size_t m_supersetDataVolume = 0;
    bool m_hasUnassignedSubset = false;
    SdfPath m_unassignedSubsetMaterial;

    // debug data for spatial clustering
    int m_spatialClusterId;

    // Creates a new empty and invalid VirtualMesh
    Impl()
        : m_id(getNextId())
        , m_derivedFromPrim(false)
        , m_valid(false)
        , m_active(false)
    {
    }

    // Superset constructor - creates a new VirtualMesh with the given data intended to be used as a superset.
    Impl(const SdfPath& destinationParentPath,
         const std::string& destinationName,
         const TfTokenVector& appliedSchemas,
         const UsdStageWeakPtr& stage,
         UsdGeomXformCache& xformCache,
         int spatialClusterId)
        : m_id(getNextId())
        , m_derivedFromPrim(false)
        , m_destinationParentPath(destinationParentPath)
        , m_destinationName(TfToken(destinationName.c_str()))
        , m_valid(true)
        , m_active(true)
        , m_data(s_emptySharedData)
        , m_spatialClusterId(spatialClusterId)
    {
        m_apiSchemas.SetPrependedItems(appliedSchemas);

        // We need the local to world transform of the destination in order to determine the transforms of
        // merged meshes later. The destinationParentPath may already exist, but in some cases it won't,
        // such as if we're merging with a custom rootPath that hasn't been created yet. Check for the
        // destination, but then keep checking any of its ancestors. If nothing at all is found then default
        // to identity (rather than an uninitialized matrix).
        m_rootLocalToWorldTransform.SetIdentity();

        for (const auto& path : destinationParentPath.GetAncestorsRange())
        {
            const auto& prim = stage->GetPrimAtPath(path);
            if (prim.IsValid())
            {
                m_rootLocalToWorldTransform = xformCache.GetLocalToWorldTransform(prim);
                break;
            }
        }
    }

    // Derived prim constructor - creates a new VirtualMesh with the given prim data.
    Impl(const UsdPrim& prim,
         UsdGeomXformCache& xformCache,
         UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
         UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache)
        : m_id(getNextId())
        , m_derivedFromPrim(true)
        , m_prim(prim)
        , m_destinationParentPath(prim.GetPath().GetParentPath())
        , m_destinationName(prim.GetPath().GetNameToken())
        , m_valid(prim.IsValid())
        , m_active(prim.IsActive())
    {
        // collect metadata
        UsdMetadataValueMap metadata;
        for (const auto& [key, value] : prim.GetAllAuthoredMetadata())
        {
            // specific metadata
            if (key == SdfFieldKeys->Specifier)
            {
                m_specifier = value.Get<SdfSpecifier>();
            }
            else if (key == SdfFieldKeys->TypeName)
            {
                m_typeName = value.Get<TfToken>();
            }
            else if (key == _tokens->apiSchemas)
            {
                m_apiSchemas = value.Get<SdfListOp<TfToken>>();
            }
            // generic metadata
            else
            {
                metadata.insert(std::make_pair(key, value));
            }
        }

        // blacklist of attributes explicitly handled - extents and normals should never be handled as attributes
        TfToken::HashSet blacklist;
        blacklist.insert(UsdGeomTokens->extent);
        blacklist.insert(UsdGeomTokens->normals);
        blacklist.insert(_tokens->debugId);

        PrimvarDataMap primvars;
        AttributeDataMap attributes;

        // retrieve mesh data from the prim - this won't copy the data if we're not modifying it
        bool hasGeometryTimeSamples = false;
        VtIntArray faceVertexCounts;
        VtIntArray faceVertexIndices;
        VtVec3fArray points;
        VtIntArray holeIndices;
        VtIntArray cornerIndices;
        VtFloatArray cornerSharpnesses;
        VtIntArray creaseIndices;
        VtIntArray creaseLengths;
        VtFloatArray creaseSharpnesses;
        VtVec3fArray extent;
        std::map<SdfPath, VtIntArray> materialSubsets;
        SdfPath unassignedSubset;
        if (UsdGeomMesh mesh = UsdGeomMesh(prim))
        {
            UsdAttribute faceVertexCountsAttr = mesh.GetFaceVertexCountsAttr();
            faceVertexCountsAttr.Get(&faceVertexCounts);
            blacklist.insert(faceVertexCountsAttr.GetName());

            UsdAttribute faceVertexIndicesAttr = mesh.GetFaceVertexIndicesAttr();
            faceVertexIndicesAttr.Get(&faceVertexIndices);
            blacklist.insert(faceVertexIndicesAttr.GetName());

            UsdAttribute pointsAttr = mesh.GetPointsAttr();
            pointsAttr.Get(&points);
            blacklist.insert(pointsAttr.GetName());

            // get subdivision surface attributes if they exist on this mesh
            UsdAttribute holeIndicesAttr = mesh.GetHoleIndicesAttr();
            if (holeIndicesAttr.IsValid())
            {
                holeIndicesAttr.Get(&holeIndices);
                blacklist.insert(holeIndicesAttr.GetName());
            }
            UsdAttribute cornerIndicesAttr = mesh.GetCornerIndicesAttr();
            if (cornerIndicesAttr.IsValid())
            {
                cornerIndicesAttr.Get(&cornerIndices);
                blacklist.insert(cornerIndicesAttr.GetName());
            }
            UsdAttribute cornerSharpnessesAttr = mesh.GetCornerSharpnessesAttr();
            if (cornerSharpnessesAttr.IsValid())
            {
                cornerSharpnessesAttr.Get(&cornerSharpnesses);
                blacklist.insert(cornerSharpnessesAttr.GetName());
            }
            UsdAttribute creaseIndicesAttr = mesh.GetCreaseIndicesAttr();
            if (creaseIndicesAttr.IsValid())
            {
                creaseIndicesAttr.Get(&creaseIndices);
                blacklist.insert(creaseIndicesAttr.GetName());
            }
            UsdAttribute creaseLengthsAttr = mesh.GetCreaseLengthsAttr();
            if (creaseLengthsAttr.IsValid())
            {
                creaseLengthsAttr.Get(&creaseLengths);
                blacklist.insert(creaseLengthsAttr.GetName());
            }
            UsdAttribute creaseSharpnessesAttr = mesh.GetCreaseSharpnessesAttr();
            if (creaseSharpnessesAttr.IsValid())
            {
                creaseSharpnessesAttr.Get(&creaseSharpnesses);
                blacklist.insert(creaseSharpnessesAttr.GetName());
            }

            // get extent
            UsdAttribute extentAttr = mesh.GetExtentAttr();
            if (extentAttr.IsValid())
            {
                extentAttr.Get(&extent);
            }
            // no extent attribute or invalid data? compute it ourselves
            if (extent.size() < 2)
            {
                UsdGeomPointBased::ComputeExtent(points, &extent);
            }

            if (faceVertexCountsAttr.ValueMightBeTimeVarying() || faceVertexIndicesAttr.ValueMightBeTimeVarying() ||
                pointsAttr.ValueMightBeTimeVarying())
            {
                hasGeometryTimeSamples = true;
            }

            // normals, velocities, and accelerations must be handled as a special case since they need to be
            // sliced/merged like a primvar
            if (!prim.GetAttribute(_tokens->primvarsNormals).IsValid())
            {
                UsdAttribute normalsAttr = mesh.GetNormalsAttr();
                if (normalsAttr.IsValid() && normalsAttr.HasAuthoredValue())
                {
                    // rename from "normals" to "primvars:normals" to clean up the usd
                    primvars.insert(std::make_pair(_tokens->primvarsNormals,
                                                   PrimvarData(normalsAttr, mesh.GetNormalsInterpolation(), true)));
                    // note: normals is already in the blacklist no need to add it
                }
            }
            // velocities should only be handled as a primvar if they're non-empty
            UsdAttribute velocitiesAttr = mesh.GetVelocitiesAttr();
            if (velocitiesAttr.IsValid())
            {
                VtVec3fArray velocities;
                velocitiesAttr.Get(&velocities);
                if (!velocities.empty())
                {
                    primvars.insert(
                        std::make_pair(velocitiesAttr.GetName(), PrimvarData(velocitiesAttr, UsdGeomTokens->vertex)));
                    blacklist.insert(velocitiesAttr.GetName());
                }
            }
            // accelerations should only be handled as a primvar if they're non-empty
            UsdAttribute accelerationsAttr = mesh.GetAccelerationsAttr();
            if (accelerationsAttr.IsValid())
            {
                VtVec3fArray accelerations;
                accelerationsAttr.Get(&accelerations);
                if (!accelerations.empty())
                {
                    primvars.insert(std::make_pair(accelerationsAttr.GetName(),
                                                   PrimvarData(accelerationsAttr, UsdGeomTokens->vertex)));
                    blacklist.insert(accelerationsAttr.GetName());
                }
            }

            // resolve the material of the prim so we group by it later if we're not considering materials
            UsdShadeMaterialBindingAPI bindingAPI(mesh);
            UsdShadeMaterial boundMaterial = bindingAPI.ComputeBoundMaterial(&bindingsCache, &collQueryCache);
            SdfPath boundMaterialPath;
            if (boundMaterial)
            {
                // explicitly bind the material to this virtual mesh
                boundMaterialPath = boundMaterial.GetPath();
                bindMaterial(boundMaterial.GetPath(), UsdMetadataValueMap());
            }

            // resolve any material subsets
            std::vector<UsdGeomSubset> materialBindSubsets = _getMaterialSubsets(prim);
            if (!materialBindSubsets.empty())
            {
                // record the material subsets
                for (const auto& materialBindSubset : materialBindSubsets)
                {
                    UsdShadeMaterialBindingAPI subsetBindingAPI(materialBindSubset.GetPrim());
                    auto boundMaterial = subsetBindingAPI.ComputeBoundMaterial(&bindingsCache, &collQueryCache);

                    VtIntArray subsetFaceIndices;
                    materialBindSubset.GetIndicesAttr().Get(&subsetFaceIndices);
                    materialSubsets[boundMaterial.GetPath()] = subsetFaceIndices;
                }
                // we also need to record the unassigned faces (if there are any) as a subset
                VtIntArray faceIndices =
                    UsdGeomSubset::GetUnassignedIndices(materialBindSubsets, int(faceVertexCounts.size()));
                if (!faceIndices.empty())
                {
                    materialSubsets[boundMaterialPath] = faceIndices;
                    unassignedSubset = boundMaterialPath;
                }
            }
        }

        // retrieve skeleton data from the prim
        VtTokenArray jointNames;
        VtIntArray jointIndices;
        int jointIndicesElementSize = 0;
        VtFloatArray jointWeights;
        int jointWeightsElementSize = 0;
        if (UsdSkelBindingAPI skelBinding = UsdSkelBindingAPI(prim))
        {
            // get joint names
            UsdAttribute jointsAttr = skelBinding.GetJointsAttr();
            jointsAttr.Get(&jointNames);
            blacklist.insert(jointsAttr.GetName());

            // get joint indices
            UsdGeomPrimvar jointIndicesPrimvar = skelBinding.GetJointIndicesPrimvar();
            jointIndicesPrimvar.Get(&jointIndices);
            jointIndicesElementSize = jointIndicesPrimvar.GetElementSize();
            blacklist.insert(jointIndicesPrimvar.GetName());

            // get joint weights
            UsdGeomPrimvar jointWeightsPrimvar = skelBinding.GetJointWeightsPrimvar();
            jointWeightsPrimvar.Get(&jointWeights);
            jointWeightsElementSize = jointWeightsPrimvar.GetElementSize();
            blacklist.insert(jointWeightsPrimvar.GetName());

            // If interpolation is constant, expand it out to vertex interpolation.
            if (jointIndicesPrimvar.GetInterpolation() == UsdGeomTokens->constant)
            {
                UsdSkelExpandConstantInfluencesToVarying(&jointIndices, points.size());
                UsdSkelExpandConstantInfluencesToVarying(&jointWeights, points.size());
            }
        }

        // discover primvars
        for (const UsdGeomPrimvar& primvar : UsdGeomPrimvarsAPI(prim).FindPrimvarsWithInheritance())
        {
            // primvars:doNotCastShadows is not actually a primvar, just a badly named attribute so we need to treat it
            // as such - yay for special cases
            if (primvar.GetName() == _tokens->primvarsDoNotCastShadows)
            {
                attributes.insert(std::make_pair(primvar.GetName(), AttributeData(primvar.GetAttr())));
                continue;
            }

            // blacklist indices regardless of whether the primvar is indexed or not (the attribute can exist but be
            // None), as we may be creating new indices for the primvar
            UsdAttribute indicesAttr = primvar.GetIndicesAttr();
            if (indicesAttr.IsValid())
            {
                blacklist.insert(primvar.GetIndicesAttr().GetName());
            }

            // primvar already blacklisted?
            if (blacklist.find(primvar.GetName()) != blacklist.end())
            {
                continue;
            }
            // blacklist the primvar
            blacklist.insert(primvar.GetAttr().GetName());

            // only handle if the primvar if it actually has an authored value
            if (primvar.HasAuthoredValue())
            {
                primvars.insert(std::make_pair(primvar.GetName(), PrimvarData(primvar)));
            }
        }

        // explicitly compute purpose since it may be inherited
        UsdGeomImageable imageable(prim);
        if (imageable)
        {
            const TfToken purpose = UsdGeomImageable(prim).ComputePurpose();
            attributes.insert(std::make_pair(UsdGeomTokens->purpose,
                                             AttributeData(UsdGeomTokens->purpose,
                                                           SdfValueTypeNames->Token,
                                                           SdfVariabilityUniform,
                                                           false,
                                                           VtValue(purpose))));
            blacklist.insert(UsdGeomTokens->purpose);
        }

        // process authored attributes on the prim
        for (const UsdAttribute& attr : prim.GetAuthoredAttributes())
        {
            // don't handle the attribute if its in the blacklist or is a primvar
            if (blacklist.find(attr.GetName()) != blacklist.end() || TfStringStartsWith(attr.GetName(), "primvars:"))
            {
                continue;
            }

            attributes.insert(std::make_pair(attr.GetName(), AttributeData(attr)));
        }

        // store relationships (this along with api schemas implicitly handles materials)
        for (const UsdRelationship& rel : prim.GetAuthoredRelationships())
        {
            m_relationships.insert(std::make_pair(rel.GetName(), RelationshipData(rel)));
        }

        // compute local to world transform
        m_localToWorldTransform = xformCache.GetLocalToWorldTransform(m_prim);

        // construct new shared data
        m_data.reset(new SharedData(m_prim.GetPath(),
                                    std::move(metadata),
                                    std::move(attributes),
                                    std::move(primvars),
                                    hasGeometryTimeSamples,
                                    std::move(faceVertexCounts),
                                    std::move(faceVertexIndices),
                                    std::move(points),
                                    std::move(holeIndices),
                                    std::move(cornerIndices),
                                    std::move(cornerSharpnesses),
                                    std::move(creaseIndices),
                                    std::move(creaseLengths),
                                    std::move(creaseSharpnesses),
                                    std::move(extent),
                                    std::move(jointNames),
                                    std::move(jointIndices),
                                    jointIndicesElementSize,
                                    std::move(jointWeights),
                                    jointWeightsElementSize));
        // set material subsets on shared data if they were discovered
        if (!materialSubsets.empty())
        {
            m_data->m_materialSubsets = std::move(materialSubsets);
            m_data->m_unassignedSubset = unassignedSubset;
        }
    }

    // Subset constructor, creates a new VirtualMesh that is a subset of the given VirtualMesh, copying relevant data
    // from the original mesh.
    Impl(const Impl& other, const std::vector<int>&& faceVertexCountIndices)
        : m_id(getNextId())
        , m_derivedFromPrim(false)
        , m_isSubset(true)
        , m_isSuperset(false)
        // while this is no longer derived from a prim, we still want ot keep track of the original prim this subset
        // was created from
        , m_prim(other.m_prim)
        , m_destinationParentPath(other.m_destinationParentPath)
        , m_destinationName(other.m_destinationName)
        , m_valid(other.m_valid)
        , m_active(other.m_active)
        , m_specifier(other.m_specifier)
        , m_typeName(other.m_typeName)
        , m_data(other.m_data)
        , m_attributeOverrides(other.m_attributeOverrides)
        , m_primvarOverrides(other.m_primvarOverrides)
        , m_apiSchemas(other.m_apiSchemas)
        , m_relationships(other.m_relationships)
        , m_explicitMaterialBinding(other.m_explicitMaterialBinding)
        , m_materialPath(other.m_materialPath)
        , m_geometryComputed(false)
        , m_localToWorldTransform(other.m_localToWorldTransform)
        , m_faceVertexCountIndices(std::move(faceVertexCountIndices))
    {
        // clear any material subsets on the data
        m_data->m_materialSubsets.clear();
    }

    std::vector<TfToken> getAttributeNames() const
    {
        std::vector<TfToken> attributeNames;
        attributeNames.reserve(m_data->m_attributes.size());
        for (const auto& attr : m_data->m_attributes)
        {
            attributeNames.push_back(attr.first);
        }
        return attributeNames;
    }

    void extendAttributeNameSet(TfToken::HashSet& nameSet) const
    {
        // extend with override primvars
        for (const auto& primvar : m_primvarOverrides)
        {
            if (primvar.first != UsdGeomTokens->purpose)
            {
                nameSet.insert(primvar.first);
            }
        }
        // extend with base primvars
        for (const auto& primvar : m_data->m_primvars)
        {
            if (primvar.first != UsdGeomTokens->purpose)
            {
                nameSet.insert(primvar.first);
            }
        }
        // extend with override attributes
        for (const auto& attr : m_attributeOverrides)
        {
            if (attr.first != UsdGeomTokens->purpose)
            {
                nameSet.insert(attr.first);
            }
        }
        // extend with base attributes
        for (const auto& attr : m_data->m_attributes)
        {
            if (attr.first != UsdGeomTokens->purpose)
            {
                nameSet.insert(attr.first);
            }
        }
    }

    bool getValue(const TfToken& name, VtValue& value) const
    {
        // check primvar overrides first
        auto findPrimvarOverride = m_primvarOverrides.find(name);
        if (findPrimvarOverride != m_primvarOverrides.end())
        {
            value = findPrimvarOverride->second.m_attrData.m_defaultValue;
            return true;
        }
        // check attribute overrides
        auto findAttributeOverride = m_attributeOverrides.find(name);
        if (findAttributeOverride != m_attributeOverrides.end())
        {
            value = findAttributeOverride->second.m_defaultValue;
            return true;
        }
        // check base primvars
        auto findPrimvar = m_data->m_primvars.find(name);
        if (findPrimvar != m_data->m_primvars.end())
        {
            value = findPrimvar->second.m_attrData.m_defaultValue;
            return true;
        }
        // check base attributes
        auto findAttribute = m_data->m_attributes.find(name);
        if (findAttribute != m_data->m_attributes.end())
        {
            value = findAttribute->second.m_defaultValue;
            return true;
        }
        // no attribute or primvar for the given name
        return false;
    }

    void setAttributeOverride(const TfToken& name,
                              const SdfValueTypeName& typeName,
                              const SdfVariability& variability,
                              bool custom,
                              VtValue&& defaultValue)
    {
        m_attributeOverrides[name] = AttributeData(name, typeName, variability, custom, std::move(defaultValue));
    }

    void getAppliedSchemas(TfTokenVector& appliedSchemas, bool includeMaterialBinding) const
    {
        appliedSchemas = m_apiSchemas.GetAppliedItems();
        if (includeMaterialBinding)
        {
            return;
        }
        // filter material binding applied schema?
        TfTokenVector filteredSchemas;
        for (const TfToken& schema : appliedSchemas)
        {
            if (schema != _tokens->materialBindingAPI)
            {
                filteredSchemas.push_back(schema);
            }
        }
        appliedSchemas = filteredSchemas;
    }

    bool isSkeleton() const
    {
        TfTokenVector appliedSchemas;
        getAppliedSchemas(appliedSchemas, true);

        for (const TfToken& schema : appliedSchemas)
        {
            if (schema == _tokens->skelBindingAPI)
            {
                return true;
            }
        }
        return false;
    }

    bool computeGeometry()
    {
        // no work to do?
        if (m_geometryComputed)
        {
            return true;
        }

        // superset geometry?
        if (m_isSuperset)
        {
            computeSupersetGeometry();
        }
        // subset or standard geometry
        else
        {
            // validate geometry is not empty
            if (m_data->m_faceVertexCounts.empty() || m_data->m_faceVertexIndices.empty() || m_data->m_points.empty())
            {
                return false;
            }
            // need to compute subset geometry?
            if (m_isSubset)
            {
                computeSubsetGeometry();
            }
        }

        m_geometryComputed = true;
        return true;
    }

    bool validateAndComputeExtent()
    {
        // no work to do?
        if (m_extentComputed)
        {
            return true;
        }

        // lazy compute geometry
        if (!computeGeometry())
        {
            return false;
        }

        // currently we only support world extents for non-time sampled geometry
        if (m_data->m_hasTimeSamples)
        {
            return false;
        }

        // to correctly transform the extents we need to transform the 8 corners of the box and then find the new
        // extents from those points
        GfVec3f minExtent = GfVec3f(std::numeric_limits<float>::infinity());
        GfVec3f maxExtent = GfVec3f(-std::numeric_limits<float>::infinity());
        for (size_t i = 0; i < 2; ++i)
        {
            for (size_t j = 0; j < 2; ++j)
            {
                for (size_t k = 0; k < 2; ++k)
                {
                    const GfVec3f point(m_data->m_extent[i][0], m_data->m_extent[j][1], m_data->m_extent[k][2]);
                    _updateExtent(GfVec3f(m_localToWorldTransform.Transform(point)), minExtent, maxExtent);
                }
            }
        }

        // transform local extent into it into worldspace
        m_worldExtent = { minExtent, maxExtent };
        m_extentComputed = true;

        return true;
    }

    size_t getRtxDuplicateHash() const
    {
        size_t hash = 0;
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_faceVertexCounts));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_faceVertexIndices));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_points));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_holeIndices));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_cornerIndices));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_cornerSharpnesses));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_creaseIndices));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_creaseLengths));
        hash = TfHash::Combine(hash, VtHashValue(m_data->m_creaseSharpnesses));
        for (const auto& primvarPair : m_data->m_primvars)
        {
            const PrimvarData& primvar = primvarPair.second;
            // note: we avoid the overhead of considering primvar time-samples, this can be checked if the we get a hash
            //       match using just the default value
            hash = TfHash::Combine(hash, VtHashValue(primvar.m_attrData.m_defaultValue));
            hash = TfHash::Combine(hash, VtHashValue(primvar.m_interpolation));
            hash = TfHash::Combine(hash, VtHashValue(primvar.m_indices));
        }

        // note: materials do NOT affect the RTX duplication hash

        return hash;
    }

    float getExtentMaxSize() const
    {
        // extract scale from the world xform
        GfVec3d scale;
        _extractScaleAbs(m_localToWorldTransform, scale);

        // compute max size, use local extent but scale it to world space
        const GfVec3d extentSizes =
            GfRange3d(GfCompMult(m_data->m_extent[0], scale), GfCompMult(m_data->m_extent[1], scale)).GetSize();
        return static_cast<float>(std::max(extentSizes[0], std::max(extentSizes[1], extentSizes[2])));
    }

    float getExtentVolume() const
    {
        // extract scale from the world xform
        GfVec3d scale;
        _extractScaleAbs(m_localToWorldTransform, scale);

        // compute volume, use local extent but scale it to world space (translation and rotation are counterproductive
        // for volume)
        return static_cast<float>(
            GfBBox3d(GfRange3d(GfCompMult(m_data->m_extent[0], scale), GfCompMult(m_data->m_extent[1], scale))).GetVolume());
    }

    float getGeometryVolume() const
    {
        float volume = 0.0f;

        // is there valid geometry data?
        if (!m_data->m_faceVertexCounts.empty() && !m_data->m_faceVertexIndices.empty() && !m_data->m_points.empty())
        {
            // extract scale from the world xform
            GfVec3d scale;
            _extractScaleAbs(m_localToWorldTransform, scale);

            // iterate through the faces of the mesh and compute the signed volume sum of each face
            float volumeSum = 0.0f;
            size_t indexOffset = 0;
            for (int count : m_data->m_faceVertexCounts)
            {
                // faces with less than 3 vertices are invalid
                if (count < 3)
                {
                    indexOffset += count;
                    continue;
                }

                // always compute the first 3 points as a tetrahedron
                const GfVec3d point0 =
                    GfCompMult(GfVec3d(m_data->m_points[m_data->m_faceVertexIndices[indexOffset + 0]]), scale);
                const GfVec3d point1 =
                    GfCompMult(GfVec3d(m_data->m_points[m_data->m_faceVertexIndices[indexOffset + 1]]), scale);
                const GfVec3d point2 =
                    GfCompMult(GfVec3d(m_data->m_points[m_data->m_faceVertexIndices[indexOffset + 2]]), scale);
                volumeSum += _signedTetrahedronVolume(point0, point1, point2);

                // divide quad into 2 triangles for faces with 4 or more sides
                if (count > 3)
                {
                    const GfVec3d point3 =
                        GfCompMult(GfVec3d(m_data->m_points[m_data->m_faceVertexIndices[indexOffset + 3]]), scale);
                    volumeSum += _signedTetrahedronVolume(point1, point2, point3);
                }

                // TODO: do we ever need to handle faces with more than 4 sides?

                indexOffset += count;
            }

            // volume is absolute value of the signed sum
            volume = fabs(volumeSum);
        }

        return volume;
    }

    void bindMaterial(const SdfPath& materialPath, const UsdMetadataValueMap& metadata)
    {
        // already has a material binding api schema?
        if (!m_apiSchemas.HasItem(_tokens->materialBindingAPI))
        {
            if (m_apiSchemas.IsExplicit())
            {
                TfTokenVector schemas = m_apiSchemas.GetExplicitItems();
                schemas.push_back(_tokens->materialBindingAPI);
                m_apiSchemas.SetExplicitItems(schemas);
            }
            else
            {
                TfTokenVector schemas = m_apiSchemas.GetPrependedItems();
                schemas.push_back(_tokens->materialBindingAPI);
                m_apiSchemas.SetPrependedItems(schemas);
            }
        }

        // already has a material binding relationship?
        auto findRelationship = m_relationships.find(UsdShadeTokens->materialBinding);
        if (findRelationship != m_relationships.end())
        {
            findRelationship->second.m_targets.clear();
            findRelationship->second.m_targets.push_back(materialPath);
            findRelationship->second.m_metadata = metadata;
        }
        else
        {
            RelationshipData materialBinding(UsdShadeTokens->materialBinding);
            materialBinding.m_targets.push_back(materialPath);
            materialBinding.m_metadata = metadata;
            m_relationships.insert(std::make_pair(UsdShadeTokens->materialBinding, materialBinding));
        }

        // set the bound material on the VirtualMesh so we can determine it when clustering
        m_explicitMaterialBinding = true;
        m_materialPath = materialPath;
    }

    void unbindMaterial()
    {
        // update schemas if the material binding api schema exists
        if (m_apiSchemas.HasItem(_tokens->materialBindingAPI))
        {
            // the material binding api schema exists, but it could be in any of the list ops, so we need to check all
            TfTokenVector newList;
            if (_removeMaterialBindingSchema(m_apiSchemas.GetExplicitItems(), newList))
            {
                m_apiSchemas.SetExplicitItems(newList);
            }

            newList.clear();
            if (_removeMaterialBindingSchema(m_apiSchemas.GetPrependedItems(), newList))
            {
                m_apiSchemas.SetPrependedItems(newList);
            }

            newList.clear();
            if (_removeMaterialBindingSchema(m_apiSchemas.GetAppendedItems(), newList))
            {
                m_apiSchemas.SetAppendedItems(newList);
            }

            newList.clear();
            if (_removeMaterialBindingSchema(m_apiSchemas.GetDeletedItems(), newList))
            {
                m_apiSchemas.SetDeletedItems(newList);
            }
        }

        // remove the material binding relationship if it exists
        auto findMaterialBinding = m_relationships.find(UsdShadeTokens->materialBinding);
        if (findMaterialBinding != m_relationships.end())
        {
            m_relationships.erase(findMaterialBinding);
        }

        m_explicitMaterialBinding = true;
        m_materialPath = SdfPath();
    }

    void replaceMaterialWithDisplayColor(const ColorValue& baseColor)
    {
        // create overrides for the display color
        // TODO: Support per-face material assignments by using uniform interpolation here.
        VtArray<GfVec3f> displayColorArray = { baseColor.color };
        VtValue displayColorValue = VtValue::Take(displayColorArray);
        m_primvarOverrides[UsdGeomTokens->primvarsDisplayColor] = PrimvarData(UsdGeomTokens->primvarsDisplayColor,
                                                                              SdfValueTypeNames->Color3fArray,
                                                                              std::move(displayColorValue),
                                                                              UsdGeomTokens->constant);

        // Override opacity if there is a non-opaque value
        if (baseColor.opacity != 1.0)
        {
            VtArray<float> displayOpacityArray = { baseColor.opacity };
            VtValue displayOpacityValue = VtValue::Take(displayOpacityArray);
            m_primvarOverrides[UsdGeomTokens->primvarsDisplayOpacity] = PrimvarData(UsdGeomTokens->primvarsDisplayOpacity,
                                                                                    SdfValueTypeNames->FloatArray,
                                                                                    std::move(displayOpacityValue),
                                                                                    UsdGeomTokens->constant);
        }

        // unbind any material
        unbindMaterial();
    }

    void useSpatialDebug()
    {
        static constexpr float s_goldenRatio = 0.618033988749895f;

        // Generate a color based on the cluster number
        float hue = s_goldenRatio * (float)m_spatialClusterId;
        hue = fmodf(hue, 1.0);
        GfVec3f color = _hsvToRgb(hue, 0.99f, 0.95f);

        // create overrides for the display color
        VtArray<GfVec3f> displayColorArray = { color };
        VtValue displayColorValue = VtValue::Take(displayColorArray);
        m_primvarOverrides[UsdGeomTokens->primvarsDisplayColor] = PrimvarData(UsdGeomTokens->primvarsDisplayColor,
                                                                              SdfValueTypeNames->Color3fArray,
                                                                              std::move(displayColorValue),
                                                                              UsdGeomTokens->constant);
        // unbind any material
        unbindMaterial();
    }

    Impl* newModifiedCopy(const GfMatrix4d& xform, VtVec3fArray&& points) const
    {
        Impl* copy = new Impl(*this);
        copy->m_id = getNextId();
        copy->m_localToWorldTransform = xform;

        // make a copy of the attributes so we can modify them with the new xform
        AttributeDataMap attributes = m_data->m_attributes;
        attributes[_tokens->xformOpTransform] = AttributeData(_tokens->xformOpTransform,
                                                              SdfValueTypeNames->Matrix4d,
                                                              SdfVariabilityVarying,
                                                              false,
                                                              VtValue(xform));
        const VtArray<TfToken> order = { _tokens->xformOpTransform };
        attributes[UsdGeomTokens->xformOpOrder] = AttributeData(UsdGeomTokens->xformOpOrder,
                                                                SdfValueTypeNames->TokenArray,
                                                                SdfVariabilityUniform,
                                                                false,
                                                                VtValue(order));
        // remove any non-matrix xform ops from the attributes
        for (auto attr = attributes.cbegin(); attr != attributes.cend();)
        {
            if (attr->first != _tokens->xformOpTransform && TfStringStartsWith(attr->first.GetString(), "xformOp:"))
            {
                attributes.erase(attr++);
            }
            else
            {
                ++attr;
            }
        }

        // copy the rest of the shared data (other than points)
        SdfPath sourcePath = m_data->m_sourcePath;
        UsdMetadataValueMap metadata = m_data->m_metadata;
        PrimvarDataMap primvars = m_data->m_primvars;
        bool hasTimeSamples = m_data->m_hasTimeSamples;
        VtIntArray faceVertexCounts = m_data->m_faceVertexCounts;
        VtIntArray faceVertexIndices = m_data->m_faceVertexIndices;
        VtIntArray holeIndices = m_data->m_holeIndices;
        VtIntArray cornerIndices = m_data->m_cornerIndices;
        VtFloatArray cornerSharpnesses = m_data->m_cornerSharpnesses;
        VtIntArray creaseIndices = m_data->m_creaseIndices;
        VtIntArray creaseLengths = m_data->m_creaseLengths;
        VtFloatArray creaseSharpnesses = m_data->m_creaseSharpnesses;
        VtVec3fArray extent = m_data->m_extent;
        VtTokenArray jointNames = m_data->m_jointNames;
        VtIntArray jointIndices = m_data->m_jointIndices;
        int jointIndicesElementSize = m_data->m_jointIndicesElementSize;
        VtFloatArray jointWeights = m_data->m_jointWeights;
        int jointWeightsElementSize = m_data->m_jointWeightsElementSize;

        // update the shared data on the copy
        copy->m_data.reset(new SharedData(sourcePath,
                                          std::move(metadata),
                                          std::move(attributes),
                                          std::move(primvars),
                                          hasTimeSamples,
                                          std::move(faceVertexCounts),
                                          std::move(faceVertexIndices),
                                          std::move(points),
                                          std::move(holeIndices),
                                          std::move(cornerIndices),
                                          std::move(cornerSharpnesses),
                                          std::move(creaseIndices),
                                          std::move(creaseLengths),
                                          std::move(creaseSharpnesses),
                                          std::move(extent),
                                          std::move(jointNames),
                                          std::move(jointIndices),
                                          jointIndicesElementSize,
                                          std::move(jointWeights),
                                          jointWeightsElementSize));
        copy->m_data->m_materialSubsets = m_data->m_materialSubsets;
        copy->m_data->m_unassignedSubset = m_data->m_unassignedSubset;

        return copy;
    }

    Impl* newSubset(const std::vector<int>&& faceVertexCountIndices)
    {
        // if the faceVertexCountIndices are empty or the same size of the faceVertexCounts, then this is not a
        // subset. Just return this VirtualMesh instead as a performance optimization
        if (faceVertexCountIndices.empty() || faceVertexCountIndices.size() == m_data->m_faceVertexCounts.size())
        {
            // remember to increase ref count
            m_refCount++;
            return this;
        }

        // if geometry is not computed and this is subset or superset we need to compute it first
        if (!m_geometryComputed && (m_isSubset || m_isSuperset))
        {
            computeGeometry();
        }

        // compute offsets and blacklist attributes (if needed)
        m_data->precomputeSubsetCache();

        // subset constructor
        return new Impl(*this, std::move(faceVertexCountIndices));
    }

    void addSupersetChild(VirtualMesh& child)
    {
        // currently can't make a superset if geometry has already been computed - could support if its necessary later
        if (m_geometryComputed)
        {
            SO_LOG_ERROR("Can't make a VirtualMesh superset after geometry has been computed");
            return;
        }

        // currently can't make a superset from a subset - could support if its necessary later
        if (m_isSubset)
        {
            SO_LOG_ERROR("Can't create a VirtualMesh superset from a subset");
            return;
        }

        // compute the child's geometry
        child.computeGeometry();
        m_isSuperset = true;

        // if the child VirtualMesh contains material subsets then we need to create a child for each subset so that
        // subset merging can take place correctly
        std::map<SdfPath, VtIntArray> materialSubsets = child.getMaterialSubsets();
        if (!materialSubsets.empty())
        {
            for (const auto& materialSubset : materialSubsets)
            {
                std::vector<int> faceVertexCountIndices(materialSubset.second.begin(), materialSubset.second.end());
                VirtualMesh subsetChild = child.newSubset(std::move(faceVertexCountIndices));
                subsetChild.bindMaterial(materialSubset.first);
                subsetChild.computeGeometry();
                subsetChild.validateAndComputeExtent();
                m_supersetChildren.push_back(subsetChild);
                m_supersetDataVolume += m_supersetChildren.back().getPoints().size();
            }

            // check if we this mesh should be using an unassigned subset for one of its children
            // no unassigned subset material yet, use the one from this child
            if (m_unassignedSubsetMaterial.IsEmpty() && !child.pImpl->m_data->m_unassignedSubset.IsEmpty())
            {
                m_hasUnassignedSubset = true;
                m_unassignedSubsetMaterial = child.pImpl->m_data->m_unassignedSubset;
            }
            // multiple children with unassigned subset materials that don't match - don't use one in the final result
            else if (m_unassignedSubsetMaterial != child.pImpl->m_data->m_unassignedSubset)
            {
                m_hasUnassignedSubset = false;
            }
        }
        // otherwise just add this VirtualMesh as a child
        else
        {
            m_supersetChildren.push_back(child);
            m_supersetDataVolume += m_supersetChildren.back().getPoints().size();
        }
    }

    void createInLayer(const UsdStageWeakPtr& stage, SdfLayerHandle& layer)
    {
        // is there a valid destination path?
        if (m_destinationParentPath.IsEmpty() || !m_destinationParentPath.IsAbsolutePath() || m_destinationName.IsEmpty())
        {
            SO_LOG_ERROR("Cannot create VirtualMesh in layer: invalid destination path: %s",
                         m_destinationParentPath.GetAsString().c_str());
            return;
        }

        // geometry must be computed first
        computeGeometry();

        // build the destination path
        SdfPath destinationPath = m_destinationParentPath.AppendChild(m_destinationName);

        // Build a list of the parent prim paths that do not already exist on the stage so that we can create them with
        // appropriate specifier and type name later. We cannot create them during iteration as that will create parent
        // prims with an over specifier.
        SdfPathVector parentPaths;
        for (const auto& parentPath : destinationPath.GetParentPath().GetAncestorsRange())
        {
            // break on the first prim path that exists on the stage
            if (stage->GetPrimAtPath(parentPath))
            {
                break;
            }
            parentPaths.push_back(parentPath);
        }

        // Define missing parent prims on the stage via Sdf so that parent specifiers are unchanged
        for (const auto& parentPath : parentPaths)
        {
            SdfPrimSpecHandle parentPrimSpec = SdfCreatePrimInLayer(layer, parentPath);
            if (!parentPrimSpec)
            {
                SO_LOG_ERROR("Failed to create VirtualMesh parent prim in layer: %s", parentPath.GetAsString().c_str());
                return;
            }
            parentPrimSpec->SetTypeName(_tokens->xform);
            parentPrimSpec->SetSpecifier(SdfSpecifierDef);
        }

        SdfPrimSpecHandle primSpec = SdfCreatePrimInLayer(layer, destinationPath);
        if (!primSpec)
        {
            SO_LOG_ERROR("Failed to create VirtualMesh in layer: %s", destinationPath.GetAsString().c_str());
            return;
        }

        // set metadata
        primSpec->SetSpecifier(m_specifier);
        primSpec->SetTypeName(m_typeName);

        TfTokenVector appliedSchemas;
        getAppliedSchemas(appliedSchemas, true);
        if (!appliedSchemas.empty())
        {
            primSpec->SetInfo(_tokens->apiSchemas, VtValue(m_apiSchemas));
        }
        for (const auto& [key, value] : m_data->m_metadata)
        {
            primSpec->SetInfo(key, value);
        }

        // TODO: for now we just pass an empty time-samples map - but in future we should support time-samples
        SdfTimeSampleMap timeSamples;
        // write geometry data
        createGeometryAttr(primSpec,
                           UsdGeomTokens->faceVertexCounts,
                           SdfValueTypeNames->IntArray,
                           VtValue(m_data->m_faceVertexCounts),
                           timeSamples);
        createGeometryAttr(primSpec,
                           UsdGeomTokens->faceVertexIndices,
                           SdfValueTypeNames->IntArray,
                           VtValue(m_data->m_faceVertexIndices),
                           timeSamples);
        createGeometryAttr(primSpec,
                           UsdGeomTokens->points,
                           SdfValueTypeNames->Point3fArray,
                           VtValue(m_data->m_points),
                           timeSamples);
        if (!m_data->m_holeIndices.empty())
        {
            createGeometryAttr(primSpec,
                               UsdGeomTokens->holeIndices,
                               SdfValueTypeNames->IntArray,
                               VtValue(m_data->m_holeIndices),
                               timeSamples);
        }
        if (!m_data->m_cornerIndices.empty())
        {
            createGeometryAttr(primSpec,
                               UsdGeomTokens->cornerIndices,
                               SdfValueTypeNames->IntArray,
                               VtValue(m_data->m_cornerIndices),
                               timeSamples);
        }
        if (!m_data->m_cornerSharpnesses.empty())
        {
            createGeometryAttr(primSpec,
                               UsdGeomTokens->cornerSharpnesses,
                               SdfValueTypeNames->FloatArray,
                               VtValue(m_data->m_cornerSharpnesses),
                               timeSamples);
        }
        if (!m_data->m_creaseIndices.empty())
        {
            createGeometryAttr(primSpec,
                               UsdGeomTokens->creaseIndices,
                               SdfValueTypeNames->IntArray,
                               VtValue(m_data->m_creaseIndices),
                               timeSamples);
        }
        if (!m_data->m_creaseLengths.empty())
        {
            createGeometryAttr(primSpec,
                               UsdGeomTokens->creaseLengths,
                               SdfValueTypeNames->IntArray,
                               VtValue(m_data->m_creaseLengths),
                               timeSamples);
        }
        if (!m_data->m_creaseSharpnesses.empty())
        {
            createGeometryAttr(primSpec,
                               UsdGeomTokens->creaseSharpnesses,
                               SdfValueTypeNames->FloatArray,
                               VtValue(m_data->m_creaseSharpnesses),
                               timeSamples);
        }

        // write the extent attribute
        if (m_data->m_extent.size() >= 2)
        {
            SdfAttributeSpecHandle extentSpec =
                SdfAttributeSpec::New(primSpec, UsdGeomTokens->extent, SdfValueTypeNames->Float3Array);
            if (extentSpec)
            {
                extentSpec->SetDefaultValue(VtValue(m_data->m_extent));
            }
            else
            {
                SO_LOG_WARN("Failed to create extent attribute for VirtualMesh in layer: %s",
                            destinationPath.GetAsString().c_str());
            }
        }

        // write skeleton values (if needed)
        if (isSkeleton())
        {
            // write the joints attribute
            if (!m_data->m_jointNames.empty())
            {
                SdfAttributeSpecHandle jointsAttrSpec = SdfAttributeSpec::New(primSpec,
                                                                              UsdSkelTokens->joints,
                                                                              SdfValueTypeNames->TokenArray,
                                                                              SdfVariabilityUniform,
                                                                              false);
                if (jointsAttrSpec)
                {
                    jointsAttrSpec->SetDefaultValue(VtValue(m_data->m_jointNames));
                }
            }

            // write the joint indices primvar
            SdfAttributeSpecHandle jointIndicesSpec = SdfAttributeSpec::New(primSpec,
                                                                            UsdSkelTokens->primvarsSkelJointIndices,
                                                                            SdfValueTypeNames->IntArray,
                                                                            SdfVariabilityVarying,
                                                                            false);
            if (jointIndicesSpec)
            {
                jointIndicesSpec->SetInfo(UsdGeomTokens->elementSize, VtValue(m_data->m_jointIndicesElementSize));
                jointIndicesSpec->SetInfo(UsdGeomTokens->interpolation, VtValue(UsdGeomTokens->vertex));
                jointIndicesSpec->SetDefaultValue(VtValue(m_data->m_jointIndices));
            }

            // write the joint weights primvar
            SdfAttributeSpecHandle jointWeightsSpec = SdfAttributeSpec::New(primSpec,
                                                                            UsdSkelTokens->primvarsSkelJointWeights,
                                                                            SdfValueTypeNames->FloatArray,
                                                                            SdfVariabilityVarying,
                                                                            false);
            if (jointWeightsSpec)
            {
                jointWeightsSpec->SetInfo(UsdGeomTokens->elementSize, VtValue(m_data->m_jointWeightsElementSize));
                jointWeightsSpec->SetInfo(UsdGeomTokens->interpolation, VtValue(UsdGeomTokens->vertex));
                jointWeightsSpec->SetDefaultValue(VtValue(m_data->m_jointWeights));
            }
        }

        // write any material geomsubets
        if (!m_data->m_materialSubsets.empty())
        {
            // write subset family attribute if it doesn't exist
            if (m_data->m_attributes.find(_tokens->subsetFamily) == m_data->m_attributes.end())
            {
                SdfAttributeSpecHandle subsetFamilySpec = SdfAttributeSpec::New(primSpec,
                                                                                _tokens->subsetFamily,
                                                                                SdfValueTypeNames->Token,
                                                                                SdfVariabilityUniform,
                                                                                false);
                if (subsetFamilySpec)
                {
                    subsetFamilySpec->SetDefaultValue(VtValue(_tokens->nonOverlapping));
                }
            }

            // create the unique paths for the subsets and an ordered list of the associated indices
            // note: technically iterating through std::map *should* give the same order, but this feels error prone
            //       especially if the map were to be updated to an unordered_map at some stage, so we explicitly retain
            //       the order
            TfTokenVector subsetNames;
            std::vector<std::pair<SdfPath, VtIntArray>> orderedSubsets;
            subsetNames.reserve(m_data->m_materialSubsets.size());
            orderedSubsets.reserve(m_data->m_materialSubsets.size());
            for (const auto& subset : m_data->m_materialSubsets)
            {
                subsetNames.push_back(subset.first.GetNameToken());
                orderedSubsets.push_back(subset);
            }
            SdfPathVector uniqueSubsetPaths = _getUniqueChildPaths(stage, destinationPath, subsetNames);

            for (size_t i = 0; i < orderedSubsets.size(); ++i)
            {
                const auto& subset = orderedSubsets[i];

                // sanity check
                if (i >= uniqueSubsetPaths.size())
                {
                    // LCOV_EXCL_START
                    SO_LOG_ERROR("Developer error: failed to create unique subset path for %s at %s",
                                 subset.first.GetAsString().c_str(),
                                 destinationPath.GetAsString().c_str());
                    continue;
                    // LCOV_EXCL_STOP
                }
                const SdfPath& subsetPath = uniqueSubsetPaths[i];

                SdfPrimSpecHandle subsetSpec = SdfCreatePrimInLayer(layer, subsetPath);
                if (!subsetSpec)
                {
                    SO_LOG_WARN("Failed to create material GeomSubset in layer: %s", subsetPath.GetAsString().c_str());
                    continue;
                }

                // set type name and specifier
                subsetSpec->SetSpecifier(SdfSpecifierDef);
                subsetSpec->SetTypeName(_tokens->geomSubset);

                // material subsets always use the MaterialBindAPI
                SdfListOp<TfToken> materialBindSchemasOp;
                materialBindSchemasOp.SetPrependedItems({ _tokens->materialBindingAPI });
                subsetSpec->SetInfo(_tokens->apiSchemas, VtValue(materialBindSchemasOp));

                // create attributes
                SdfAttributeSpecHandle elementTypeSpec = SdfAttributeSpec::New(subsetSpec,
                                                                               _tokens->elementType,
                                                                               SdfValueTypeNames->Token,
                                                                               SdfVariabilityUniform,
                                                                               false);
                if (elementTypeSpec)
                {
                    elementTypeSpec->SetDefaultValue(VtValue(_tokens->face));
                }
                SdfAttributeSpecHandle familyNameSpec = SdfAttributeSpec::New(subsetSpec,
                                                                              _tokens->familyName,
                                                                              SdfValueTypeNames->Token,
                                                                              SdfVariabilityUniform,
                                                                              false);
                if (familyNameSpec)
                {
                    familyNameSpec->SetDefaultValue(VtValue(_tokens->materialBind));
                }
                SdfAttributeSpecHandle indicesSpec = SdfAttributeSpec::New(subsetSpec,
                                                                           _tokens->indices,
                                                                           SdfValueTypeNames->IntArray,
                                                                           SdfVariabilityVarying,
                                                                           false);
                if (indicesSpec)
                {
                    indicesSpec->SetDefaultValue(VtValue(subset.second));
                }

                // create binding relationship
                SdfRelationshipSpecHandle bindingSpec =
                    SdfRelationshipSpec::New(subsetSpec, UsdShadeTokens->materialBinding, true);
                if (!bindingSpec)
                {
                    SO_LOG_WARN("Failed to material binding relationship %s for material GeomSubset in layer: %s",
                                subsetPath.GetAsString().c_str());
                    continue;
                }
                bindingSpec->GetTargetPathList().Add(subset.first);
            }
        }

        // build the list of primvars to write by combining overrides and base primvars
        PrimvarDataMap writePrimvars;
        for (const auto& primvar : m_primvarOverrides)
        {
            writePrimvars.insert(primvar);
        }
        for (const auto& primvar : m_data->m_primvars)
        {
            writePrimvars.insert(primvar);
        }

        // write primvars
        for (auto& [primvarName, primvarData] : writePrimvars)
        {
            // create the primvar attribute
            SdfAttributeSpecHandle primvarSpec = SdfAttributeSpec::New(primSpec,
                                                                       primvarName,
                                                                       primvarData.m_attrData.m_typeName,
                                                                       primvarData.m_attrData.m_variability,
                                                                       primvarData.m_attrData.m_custom);
            if (!primvarSpec)
            {
                SO_LOG_WARN("Failed to create primvar %s for VirtualMesh in layer: %s",
                            primvarName,
                            destinationPath.GetAsString().c_str());
                continue;
            }

            // metadata
            for (const auto& [key, value] : primvarData.m_attrData.m_metadata)
            {
                primvarSpec->SetInfo(key, value);
            }
            // interpolation
            primvarSpec->SetInfo(UsdGeomTokens->interpolation, VtValue(primvarData.m_interpolation));
            // default value
            if (!primvarData.m_attrData.m_defaultValue.IsEmpty())
            {
                primvarSpec->SetDefaultValue(primvarData.m_attrData.m_defaultValue);
            }
            // TODO: support time samples?

            // create the indices attribute
            if (!primvarData.m_indices.empty())
            {
                const std::string indicesName = primvarName.GetString() + ":indices";
                SdfAttributeSpecHandle indicesSpec = SdfAttributeSpec::New(primSpec,
                                                                           indicesName,
                                                                           SdfValueTypeNames->IntArray,
                                                                           SdfVariabilityVarying,
                                                                           primvarData.m_attrData.m_custom);
                if (indicesSpec)
                {
                    // default value
                    indicesSpec->SetDefaultValue(VtValue(primvarData.m_indices));
                    // TODO: support time samples?
                }
                else
                {
                    SO_LOG_WARN("Failed to create indices for primvar %s for VirtualMesh in layer: %s",
                                indicesName,
                                destinationPath.GetAsString().c_str());
                }
            }
        }

        // build the list of attributres to write by combining overrides and base attributes
        AttributeDataMap writeAttributes;
        for (const auto& attribute : m_attributeOverrides)
        {
            writeAttributes.insert(attribute);
        }
        for (const auto& attribute : m_data->m_attributes)
        {
            writeAttributes.insert(attribute);
        }

        // write arbitrary attributes
        for (auto& [attrName, attrData] : writeAttributes)
        {
            SdfAttributeSpecHandle attrSpec =
                SdfAttributeSpec::New(primSpec, attrName, attrData.m_typeName, attrData.m_variability, attrData.m_custom);
            if (!attrSpec)
            {
                SO_LOG_WARN("Failed to create attribute %s for VirtualMesh in layer: %s",
                            attrName.GetString().c_str(),
                            destinationPath.GetAsString().c_str());
                continue;
            }

            // metadata
            for (const auto& [key, value] : attrData.m_metadata)
            {
                attrSpec->SetInfo(key, value);
            }
            // default value
            if (!attrData.m_defaultValue.IsEmpty())
            {
                attrSpec->SetDefaultValue(attrData.m_defaultValue);
            }
            // time samples
            if (!attrData.m_timeSamples.empty())
            {
                attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(attrData.m_timeSamples));
            }
        }

        // write relationships
        for (auto& [relName, relData] : m_relationships)
        {
            SdfRelationshipSpecHandle relSpec = SdfRelationshipSpec::New(primSpec, relName);
            if (!relSpec)
            {
                SO_LOG_WARN("Failed to create relationship %s for VirtualMesh in layer: %s",
                            relName,
                            destinationPath.GetAsString().c_str());
                continue;
            }

            // targets
            SdfTargetsProxy targets = relSpec->GetTargetPathList();
            for (const SdfPath& target : relData.m_targets)
            {
                targets.Add(target);
            }
            // metadata
            for (const auto& [key, value] : relData.m_metadata)
            {
                relSpec->SetInfo(key, value);
            }
        }
    }

private:
    // returns a new unique id each time this function is called.
    static size_t getNextId()
    {
        static std::atomic_size_t next_id = 0;
        return next_id++;
    }

    // Used to compute the geometry data if this is a subset
    void computeSubsetGeometry()
    {
        // compute offsets and blacklist attributes (if needed)
        m_data->precomputeSubsetCache();

        // faster access to geometry data
        auto faceVertexCountsRaw = m_data->m_faceVertexCounts.cdata();
        auto faceVertexIndicesRaw = m_data->m_faceVertexIndices.cdata();
        auto pointsRaw = m_data->m_points.cdata();
        auto holeIndicesRaw = m_data->m_holeIndices.cdata();
        auto cornerIndicesRaw = m_data->m_cornerIndices.cdata();
        auto cornerSharpnessesRaw = m_data->m_cornerSharpnesses.cdata();
        auto creaseIndicesRaw = m_data->m_creaseIndices.cdata();
        auto creaseLengthsRaw = m_data->m_creaseLengths.cdata();
        auto creaseSharpnessesRaw = m_data->m_creaseSharpnesses.cdata();

        // geometry data
        VtIntArray faceVertexCounts;
        faceVertexCounts.reserve(m_faceVertexCountIndices.size());
        VtVec3fArray points;
        VtIntArray holeIndices;
        VtIntArray cornerIndices;
        VtFloatArray cornerSharpnesses;
        VtIntArray creaseIndices;
        VtIntArray creaseLengths;
        VtFloatArray creaseSharpnesses;

        // do a first pass to correctly reserve indices size
        size_t vertexIndicesSize = 0;
        for (int faceIndex : m_faceVertexCountIndices)
        {
            // valid face index?
            if (faceIndex < 0 || faceIndex >= static_cast<int>(m_data->m_faceVertexCounts.size()))
            {
                continue;
            }
            vertexIndicesSize += faceVertexCountsRaw[faceIndex];
        }

        // invalid geometry?
        if (vertexIndicesSize == 0)
        {
            // New empty data
            m_data = s_emptySharedData;
            return;
        }

        IntPool::Vec faceVertexIndices = s_intPool.allocate(vertexIndicesSize);
        faceVertexIndices.get().resize(vertexIndicesSize);

        // TODO: can probably use an optimization structure here too like the VertexIndexFastMap? - need a scene with
        //       lots of subdiv data to test against
        // map of from the original vertex indices of points in the subset geometry to their new indices in the subset
        std::unordered_map<int, int> subsetIndicesMap;

        // convert hole indices into a set so we can remap them to the new face indices
        std::unordered_set<int> holeIndicesSet;
        for (size_t i = 0; i < m_data->m_holeIndices.size(); ++i)
        {
            holeIndicesSet.insert(holeIndicesRaw[i]);
        }

        // whether we actually need to do any work for subdivision data
        const bool hasSubdivData = !m_data->m_cornerIndices.empty() || !m_data->m_creaseIndices.empty();

        // iterate through the face vertex count indices which are used to slice the mesh and collect the vertices that
        // are used in the new mesh
        int faceOffset = 0;
        size_t vertexIndex = 0;
        for (int faceIndex : m_faceVertexCountIndices)
        {
            // valid face index?
            if (faceIndex < 0 || faceIndex >= static_cast<int>(m_data->m_faceVertexCounts.size()))
            {
                continue;
            }

            // is this index a hole?
            if (!holeIndicesSet.empty())
            {
                if (holeIndicesSet.find(faceIndex) != holeIndicesSet.end())
                {
                    holeIndices.push_back(faceOffset);
                }
            }

            // get the number of vertices in the face and the offset into the faceVertexIndices array
            int faceCount = faceVertexCountsRaw[faceIndex];
            faceVertexCounts.push_back(faceCount);
            int offset = (*m_data->m_faceVertexIndicesOffsets)[faceIndex];

            // iterate through the offsets of vertex indices for the face
            for (int i = 0; i < faceCount; ++i)
            {
                faceVertexIndices.get()[vertexIndex++] = faceVertexIndicesRaw[offset + i];
            }

            ++faceOffset;
        }

        // sort the face vertices so we can insert the points in the correct order to retain winding order and count the
        // number of points so we can resize to the correct memory size
        IntPool::Vec sortedIndices = s_intPool.allocate(faceVertexIndices.get().size());
        sortedIndices.get().insert(sortedIndices.get().end(),
                                   faceVertexIndices.get().begin(),
                                   faceVertexIndices.get().end());
        std::sort(sortedIndices.get().begin(), sortedIndices.get().end());
        const size_t indexRange = static_cast<size_t>(sortedIndices.get().back() - sortedIndices.get().front() + 1);
        VertexIndexFastMap vertexIndicesMap(indexRange, static_cast<size_t>(sortedIndices.get().front()));
        IntPool::Vec orderedIndices = s_intPool.allocate(indexRange);
        size_t numPoints = 0;
        for (int vertexIndex : sortedIndices.get())
        {
            int& mapLookup = vertexIndicesMap[vertexIndex];
            if (mapLookup != -1)
            {
                continue;
            }
            mapLookup = static_cast<int>(numPoints);
            numPoints++;
            orderedIndices.get().push_back(vertexIndex);
        }

        // allocate points and compute extents at the same time (note: its significantly faster to compute extents while
        // iterating the points compared to calling UsdGeomPointBased::ComputeExtent)
        GfVec3f minExtent = GfVec3f(std::numeric_limits<float>::infinity());
        GfVec3f maxExtent = GfVec3f(-std::numeric_limits<float>::infinity());
        points.resize(numPoints);
        for (size_t i = 0; i < numPoints; ++i)
        {
            // add to points
            int index = orderedIndices.get()[i];
            const GfVec3f& point = pointsRaw[index];
            points[i] = point;
            // keep the extents updated
            _updateExtent(point, minExtent, maxExtent);

            // record the original index of this point if we're having to process subdivision data
            if (hasSubdivData)
            {
                subsetIndicesMap.insert(std::make_pair(index, static_cast<int>(i)));
            }
        }
        // set extent (unless there are no points)
        VtVec3fArray extent;
        if (numPoints > 0)
        {
            extent = { minExtent, maxExtent };
        }

        // compute subdivison data?
        if (hasSubdivData)
        {
            // TODO: in future we should test whether its faster to do a first iteration to calculate how many
            //       corner/crease indices are in the subset and reserve the array size before adding the new corner
            //       indices

            // compute the corner indices and sharpnesses in the subset data
            for (size_t i = 0; i < m_data->m_cornerIndices.size(); ++i)
            {
                const int cornerIndex = cornerIndicesRaw[i];
                auto findVertexIndex = subsetIndicesMap.find(cornerIndex);
                if (findVertexIndex != subsetIndicesMap.end())
                {
                    cornerIndices.push_back(findVertexIndex->second);
                    if (i < m_data->m_cornerSharpnesses.size())
                    {
                        cornerSharpnesses.push_back(cornerSharpnessesRaw[i]);
                    }
                }
            }

            // compute the crease indices, lengths, and sharpnesses in the subset data
            int creaseOffset = 0;
            for (size_t i = 0; i < m_data->m_creaseLengths.size(); ++i)
            {
                int creaseLength = creaseLengthsRaw[i];
                int newCreaseLength = 0;
                for (int j = 0; j < creaseLength; ++j)
                {
                    // sanity check
                    if (creaseOffset < static_cast<int>(m_data->m_creaseIndices.size()))
                    {
                        const int creaseIndex = creaseIndicesRaw[creaseOffset];
                        auto findVertexIndex = subsetIndicesMap.find(creaseIndex);
                        if (findVertexIndex != subsetIndicesMap.end())
                        {
                            ++newCreaseLength;
                            creaseIndices.push_back(findVertexIndex->second);
                        }
                    }
                    ++creaseOffset;
                }
                // update crease length data
                if (newCreaseLength > 0)
                {
                    creaseLengths.push_back(newCreaseLength);
                    if (i < m_data->m_creaseSharpnesses.size())
                    {
                        creaseSharpnesses.push_back(creaseSharpnessesRaw[i]);
                    }
                }
            }
        }

        // now reduce the map of the unsorted indices so they point to the new vertices
        VtIntArray mappedIndices;
        mappedIndices.reserve(faceVertexIndices.get().size());
        for (int& vertexIndex : faceVertexIndices.get())
        {
            mappedIndices.push_back(vertexIndicesMap[vertexIndex]);
        }

        // TODO: splitting skeleton isn't supported yet - not sure if there's ever a reason to do even do so
        VtTokenArray jointNames = m_data->m_jointNames;
        VtIntArray jointIndices = m_data->m_jointIndices;
        int jointIndicesElementSize = m_data->m_jointIndicesElementSize;
        VtFloatArray jointWeights = m_data->m_jointWeights;
        int jointWeightsElementSize = m_data->m_jointWeightsElementSize;

        // slice primvars
        PrimvarDataMap primvars;
        for (const auto& primvar : m_data->m_primvars)
        {
            TfToken name = primvar.first;
            primvars[name] =
                _slicePrimvar(name, primvar.second, m_data.get(), points.size(), vertexIndicesMap, m_faceVertexCountIndices);
        }

        // copy metadata
        UsdMetadataValueMap metadata = m_data->m_metadata;

        // copy attributes that aren't in the subset blacklist
        AttributeDataMap attributes;
        for (const auto& [attrName, attrData] : m_data->m_attributes)
        {
            if (m_data->m_subsetAttributeBlacklist->find(attrName) == m_data->m_subsetAttributeBlacklist->end())
            {
                attributes.insert(std::make_pair(attrName, attrData));
            }
        }

        // update with new data
        m_data.reset(new SharedData(m_data->m_sourcePath,
                                    std::move(metadata),
                                    std::move(attributes),
                                    std::move(primvars),
                                    false,
                                    std::move(faceVertexCounts),
                                    std::move(mappedIndices),
                                    std::move(points),
                                    std::move(holeIndices),
                                    std::move(cornerIndices),
                                    std::move(cornerSharpnesses),
                                    std::move(creaseIndices),
                                    std::move(creaseLengths),
                                    std::move(creaseSharpnesses),
                                    std::move(extent),
                                    std::move(jointNames),
                                    std::move(jointIndices),
                                    jointIndicesElementSize,
                                    std::move(jointWeights),
                                    jointWeightsElementSize));
    }

    // Used to compute geometry data if this is a superset.
    void computeSupersetGeometry()
    {
        m_specifier = SdfSpecifierDef;
        // for now we always assume tyhe type will be mesh
        m_typeName = _tokens->mesh;

        // identify if there are child prims with differing materials - this means we need to make geom subset children
        // rather than just grouping everything into a single prim
        bool multipleMaterials = false;
        if (m_supersetChildren.size() >= 2)
        {
            SdfPath firstMaterial = m_supersetChildren.front().pImpl->m_materialPath;
            for (size_t i = 1; i < m_supersetChildren.size(); ++i)
            {
                if (m_supersetChildren[i].pImpl->m_materialPath != firstMaterial)
                {
                    multipleMaterials = true;
                    break;
                }
            }
        }

        // perform a first pass just to determine the sizes of the vectors
        size_t faceVertexCountsSize = 0;
        size_t faceVertexIndicesSize = 0;
        size_t pointsSize = 0;
        size_t holeIndicesSize = 0;
        size_t cornerIndicesSize = 0;
        size_t cornerSharpnessesSize = 0;
        size_t creaseIndicesSize = 0;
        size_t creaseLengthsSize = 0;
        size_t creaseSharpnessesSize = 0;
        size_t jointNamesSize = 0;
        size_t jointIndicesSize = 0;
        size_t jointWeightsSize = 0;
        for (VirtualMesh& child : m_supersetChildren)
        {
            child.computeGeometry();
            faceVertexCountsSize += child.getFaceVertexCounts().size();
            faceVertexIndicesSize += child.getFaceVertexIndices().size();
            pointsSize += child.getPoints().size();
            holeIndicesSize += child.getHoleIndices().size();
            cornerIndicesSize += child.getCornerIndices().size();
            cornerSharpnessesSize += child.getCornerSharpnesses().size();
            creaseIndicesSize += child.getCreaseIndices().size();
            creaseLengthsSize += child.getCreaseLengths().size();
            creaseSharpnessesSize += child.getCreaseSharpnesses().size();
            jointNamesSize += child.getJointNames().size();
            jointIndicesSize += child.getJointIndices().size();
            jointWeightsSize += child.getJointWeights().size();
        }

        // list of primvars and their default values that we care to merge if they are authored on children
        static std::map<TfToken, VtValue> s_primvarDefaults = {
            { UsdGeomTokens->primvarsDisplayColor, VtValue(GfVec3f(0.0, 0.0, 0.0)) },
            { UsdGeomTokens->primvarsDisplayOpacity, VtValue(1.0f) },
            { UsdGeomTokens->normals, VtValue(GfVec3f(0.0, 0.0, 0.0)) },
            { _tokens->primvarsNormals, VtValue(GfVec3f(0.0, 0.0, 0.0)) },
            { _tokens->primvarsSt, VtValue(GfVec2f(0.0, 0.0)) },
            { _tokens->primvarsSt0, VtValue(GfVec2f(0.0, 0.0)) },
            { _tokens->primvarsSt1, VtValue(GfVec2f(0.0, 0.0)) },
            { _tokens->primvarsSt2, VtValue(GfVec2f(0.0, 0.0)) },
        };

        // new geometry data
        VtIntArray faceVertexCounts;
        faceVertexCounts.reserve(faceVertexCountsSize);
        VtIntArray faceVertexIndices;
        faceVertexIndices.reserve(faceVertexIndicesSize);
        VtVec3fArray points;
        points.reserve(pointsSize);
        VtIntArray holeIndices;
        holeIndices.reserve(holeIndicesSize);
        VtIntArray cornerIndices;
        cornerIndices.reserve(cornerIndicesSize);
        VtFloatArray cornerSharpnesses;
        cornerSharpnesses.reserve(cornerSharpnessesSize);
        VtIntArray creaseIndices;
        creaseIndices.reserve(creaseIndicesSize);
        VtIntArray creaseLengths;
        creaseLengths.reserve(creaseLengthsSize);
        VtFloatArray creaseSharpnesses;
        creaseSharpnesses.reserve(creaseSharpnessesSize);
        std::map<SdfPath, VtIntArray> materialSubsets;

        // whether we need to handle skeleton data
        const bool handleSkeleton = isSkeleton();

        // new skeleton data
        VtTokenArray jointNames;
        jointNames.reserve(jointNamesSize);
        VtIntArray jointIndices;
        jointIndices.reserve(jointIndicesSize);
        VtFloatArray jointWeights;
        jointWeights.reserve(jointWeightsSize);
        int jointMaxElementSize = 0;
        std::map<TfToken, int> jointNameToIndex;

        // merge geometry data, primvars, attributes, and relationships
        int faceOffset = 0;
        int vertexOffset = 0;
        SdfPath sourcePath;
        GfVec3f minExtent = GfVec3f(std::numeric_limits<float>::infinity());
        GfVec3f maxExtent = GfVec3f(-std::numeric_limits<float>::infinity());
        AttributeDataMap attributes;
        std::unordered_map<TfToken, PrimvarData::PrimvarMeshVector, TfToken::HashFunctor> primvars;
        for (VirtualMesh& child : m_supersetChildren)
        {
            // the material path of this child
            SdfPath materialPath = child.pImpl->m_materialPath;

            // do we need to apply a transform for this child?
            GfMatrix4d targetMatrix = child.getLocalToWorldTransform();
            bool applyTransform = (targetMatrix != m_rootLocalToWorldTransform);

            if (applyTransform)
            {
                targetMatrix = targetMatrix * m_rootLocalToWorldTransform.GetInverse();
                // we need to extract and store the rotation matrix on the child VirtualMesh so we can use it to rotate
                // normals when primvars are merged since we're applying to the transform to the geometry
                GfVec3d scaleVecUnused{ 1.0, 1.0, 1.0 };
                GfVec3d translationVecUnused;
                GfMatrix4d scaleOrientMatUnused, perspMatUnused;
                targetMatrix.Factor(&scaleOrientMatUnused,
                                    &scaleVecUnused,
                                    &child.pImpl->m_rotationMatrix, // extract into output parameter
                                    &translationVecUnused,
                                    &perspMatUnused);
            }

            // process subdivision surface values first so offsets are correct
            for (int holeIndex : child.getHoleIndices())
            {
                holeIndices.push_back(holeIndex + faceOffset);
            }
            for (int cornerIndex : child.getCornerIndices())
            {
                cornerIndices.push_back(vertexOffset + cornerIndex);
            }
            for (float cornerSharpness : child.getCornerSharpnesses())
            {
                cornerSharpnesses.push_back(cornerSharpness);
            }
            for (int creaseIndex : child.getCreaseIndices())
            {
                creaseIndices.push_back(vertexOffset + creaseIndex);
            }
            for (int creaseLength : child.getCreaseLengths())
            {
                creaseLengths.push_back(creaseLength);
            }
            for (float creaseSharpness : child.getCreaseSharpnesses())
            {
                creaseSharpnesses.push_back(creaseSharpness);
            }

            // if we have multiple materials we also have to record faces as material subsets (unless its the unassigned
            // subset)
            bool recordAsSubset = multipleMaterials && !materialPath.IsEmpty() &&
                                  (!m_hasUnassignedSubset || materialPath != m_unassignedSubsetMaterial);
            for (int faceVertexCount : child.getFaceVertexCounts())
            {
                faceVertexCounts.push_back(faceVertexCount);
                if (recordAsSubset)
                {
                    materialSubsets[materialPath].push_back(faceOffset);
                }
                ++faceOffset;
            }

            for (int faceVertexIndex : child.getFaceVertexIndices())
            {
                faceVertexIndices.push_back(vertexOffset + faceVertexIndex);
            }

            for (GfVec3f point : child.getPoints())
            {
                // transform the point if needed
                if (applyTransform)
                {
                    point = GfVec3f(targetMatrix.Transform(point));
                }
                // keep the extents updated
                _updateExtent(point, minExtent, maxExtent);

                points.push_back(point);
            }

            vertexOffset += static_cast<int>(child.getPoints().size());

            // handle skeleton data?
            if (handleSkeleton)
            {
                // update the joint max element size
                jointMaxElementSize = std::max(child.pImpl->m_data->m_jointIndicesElementSize, jointMaxElementSize);
                jointMaxElementSize = std::max(child.pImpl->m_data->m_jointWeightsElementSize, jointMaxElementSize);

                // build the mapping from joint names to index
                for (const TfToken& jointName : child.getJointNames())
                {
                    if (jointNameToIndex.count(jointName) == 0)
                    {
                        int newIndex = int(jointNameToIndex.size());
                        jointNameToIndex[jointName] = newIndex;
                        jointNames.push_back(jointName);
                    }
                }
            }

            // use the first valid path as the source path
            if (sourcePath.IsEmpty())
            {
                sourcePath = child.getSourcePath();
            }

            // combine override and base privmars
            PrimvarDataMap childPrimvars;
            for (const auto& primvar : child.pImpl->m_primvarOverrides)
            {
                childPrimvars.insert(primvar);
            }
            for (const auto& primvar : child.pImpl->m_data->m_primvars)
            {
                childPrimvars.insert(primvar);
            }
            // check for attributes that should be treated as primvars
            for (const TfToken& attrName : s_mergeAttrsAsPrimvars)
            {
                // already have it as a primvar? - then skip
                if (childPrimvars.find(attrName) != childPrimvars.end())
                {
                    continue; // LCOV_EXCL_LINE
                }

                // do we actually have this attribute on the child?
                auto findAttrData = child.pImpl->m_data->m_attributes.find(attrName);
                if (findAttrData != child.pImpl->m_data->m_attributes.end())
                {
                    const AttributeData& attrData = findAttrData->second;
                    VtValue attrValue = attrData.m_defaultValue;

                    // an array type? Can't treat as a primvar (we only know how to handle single value attributes as
                    // constant primvars)
                    if (attrValue.IsArrayValued())
                    {
                        continue;
                    }

                    // convert the single value to a single element array
                    attrValue = _toArrayVtValue(attrValue);

                    // create a primvar style name
                    const TfToken primvarName("primvars:" + attrName.GetString());

                    // construct the primvar
                    PrimvarData primvarData(primvarName,
                                            attrData.m_typeName.GetArrayType(),
                                            std::move(attrValue),
                                            UsdGeomTokens->constant);
                    childPrimvars.insert(std::make_pair(primvarName, primvarData));
                }
            }

            // collect primvars (and ensure any primvars that are normals are rotated if needed)
            for (const auto& [primvarName, primvarData] : childPrimvars)
            {
                // do we need a version of the primvar that is rotated as normals or just as is?
                if (applyTransform && primvarData.m_isNormals)
                {
                    primvars[primvarName].push_back(
                        std::make_pair(primvarData.rotateNormals(child.pImpl->m_rotationMatrix), child));
                }
                else
                {
                    primvars[primvarName].push_back(std::make_pair(primvarData, child));
                }
            }

            // combine override and base attributes
            AttributeDataMap childAttributes;
            for (const auto& attribute : child.pImpl->m_attributeOverrides)
            {
                childAttributes.insert(attribute);
            }
            for (const auto& attribute : child.pImpl->m_data->m_attributes)
            {
                childAttributes.insert(attribute);
            }

            // copy child attributes that aren't already in the superset
            for (const auto& [attrName, attrData] : childAttributes)
            {
                // drop if this is already being treated as a primvar
                if (s_mergeAttrsAsPrimvars.find(attrName) != s_mergeAttrsAsPrimvars.end())
                {
                    continue;
                }

                // drop xform attributes from children
                if (TfStringStartsWith(attrName.GetString(), "xformOp:") || attrName == "xformOpOrder")
                {
                    continue;
                }

                auto findAttrData = attributes.find(attrName);
                if (findAttrData == attributes.end())
                {
                    attributes.insert(std::make_pair(attrName, attrData));
                }
            }

            // TODO: all relationships from children are currently blindly copied - should we do something different
            // here?
            for (const auto& [relName, relData] : child.pImpl->m_relationships)
            {
                m_relationships.insert(std::make_pair(relName, relData));
            }
        }

        // set extent (unless there are no points)
        VtVec3fArray extent;
        if (!points.empty())
        {
            extent = { minExtent, maxExtent };
        }

        // perform a second pass to handle the skeleton data (if needed)
        if (handleSkeleton)
        {
            for (VirtualMesh& child : m_supersetChildren)
            {
                VtTokenArray childJointNames = child.getJointNames();
                VtIntArray childJointIndices = child.getJointIndices();
                VtFloatArray childJointWeights = child.getJointWeights();

                // If the number of influences needs to be increased do so.
                if (child.pImpl->m_data->m_jointIndicesElementSize != jointMaxElementSize)
                {
                    UsdSkelResizeInfluences(&childJointIndices,
                                            child.pImpl->m_data->m_jointIndicesElementSize,
                                            jointMaxElementSize);
                    UsdSkelResizeInfluences(&childJointWeights,
                                            child.pImpl->m_data->m_jointWeightsElementSize,
                                            jointMaxElementSize);
                }

                // Update joint indices based on the new joint names.
                if (!childJointNames.empty())
                {
                    for (size_t index = 0; index < childJointIndices.size(); ++index)
                    {
                        int jointIndex = childJointIndices[index];
                        TfToken jointName = childJointNames[jointIndex];
                        childJointIndices[index] = jointNameToIndex[jointName];
                    }
                }

                // Push the newly computed joint indices and weights back in the superset values
                for (const auto value : childJointIndices)
                {
                    jointIndices.push_back(value);
                }
                for (const auto value : childJointWeights)
                {
                    jointWeights.push_back(value);
                }
            }
        }

        // TODO: possibly future room for optimization when merging primvars - this the biggest timesink currently
        // merge the primvars
        PrimvarDataMap mergedPrimvars;
        for (auto& [primvarName, primvarMeshVector] : primvars)
        {
            VtValue defaultValue;

            // look up the primvar default
            auto findPrimvar = s_primvarDefaults.find(primvarName);
            // in the known primvars map with a default value, use that
            if (findPrimvar != s_primvarDefaults.end())
            {
                defaultValue = findPrimvar->second;
            }
            // otherwise check if all privmars have the same type and use the default value of the first
            else
            {
                // sanity check - should never be reached
                if (primvarMeshVector.empty())
                {
                    continue; // LCOV_EXCL_LINE
                }

                // if the primvar isn't on each mesh to merge, then don't attempt to merge it
                if (primvarMeshVector.size() < m_supersetChildren.size())
                {
                    continue;
                }

                // get the attribute data of the first primvar
                const AttributeData& attrData = primvarMeshVector.front().first.m_attrData;

                // we can only merge array type primvars
                if (!attrData.m_defaultValue.IsArrayValued())
                {
                    continue; // LCOV_EXCL_LINE
                }

                // if this wasn't in the list of primvars with known defaults, then check they are all the same type
                bool typesMatch = true;
                SdfValueTypeName firstType = attrData.m_typeName;
                for (size_t i = 1; i < primvarMeshVector.size(); ++i)
                {
                    if (primvarMeshVector[i].first.m_attrData.m_typeName != firstType)
                    {
                        // can't merge primvars with different types
                        typesMatch = false;
                        break;
                    }
                }
                if (!typesMatch)
                {
                    continue;
                }

                // get the default value for this primvar's type
                defaultValue = _getDefaultSingleVtValue(attrData.m_defaultValue);
            }

            PrimvarData newPrimvar = PrimvarData::mergePrimvars(primvarName, defaultValue, primvarMeshVector);
            if (newPrimvar.isValid())
            {
                mergedPrimvars.insert(std::make_pair(primvarName, newPrimvar));
            }
        }

        // TODO: currently we don't handle any prim metadata for supersets - should we?

        // update with new data
        m_data.reset(new SharedData(sourcePath,
                                    UsdMetadataValueMap(),
                                    std::move(attributes),
                                    std::move(mergedPrimvars),
                                    false,
                                    std::move(faceVertexCounts),
                                    std::move(faceVertexIndices),
                                    std::move(points),
                                    std::move(holeIndices),
                                    std::move(cornerIndices),
                                    std::move(cornerSharpnesses),
                                    std::move(creaseIndices),
                                    std::move(creaseLengths),
                                    std::move(creaseSharpnesses),
                                    std::move(extent),
                                    std::move(jointNames),
                                    std::move(jointIndices),
                                    jointMaxElementSize,
                                    std::move(jointWeights),
                                    jointMaxElementSize));
        // apply material subsets to the new data and drop the material binding if this VirtualMesh is now using
        // material subsets
        if (!materialSubsets.empty())
        {
            // set material subsets on shared data
            m_data->m_materialSubsets = std::move(materialSubsets);
            // if this is using an unassigned subset, ensure the material for it is correctly bound
            if (m_hasUnassignedSubset)
            {
                bindMaterial(m_unassignedSubsetMaterial, UsdMetadataValueMap());
            }
            // otherwise drop the material binding
            else
            {
                unbindMaterial();
            }
        }
    }

    // Helper function to create a new Sdf attribute for geometry data
    void createGeometryAttr(SdfPrimSpecHandle& primSpec,
                            const TfToken& name,
                            const SdfValueTypeName& typeName,
                            const VtValue& defaultValue,
                            const SdfTimeSampleMap& timeSamples)
    {
        SdfVariability variability = SdfVariabilityVarying;
        bool custom = false;

        // find the original attribute data
        auto findAttrData = m_data->m_attributes.find(name);
        if (findAttrData != m_data->m_attributes.end())
        {
            variability = findAttrData->second.m_variability;
            custom = findAttrData->second.m_custom;
        }

        SdfAttributeSpecHandle attrSpec = SdfAttributeSpec::New(primSpec, name, typeName, variability, custom);

        // set metadata
        if (findAttrData != m_data->m_attributes.end())
        {
            for (const auto& [key, value] : findAttrData->second.m_metadata)
            {
                attrSpec->SetInfo(key, value);
            }
        }

        // set default value
        if (!defaultValue.IsEmpty())
        {
            attrSpec->SetDefaultValue(defaultValue);
        }

        // set time samples
        if (!timeSamples.empty())
        {
            attrSpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
        }
    }
};


VirtualMesh::OptLifetime::OptLifetime()
{
    // activate the pool
    s_intPool.setActive(true);
}


VirtualMesh::OptLifetime::~OptLifetime()
{
    // deactivate the pool - this clears all the memory currently in the pool
    s_intPool.setActive(false);
}


VirtualMesh::ConfigLifetime::ConfigLifetime()
{
}


VirtualMesh::ConfigLifetime::~ConfigLifetime()
{
    s_mergeAttrsAsPrimvars.clear();
}


void VirtualMesh::ConfigLifetime::setMergeAttrsAsPrimvars(const std::vector<std::string>& attrs)
{
    for (const std::string& attrName : attrs)
    {
        s_mergeAttrsAsPrimvars.insert(TfToken(attrName));
    }
}


VirtualMesh::VirtualMesh()
    : pImpl(new Impl())
{
}


VirtualMesh::VirtualMesh(Impl* impl)
    : pImpl(impl)
{
}


VirtualMesh::VirtualMesh(const SdfPath& destinationParentPath,
                         const std::string& destinationName,
                         const TfTokenVector& appliedSchemas,
                         const UsdStageWeakPtr& stage,
                         UsdGeomXformCache& xformCache,
                         int spatialClusterId)
    : pImpl(new Impl(destinationParentPath, destinationName, appliedSchemas, stage, xformCache, spatialClusterId))
{
}


VirtualMesh::VirtualMesh(const UsdPrim& prim,
                         UsdGeomXformCache& xformCache,
                         UsdShadeMaterialBindingAPI::BindingsCache& bindingsCache,
                         UsdShadeMaterialBindingAPI::CollectionQueryCache& collQueryCache)
    : pImpl(new Impl(prim, xformCache, bindingsCache, collQueryCache))
{
}


VirtualMesh::VirtualMesh(const VirtualMesh& other)
    : pImpl(other.pImpl)
{
    ++pImpl->m_refCount;
}


VirtualMesh& VirtualMesh::operator=(const VirtualMesh& other)
{
    if (pImpl == other.pImpl)
    {
        return *this;
    }

    if (pImpl->m_refCount <= 1)
    {
        delete pImpl;
    }
    else
    {
        --pImpl->m_refCount;
    }

    pImpl = other.pImpl;
    ++pImpl->m_refCount;

    return *this;
}


VirtualMesh::~VirtualMesh()
{
    if (pImpl != nullptr)
    {
        if (pImpl->m_refCount <= 1)
        {
            delete pImpl;
        }
        else
        {
            --pImpl->m_refCount;
        }
    }
}


size_t VirtualMesh::getId() const
{
    return pImpl->m_id;
}


const SdfPath& VirtualMesh::getSourcePath() const
{
    return pImpl->m_data->m_sourcePath;
}


SdfPath VirtualMesh::getDestinationPath() const
{
    return pImpl->m_destinationParentPath.AppendChild(pImpl->m_destinationName);
}


const SdfPath& VirtualMesh::getDestinationParentPath() const
{
    return pImpl->m_destinationParentPath;
}


const std::string& VirtualMesh::getDestinationName() const
{
    return pImpl->m_destinationName.GetString();
}


void VirtualMesh::setDestinationPath(const SdfPath& destinationPath)
{
    pImpl->m_destinationParentPath = destinationPath.GetParentPath();
    pImpl->m_destinationName = destinationPath.GetNameToken();
}


void VirtualMesh::setDestinationName(const std::string& name)
{
    pImpl->m_destinationName = TfToken(name.c_str());
}


bool VirtualMesh::isValid() const
{
    return pImpl->m_valid;
}


bool VirtualMesh::isActive() const
{
    return pImpl->m_active;
}


bool VirtualMesh::isDerivedFromPrim() const
{
    return pImpl->m_derivedFromPrim;
}


bool VirtualMesh::isSubset() const
{
    return pImpl->m_isSubset;
}


bool VirtualMesh::isSuperset() const
{
    return pImpl->m_isSuperset;
}


const UsdPrim& VirtualMesh::getPrim() const
{
    return pImpl->m_prim;
}


SdfSpecifier VirtualMesh::getSpecifier() const
{
    return pImpl->m_specifier;
}


TfToken VirtualMesh::getTypeName() const
{
    return pImpl->m_typeName;
}


std::vector<TfToken> VirtualMesh::getAttributeNames() const
{
    return pImpl->getAttributeNames();
}


void VirtualMesh::extendAttributeNameSet(TfToken::HashSet& nameSet) const
{
    pImpl->extendAttributeNameSet(nameSet);
}


bool VirtualMesh::getValue(const TfToken& name, VtValue& value) const
{
    return pImpl->getValue(name, value);
}


void VirtualMesh::setAttributeOverride(const TfToken& name,
                                       const SdfValueTypeName& typeName,
                                       const SdfVariability& variability,
                                       bool custom,
                                       VtValue&& defaultValue)
{
    pImpl->setAttributeOverride(name, typeName, variability, custom, std::move(defaultValue));
}


void VirtualMesh::getAppliedSchemas(TfTokenVector& appliedSchemas, bool includeMaterialBinding) const
{
    pImpl->getAppliedSchemas(appliedSchemas, includeMaterialBinding);
}


bool VirtualMesh::isSkeleton() const
{
    return pImpl->isSkeleton();
}


bool VirtualMesh::computeGeometry()
{
    return pImpl->computeGeometry();
}


bool VirtualMesh::validateAndComputeExtent()
{
    return pImpl->validateAndComputeExtent();
}


const VtIntArray& VirtualMesh::getFaceVertexCounts() const
{
    return pImpl->m_data->m_faceVertexCounts;
}


const VtIntArray& VirtualMesh::getFaceVertexIndices() const
{
    return pImpl->m_data->m_faceVertexIndices;
}


const VtVec3fArray& VirtualMesh::getPoints() const
{
    return pImpl->m_data->m_points;
}


const VtIntArray& VirtualMesh::getHoleIndices()
{
    return pImpl->m_data->m_holeIndices;
}


const VtIntArray& VirtualMesh::getCornerIndices()
{
    return pImpl->m_data->m_cornerIndices;
}


const VtFloatArray& VirtualMesh::getCornerSharpnesses()
{
    return pImpl->m_data->m_cornerSharpnesses;
}


const VtIntArray& VirtualMesh::getCreaseIndices()
{
    return pImpl->m_data->m_creaseIndices;
}


const VtIntArray& VirtualMesh::getCreaseLengths()
{
    return pImpl->m_data->m_creaseLengths;
}


const VtFloatArray& VirtualMesh::getCreaseSharpnesses()
{
    return pImpl->m_data->m_creaseSharpnesses;
}


const VtTokenArray& VirtualMesh::getJointNames()
{
    return pImpl->m_data->m_jointNames;
}


const VtIntArray& VirtualMesh::getJointIndices()
{
    return pImpl->m_data->m_jointIndices;
}


const VtFloatArray& VirtualMesh::getJointWeights()
{
    return pImpl->m_data->m_jointWeights;
}


size_t VirtualMesh::getRtxDuplicateHash() const
{
    return pImpl->getRtxDuplicateHash();
}


const GfMatrix4d& VirtualMesh::getLocalToWorldTransform() const
{
    return pImpl->m_localToWorldTransform;
}


const VtVec3fArray& VirtualMesh::getLocalExtent() const
{
    return pImpl->m_data->m_extent;
}


const VtVec3fArray& VirtualMesh::getWorldExtent() const
{
    return pImpl->m_worldExtent;
}


float VirtualMesh::getExtentMaxSize() const
{
    return pImpl->getExtentMaxSize();
}


float VirtualMesh::getExtentVolume() const
{
    return pImpl->getExtentVolume();
}


float VirtualMesh::getGeometryVolume() const
{
    return pImpl->getGeometryVolume();
}


bool VirtualMesh::hasExplicitMaterialBinding() const
{
    return pImpl->m_explicitMaterialBinding;
}


const SdfPath& VirtualMesh::getBoundMaterialPath() const
{
    return pImpl->m_materialPath;
}


void VirtualMesh::bindMaterial(const SdfPath& materialPath, const UsdMetadataValueMap& metadata)
{
    pImpl->bindMaterial(materialPath, metadata);
}


void VirtualMesh::unbindMaterial()
{
    pImpl->unbindMaterial();
}


const std::map<SdfPath, VtIntArray>& VirtualMesh::getMaterialSubsets() const
{
    return pImpl->m_data->m_materialSubsets;
}


void VirtualMesh::replaceMaterialWithDisplayColor(const ColorValue& baseColor)
{
    pImpl->replaceMaterialWithDisplayColor(baseColor);
}


void VirtualMesh::useSpatialDebug()
{
    pImpl->useSpatialDebug();
}


VirtualMesh VirtualMesh::newModifiedCopy(const GfMatrix4d& xform, VtVec3fArray&& points) const
{
    return VirtualMesh(pImpl->newModifiedCopy(xform, std::move(points)));
}


VirtualMesh VirtualMesh::newSubset(const std::vector<int>&& faceVertexCountIndices)
{
    return VirtualMesh(pImpl->newSubset(std::move(faceVertexCountIndices)));
}


const std::vector<int>& VirtualMesh::getSubsetFaceVertexCountIndices() const
{
    return pImpl->m_faceVertexCountIndices;
}


void VirtualMesh::addSupersetChild(VirtualMesh& child)
{
    pImpl->addSupersetChild(child);
}


const std::vector<VirtualMesh>& VirtualMesh::getSupersetChildren() const
{
    return pImpl->m_supersetChildren;
}


size_t VirtualMesh::getSupersetDataVolume() const
{
    return pImpl->m_supersetDataVolume;
}


int VirtualMesh::getSpatialClusterId() const
{
    return pImpl->m_spatialClusterId;
}


void VirtualMesh::setSpatialClusterId(int id)
{
    pImpl->m_spatialClusterId = id;
}


void VirtualMesh::createInLayer(const UsdStageWeakPtr& stage, SdfLayerHandle& layer)
{
    pImpl->createInLayer(stage, layer);
}

} // namespace omni::scene::optimizer
