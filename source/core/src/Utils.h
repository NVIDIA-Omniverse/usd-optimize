// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/Log.h"
#include "omni/scene.optimizer/core/Report.h"
#include "omni/scene.optimizer/core/Types.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"

// USD
#include <pxr/base/gf/half.h>
#include <pxr/base/gf/math.h>
#include <pxr/base/gf/traits.h>

// C++
#include <chrono>
#include <string>
#include <type_traits>


namespace omni::scene::optimizer
{


// Forward declarations
class Report;


/// Manual timer that can be started, paused, and stopped. Useful for accumulating time spent on a section of code.
///
/// Timers can have a log level which controls whether they are printed to the console.
class ScopedTimer
{
public:
    /// Create a timer with a label.
    ///
    /// When the timer is stopped the duration will be printed along with the label. This timer
    /// defaults to a logging level of Info.
    ///
    /// \param label Label to print when the timer finishes.
    /// \param paused Whether the timer will be initialised in a paused state
    OMNI_SO_EXPORT
    ScopedTimer(const std::string& label, bool paused = false);

    /// Create a timer with a category and logging level.
    ///
    /// This constructor creates a scoped timer with a reporting category, if required, and also
    /// takes a logging level. This allows controlling whether the timer will print based on the
    /// current verbosity.
    ///
    /// \param label The label to log when the timer finishes.
    /// \param category The category (used for reporting)
    /// \param level The log level of messages from this timer.
    /// \param paused Whether the timer will be initialised in a paused state
    OMNI_SO_EXPORT
    ScopedTimer(const std::string& label, const std::string& category, LogLevel level, bool paused = false);

    /// Destructor
    OMNI_SO_EXPORT
    virtual ~ScopedTimer();

    /// Disable copying
    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

    /// (Re)start timing.
    OMNI_SO_EXPORT
    void start();

    /// Temporarily pause timing.
    ///
    /// When paused, the accumulated duration is updated. Timing can be restarted with \p start.
    OMNI_SO_EXPORT
    void pause();

    /// Stop timing.
    ///
    /// This will also print/log the duration of the timer to stdout/reports.
    OMNI_SO_EXPORT
    void stop();

    /// Set the logging level of this timer.
    OMNI_SO_EXPORT
    void setLogLevel(LogLevel level);

private:
    std::string m_label;
    std::string m_category;
    LogLevel m_level;
    size_t m_accumMs = 0;
    std::chrono::time_point<std::chrono::high_resolution_clock> m_start;
    bool m_paused = false;
    bool m_stopped = true;
};


/// Manages a pool of vectors of type T that can be accessed concurrently. If there are no free vectors available,
/// a new one is allocated.
/// Note: this does not garbage collect, the destructor or clear must be called to release memory.
template <typename T>
class ConcurrentVectorMemoryPool
{
public:
    /// Scoped wrapper around a memory pool allocated vector. This means the vector is deallocated when the wrapper goes
    /// out of scope.
    class Vec
    {
    public:
        friend class ConcurrentVectorMemoryPool;

        ~Vec()
        {
            m_memPool.deallocate(m_vec);
        }

        Vec(const Vec&) = delete;
        Vec& operator=(const Vec&) = delete;

        std::vector<T>& get()
        {
            return *m_vec;
        }

    protected:
        Vec(ConcurrentVectorMemoryPool& memPool, std::vector<T>* vec)
            : m_memPool(memPool)
            , m_vec(vec)
        {
        }

    private:
        ConcurrentVectorMemoryPool& m_memPool;
        std::vector<T>* m_vec;
    };

    ConcurrentVectorMemoryPool(bool active)
        : m_active(active)
    {
    }


    ~ConcurrentVectorMemoryPool()
    {
        clear();
    }

    ConcurrentVectorMemoryPool(const ConcurrentVectorMemoryPool&) = delete;
    ConcurrentVectorMemoryPool& operator=(const ConcurrentVectorMemoryPool&) = delete;

    /// Sets whether this memory pool is active or not. If it is not active, then all vectors are allocated directly and
    /// not from the pool
    /// note: this function calls `clear` so it is not thread safe and should only be called when no other threads are
    ///       using the pool.
    void setActive(bool active)
    {
        clear();
        m_active = active;
    }

    /// Either allocates a new vector or returns an existing one from the pool.
    ///
    /// \param size The minimum size that the vector should have of reserved memory.
    Vec allocate(size_t size = 0)
    {
        std::vector<T>* vec = nullptr;
        if (!m_active)
        {
            // not actually using the memory pool, just allocate a new vector
            vec = new std::vector<T>();
        }
        else
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_free.empty())
            {
                vec = new std::vector<T>();
                m_used.insert(vec);
            }
            else
            {
                vec = m_free.back();
                m_free.pop_back();
                vec->clear();
                m_used.insert(vec);
            }
        }

        if (size > 0)
        {
            vec->reserve(size);
        }
        return Vec(*this, vec);
    }

    /// Frees all vectors in the pool.
    /// Warning: Calling clear is not thread safe and should only be called when no other threads are using the pool.
    void clear()
    {
        for (auto& vec : m_free)
        {
            delete vec;
        }
        m_free.clear();
        for (auto& vec : m_used)
        {
            delete vec;
        }
        m_used.clear();
    }

protected:
    /// Returns the vector to the pool. The vector must have been allocated from this pool.
    void deallocate(std::vector<T>* vec)
    {
        // not actually using the memory pool, just delete the vector
        if (!m_active)
        {
            delete vec;
            return;
        }

        // find the vector in the used list and move it to the free list
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_used.find(vec);
        if (it != m_used.end())
        {
            m_free.push_back(*it);
            m_used.erase(it);
        }
    }

private:
    bool m_active;
    std::mutex m_mutex;
    std::vector<std::vector<T>*> m_free;
    std::unordered_set<std::vector<T>*> m_used;
};

/// Hash functor for using SdfValueTypeNames with hash-based containers
struct SdfValueTypeNameHashFunctor
{
    size_t operator()(const PXR_NS::SdfValueTypeName& valueTypeName) const
    {
        return valueTypeName.GetHash();
    }
};


/// Cache of prim to hash
using HashCache = std::map<PXR_NS::UsdPrim, size_t>;

/// Attribute filter for use when hashing
using IncludeAttrValueFn = std::function<bool(const PXR_NS::UsdAttribute&)>;

/// Calculate a hash for a prim
///
/// Attempts to calculate the hash of a prim. The names of the prim and its children are not considered. All
/// authored attributes are included, however connections are handled by hashing the connection property name
/// and the hash of the prim the connection is to. This is intended to allow comparing e.g. shading networks
/// that may be the same but have different internal relationship names etc.
///
/// An optional filter function can be specified to skip attribute values. This allows considering prims
/// equal if they have the same attributes, in the case you don't care what the values of those attributes
/// are. Note that this affects what is put in the HashCache, so changing the includeFn but using the same
/// cache instance would result in hashes that don't match.
///
/// This function is recursive and will hash the prim along with all of its descendants.
///
/// \param usdStage The usd stage
/// \param prim The prim to hash
/// \param cache Optional cache to track prims that were already hashed
/// \param includeFn Optional function to allow ignoring attribute values in the hash
/// \return The calculated hash
OMNI_SO_EXPORT
size_t _hashPrim(const PXR_NS::UsdStageWeakPtr& usdStage,
                 const PXR_NS::UsdPrim& prim,
                 HashCache* cache,
                 const IncludeAttrValueFn& includeFn);

/// Kept for backwards compatibility, overload that requires a hash cache.
OMNI_SO_EXPORT
size_t _hashPrim(const PXR_NS::UsdStageWeakPtr& usdStage,
                 const PXR_NS::UsdPrim& prim,
                 HashCache& cache,
                 const IncludeAttrValueFn& includeFn);

/// This is an overload that just passes a null filter for \p includeFn.
OMNI_SO_EXPORT
size_t _hashPrim(const PXR_NS::UsdStageWeakPtr& usdStage, const PXR_NS::UsdPrim& prim, HashCache& cache);


/// Construct a list of unique prim paths based on a parent and a list of preferred child names.
///
/// If a child with a preferred name name already exists or the name has already been allocated, then a new name will be
/// generated by appending a suitable index as suffix to the name (eg, name_1) to avoid name collisions.
///
/// This mimics the logic of UsdGeomSubset::CreateUniqueGeomSubset but operates in bulk.
///
/// \param usdStage The usd stage
/// \param parentPath The path of the parent prim on the stage
/// \param preferredNames Names that the paths would ideally have provided there are no collisions
/// \return Unique paths for new prims
OMNI_SO_EXPORT
PXR_NS::SdfPathVector _getUniqueChildPaths(const PXR_NS::UsdStageWeakPtr& usdStage,
                                           const PXR_NS::SdfPath& parentPath,
                                           const PXR_NS::TfTokenVector& preferredNames);

/// Create a prim in the stage by defining it and any parents that do not currently exist in the stage.
///
/// Maintain inherited prim state by only defining prims that are not present in the stage regardless of specifier
///
/// \param usdStage The usd stage
/// \param primPath The path of the prim created
/// \param typeName Type name for the prim created
/// \param parentTypeName Type name for any parent prims created
/// \param editLayer Layer in which the prim specs should be created
OMNI_SO_EXPORT
void _safeCreatePrim(const PXR_NS::UsdStageWeakPtr& usdStage,
                     const PXR_NS::SdfPath& primPath,
                     const std::string& typeName,
                     const std::string& parentTypeName,
                     PXR_NS::SdfLayerHandle& editLayer);

/// Determine a color value that can be used as display color instead of this material.
///
/// Use a heuristic on the material to see if it or it's surface shader prim have an attribute like inputs:diffuseColor,
/// inputs:displayColor or inputs:diffuse_color_constant. If this function returns true then \p colorValue was
/// populated with a valid color.
///
/// If a color is found then opacity will also be checked for. It will always be reset to 1.0. If a color is found,
/// and subsequently an opacity attribute, then it will be populated.
///
/// \param material The material to check
/// \param colorValue Output to populate with color values
/// \return Whether a valid color was found.
OMNI_SO_EXPORT
bool _getMaterialAlbedo(const PXR_NS::UsdShadeMaterial& material, ColorValue& colorValue);


/// Find any instanced prims in the scene.
///
/// Checks any prototypes in the scene to record what is being instanced. When finding meshes
/// to merge we can't easily tell if a prim we are looking at is being instanced by something
/// else. This set of prims can be found and then passed in to _findMeshes.
///
/// \param stage The USD stage
/// \param instancedPrims Set of instanced prims to populate.
OMNI_SO_EXPORT
void _findInstancedPrims(const PXR_NS::UsdStageWeakPtr& stage, std::set<PXR_NS::UsdPrim>& instancedPrims);

// Conversion functions for convenience.
OMNI_SO_EXPORT
PXR_NS::SdfPathVector _convertToSdfPaths(const std::vector<PXR_NS::UsdPrim>& prims);

// Returns true if at least one op in OrderedXformOps contains the given suffix.
OMNI_SO_EXPORT
bool _containsOrderedXformOpsSuffix(const PXR_NS::UsdPrim& prim, const PXR_NS::TfToken& suffix);

/// Return true if an attribute of the prim ValueMightBeTimeVarying.
OMNI_SO_EXPORT
bool _mightBeTimeVarying(const PXR_NS::UsdPrim& prim);

/// Return true if an attribute of the prim has any authored time samples
OMNI_SO_EXPORT
bool _hasAuthoredTimeSamples(const PXR_NS::UsdPrim& prim);

/// Filter a list of prims, removing a prim if an attribute has any authored time samples
OMNI_SO_EXPORT
void _removePrimsWithAuthoredTimeSamples(std::vector<PXR_NS::UsdPrim>& prims);

/// Convert HSV values to an RGB color.
OMNI_SO_EXPORT
PXR_NS::GfVec3f _hsvToRgb(float hue, float saturation, float value);

/// Returns true if the prim has authored UVs with values
OMNI_SO_EXPORT
bool _primHasAuthoredUVsWithValues(const PXR_NS::UsdPrim& prim);

/// Flattens this property to a property spec with the same name beneath the given parent prim spec.
OMNI_SO_EXPORT
void _flattenPropertyToPrimSpec(const PXR_NS::UsdProperty& property, const PXR_NS::SdfPrimSpecHandle& spec);

/// Flattens a property to a property spec on a prim spec, with an optional custom value.
///
/// \param property The UsdProperty to flatten
/// \param spec The prim spec to copy the property to
/// \param value An optional value to use instead of the existing property value
OMNI_SO_EXPORT
void _flattenPropertyToPrimSpecWithValue(const PXR_NS::UsdProperty& property,
                                         const PXR_NS::SdfPrimSpecHandle& spec,
                                         const PXR_NS::VtValue& value);

// Returns true if the property can be meaningfully inherited by child prims.
//
// This is used to determine which properties can remain on a parent when splitting a prim into a parent Xform and
// child Gprim
OMNI_SO_EXPORT
bool _isInheritableProperty(const PXR_NS::UsdProperty& property);

// Adds MaterialBindingAPI to the list of applied schemas on the prim spec if it is not already present. Returns true if
// the API schema was added, false if it was already present.
OMNI_SO_EXPORT
bool _addMaterialBindingAPIToSchemas(PXR_NS::SdfListOp<PXR_NS::TfToken>& apiSchemas);

// For each prim, add a child prim of the same type and flatten non-inheritable properties onto a child prim while
// repurposing the current prim as an Xform.
OMNI_SO_EXPORT
void _batchedSplitIntoXformAndChild(const PXR_NS::UsdStageWeakPtr& stage, const std::vector<PXR_NS::SdfPath>& primPaths);


/// Adapted from ArchMakeTmpFileName (pxr/base/arch).  The original uses /var/tmp which
/// is not automatically cleaned up; we override to /tmp to avoid accumulating stale files.
/// Alternatives (tmpnam, mkstemp, boost::filesystem) are either unsafe, non-portable,
/// or add an unwanted link dependency.
///
/// \param prefix Filename prefix (name before the pid)
/// \param suffix Filename suffix (name after the pid)
OMNI_SO_EXPORT
std::string _getTempFile(const std::string& prefix, const std::string& suffix);

/// Calculates the UV scaling value to apply for UV generation tasks.
/// The scaling value is based on the `scaleFactor` multiplied by the distance `scaleUnits` in relation to the distance
/// units of the Usd stage. Where `scaleUnits` of 0.0 signify that distance units should not be applied to the
/// calculated scaling value.
OMNI_SO_EXPORT
float _calculteUVScaleValue(const PXR_NS::UsdStageWeakPtr& stage, float scaleFactor, float scaleUnits);

/// Get a new char* from a std::string.
///
/// Creates a new char*, the caller owns the memory and is responsible for freeing it. Note
/// this uses malloc, and should be released with free.
///
/// \param name The string to get a c string for.
/// \return A new char* the caller now owns.
OMNI_SO_EXPORT
char* getCStr(const std::string& name);

/// Create a copy of \p prim at \p targetPath.
///
/// \param prim The prim to copy
/// \param targetLayer The layer to create the new prim in
/// \param targetPath The full path and name of the new prim to create
OMNI_SO_EXPORT
PXR_NS::UsdPrim _copyPrim(const PXR_NS::UsdPrim& prim,
                          const PXR_NS::SdfLayerHandle& targetLayer,
                          const PXR_NS::SdfPath& targetPath);

/// Completely remove instancing from a prim.
///
/// Assuming an instance, flattens the prim. This function turns off instanceable on the specified
/// prim. It also removes composition (for example a reference to the thing being instanced) and
/// replaces it with explicit properties. All children (what would be the instance proxies underneath
/// the specified prim) are copied from what it was instancing.
///
/// The point of this function is to completely remove instancing so that if the thing being instanced
/// went away it would still exist on its own.
///
/// \return Whether flattening succeeded
OMNI_SO_EXPORT
bool _flattenInstance(const PXR_NS::UsdPrim& prim);

/// Given a number of bytes, format a human-readable string.
///
/// For example, converts a number of bytes to "1.23 MB" or "18 GB".
///
/// \param bytes The raw number of bytes
/// \return Formatted string
OMNI_SO_EXPORT
std::string _getFormattedBytes(double bytes);


/// Get the underlying datatype size of an SdfValueType.
///
/// Given an SdfValueTypeName, tries to return the result of sizeof() on the underlying C++ data
/// type. Not all types are supported, so some may return zero (for example strings).
///
/// \param value The type to get a size from
/// \return The size of the C++ data type, or zero if an unsupported type
OMNI_SO_EXPORT
size_t _getSizeFromSdfValueType(const PXR_NS::SdfValueTypeName& value);


/// Given a VtValue, this function attempts to return a default single value of the same type, or the element type if
/// the VtValue holds an array.
///
/// For example, for a VtValue holding an `int` this function would return a VtValue holding an `int` with a value of 0.
/// And for a VtValue holding an `VtArray<int>` this function would also return a VtValue holding a single `int` with a
/// value of 0.
///
/// \param value The VtValue to get a default single value for.
/// \return A default single VtValue of the same type or element type - or an empty VtValue if unsupported.
OMNI_SO_EXPORT
PXR_NS::VtValue _getDefaultSingleVtValue(const PXR_NS::VtValue& value);


/// Given a VtValue, this function either returns it as is, if its an array type or converts it to a single-element
/// array of the same type.
///
/// \param value The VtValue to convert to an array
/// \return A VtValue holding an array of the same type or element type - or an empty VtValue if unsupported.
OMNI_SO_EXPORT
PXR_NS::VtValue _toArrayVtValue(const PXR_NS::VtValue& value);


/// Given a list of ordered xform ops for a prim, returns a ordered list of any of the xforms ops that are pivots
///
/// \param orderedXformOps The list of ordered xform ops to find pivots in.
/// \return A list of forward pivot ops that participate in a pivot/!invert pair, in the same order they were in the
///         original list. Orphan pivot ops (forward without a matching inverse) are skipped. Empty if no pivot pairs
///         are found.
///
/// Kept for ABI compatibility -- forwards to the 2-arg overload with includeInverseOps=false.
OMNI_SO_EXPORT
std::vector<PXR_NS::UsdGeomXformOp> _getPivotXformOps(const std::vector<PXR_NS::UsdGeomXformOp>& orderedXformOps);

/// \param orderedXformOps The list of ordered xform ops to find pivots in.
/// \param includeInverseOps If true, the returned list also contains the matched inverse ops (interleaved in the order
///        they appeared in \p orderedXformOps). If false, only forward ops are returned, which is convenient when you
///        need the pivot values -- inverse ops do not own an attribute value.
/// \return A list of xform ops that participate in a pivot/!invert pair, in the same order they were in the original
///         list. Orphan pivot ops (forward without a matching inverse, or vice versa) are skipped. Empty if no pivot
///         pairs are found.
OMNI_SO_EXPORT
std::vector<PXR_NS::UsdGeomXformOp> _getPivotXformOps(const std::vector<PXR_NS::UsdGeomXformOp>& orderedXformOps,
                                                      bool includeInverseOps);


// Tolerance comparison for numeric types
//
// Overloaded `isClose(a, b, epsilon)` family: returns true when `a` and `b`
// are within `epsilon`. Covers scalar (double / float / GfHalf), GfMatrix,
// GfVec, GfQuat, and VtArray<T> for any T the family already supports.
//
// All arithmetic is forwarded to `GfIsClose`, so comparisons are performed
// in double precision regardless of input width (no narrowing).
inline bool isClose(const double& a, const double& b, double epsilon)
{
    return PXR_NS::GfIsClose(a, b, epsilon);
}

inline bool isClose(const float& a, const float& b, double epsilon)
{
    return PXR_NS::GfIsClose(a, b, epsilon);
}

inline bool isClose(const PXR_NS::GfHalf& a, const PXR_NS::GfHalf& b, double epsilon)
{
    return PXR_NS::GfIsClose(a, b, epsilon);
}

template <typename T, typename std::enable_if<PXR_NS::GfIsGfMatrix<T>::value>::type* = nullptr>
inline bool isClose(const T& a, const T& b, double epsilon)
{
    for (int i = 0; i < (int)T::numRows; ++i)
    {
        for (int j = 0; j < (int)T::numColumns; ++j)
        {
            if (!isClose(a[i][j], b[i][j], epsilon))
            {
                return false;
            }
        }
    }
    return true;
}

template <typename T, typename std::enable_if<PXR_NS::GfIsGfVec<T>::value>::type* = nullptr>
inline bool isClose(const T& a, const T& b, double epsilon)
{
    for (size_t i = 0; i < T::dimension; ++i)
    {
        if (!isClose(a[i], b[i], epsilon))
        {
            return false;
        }
    }
    return true;
}

template <typename T, typename std::enable_if<PXR_NS::GfIsGfQuat<T>::value>::type* = nullptr>
inline bool isClose(const T& a, const T& b, double epsilon)
{
    if (!isClose(a.GetReal(), b.GetReal(), epsilon))
    {
        return false;
    }
    return isClose(a.GetImaginary(), b.GetImaginary(), epsilon);
}

template <typename T>
inline bool isClose(const PXR_NS::VtArray<T>& a, const PXR_NS::VtArray<T>& b, double epsilon)
{
    if (a.size() != b.size())
    {
        return false;
    }
    for (size_t i = 0; i < a.size(); ++i)
    {
        if (!isClose(a[i], b[i], epsilon))
        {
            return false;
        }
    }
    return true;
}


} // namespace omni::scene::optimizer
