// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "OptimizeTimeSamples.h"

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/ResolveSdfPaths.h>
#include <omni/scene.optimizer/core/Utils.h>

// Carbonite
#include <carb/profiler/Profile.h>

// TBB
#include <tbb/parallel_for.h>

PXR_NAMESPACE_USING_DIRECTIVE

// Plugin initialization
SO_PLUGIN_INIT(omni::scene::optimizer::OptimizeTimeSamplesOperation);


namespace omni::scene::optimizer
{

// Constants
constexpr const char* s_category = "OPTIMIZE_TIMESAMPLES";


OptimizeTimeSamplesOperation::OptimizeTimeSamplesOperation()
    : Operation("optimizeTimeSamples",
                "Optimize Time Samples",
                "This operation removes redundant time samples from attributes throughout a stage")
{

    addArgument("paths", "Prim Paths", kDisplayTypePrimPaths, "Optional list of prim paths to consider", m_paths)
        .setPlaceholder("Add prims or all will be processed");

    addArgument("attributes",
                "Attributes",
                kDisplayTypeTextList,
                "If specified, only these attributes will be processed.",
                m_attributeNames)
        .setVisible(false);

    addArgument("removeInterpolated",
                "Remove Interpolated",
                kDisplayTypeBool,
                "Remove intermediate samples that can be linearly interpolated",
                m_removeInterpolated);

    addArgument("epsilonD",
                "Epsilon (Double)",
                kDisplayTypeFloat,
                "Threshold for which to consider double numbers equal",
                m_epsilonD)
        .setPrecision(20);

    addArgument("epsilonF",
                "Epsilon (Float)",
                kDisplayTypeFloat,
                "Threshold for which to consider floating point numbers equal",
                m_epsilonF)
        .setPrecision(20);

    // Debug optimization argument
    addArgument("attributePaths",
                "Attribute Paths",
                kDisplayTypePrimPaths,
                "Internal argument to target explicit attributes for optimization",
                m_attributePaths)
        .setVisible(false);
}


std::string OptimizeTimeSamplesOperation::getAuthor() const
{
    return OMNI_SO_TO_STRING(SO_PLUGIN_AUTHOR);
}


SOPluginVersion OptimizeTimeSamplesOperation::getVersion() const
{
    return { 1, 0, 0 };
}


std::string OptimizeTimeSamplesOperation::getCategory() const
{
    return s_category;
}


std::string OptimizeTimeSamplesOperation::getDisplayGroup() const
{
    return s_displayGroupStage;
}


bool OptimizeTimeSamplesOperation::getSupportsAnalysis() const
{
    return true;
}


void OptimizeTimeSamplesOperation::setAttributeNames(const std::vector<std::string>& attributes)
{
    m_attributes.clear();
    for (const auto& name : attributes)
    {
        m_attributes.insert(TfToken(name));
    }
}


static SdfTimeSampleMap getTimeSampleMap(const UsdAttribute& attribute)
{

    SdfTimeSampleMap timeSamples;

    UsdAttributeQuery attrQuery(attribute);

    std::vector<double> times;
    if (attrQuery.GetTimeSamples(&times))
    {
        for (const auto& time : times)
        {
            VtValue value;
            if (attrQuery.Get(&value, time))
            {
                timeSamples[time].Swap(value);
            }
            else
            {
                timeSamples[time] = VtValue(SdfValueBlock());
            }
        }
    }

    return timeSamples;
}


// Adjust a value by delta to compensate for non-contiguous frames
// Most types can use a simple operator*, but matrices need to be
// handled and arrays need to be unpacked
// Note most types can work with the fallback operator*= so this
// just specializes for VtArray/Matrix for simplicity.
#ifdef _MSC_VER
#    pragma warning(push)
#    pragma warning(disable : 4244) // = Conversion from double to float / int to float
#endif
template <typename T>
void adjustForDelta(T& value, double delta)
{
    // Unpack array types
    if constexpr (VtIsArray<T>::value)
    {
        for (auto& val : value)
        {
            val *= delta;
        }

        return;
    }
    else if constexpr (GfIsGfMatrix<T>::value)
    {
        double* _data = value.data();
        for (int i = 0; i < (int)(T::numRows * T::numColumns); ++i)
        {
            _data[i] *= delta;
        }
    }
    else
    {
        value *= delta;
    }
}
#ifdef _MSC_VER
#    pragma warning(pop)
#endif

// Only certain types support linear interpolation. Anything else is classed as held,
// regardless of the state of the stage.
// https://openusd.org/release/api/interpolation_8h.html#ae1d252a3278800a696ca4707e6b6a3e7
static bool isHeldType(const UsdAttribute& attribute)
{
    static std::set<TfToken> s_linearTypes = {
        SdfValueTypeNames->Double.GetAsToken(),     SdfValueTypeNames->DoubleArray.GetAsToken(),
        SdfValueTypeNames->Double2.GetAsToken(),    SdfValueTypeNames->Double2Array.GetAsToken(),
        SdfValueTypeNames->Double3.GetAsToken(),    SdfValueTypeNames->Double3Array.GetAsToken(),
        SdfValueTypeNames->Double4.GetAsToken(),    SdfValueTypeNames->Double4Array.GetAsToken(),
        SdfValueTypeNames->Float.GetAsToken(),      SdfValueTypeNames->FloatArray.GetAsToken(),
        SdfValueTypeNames->Float2.GetAsToken(),     SdfValueTypeNames->Float2Array.GetAsToken(),
        SdfValueTypeNames->Float3.GetAsToken(),     SdfValueTypeNames->Float3Array.GetAsToken(),
        SdfValueTypeNames->Float4.GetAsToken(),     SdfValueTypeNames->Float4Array.GetAsToken(),
        SdfValueTypeNames->Matrix2d.GetAsToken(),   SdfValueTypeNames->Matrix3d.GetAsToken(),
        SdfValueTypeNames->Matrix4d.GetAsToken(),   SdfValueTypeNames->Quatd.GetAsToken(),
        SdfValueTypeNames->QuatdArray.GetAsToken(), SdfValueTypeNames->Quatf.GetAsToken(),
        SdfValueTypeNames->QuatfArray.GetAsToken(), SdfValueTypeNames->Half.GetAsToken(),
        SdfValueTypeNames->HalfArray.GetAsToken(),
    };

    auto findIt = s_linearTypes.find(attribute.GetTypeName().GetAsToken());

    return findIt == s_linearTypes.end();
}


// For held types things are really simple. If we see a duplicate value then it gets
// removed - the existing value is held until it changes so any subsequent duplicate
// can just be removed.
static size_t filterHeld(const std::vector<double>& times, SdfTimeSampleMap& timeSamples)
{
    size_t removed = 0;
    VtValue lastValue;

    for (size_t index = 0; index < times.size(); ++index)
    {
        // Grab the current value.
        // Note: don't take a const ref, otherwise it may be invalidated (on windows)
        // after erasing.
        VtValue value = timeSamples[times[index]];

        if (index > 0)
        {
            // For held types only a simple VtValue equality check required
            // They can't be interpolated, so they are "held" until the value changes
            if (value == lastValue)
            {
                timeSamples.erase(times[index]);
                ++removed;
            }
        }

        lastValue = value;
    }

    return removed;
}


// When filtering floating-point types we want to not check they are equal, but "essentially
// equal" to compensate for precision.
//
// Much like the Sparse Value Writer this looks for a set of three samples, meaning the middle
// one can be removed as it is a duplicate that is redundant.
template <typename T>
static size_t filterFloatingPoint(const std::vector<double>& times, double epsilon, SdfTimeSampleMap& timeSamples)
{
    T lastValue{};
    bool lastEqual = false;

    size_t removed = 0;

    for (size_t index = 0; index < times.size(); ++index)
    {
        // Note: due to how UncheckedGet and VtValue work, do not take a const ref here.
        // Otherwise, we can end up with bogus memory in "last" before it is returned.
        T value = timeSamples[times[index]].UncheckedGet<T>();

        if (index > 0)
        {

            // Check if the values are equal (or close enough to equal)
            bool equal = isClose(value, lastValue, epsilon);

            // If they are equal, and the last check was also equal, then this is a set of
            // three. We can delete the middle one without affecting anything (either held or linear).
            // Rinse and repeat.
            if (equal && lastEqual)
            {
                timeSamples.erase(times[index - 1]);
                ++removed;
            }

            lastEqual = equal;
        }

        // Final sample
        if (index == times.size() - 1)
        {
            // If the last sample is the same as the previous value, then it's unnecessary
            // It got to that point and stopped as there are no further times to interpolate
            // to therefore we can just remove it.
            if (isClose(value, lastValue, epsilon))
            {
                timeSamples.erase(times[index]);
                ++removed;
            }
        }

        lastValue = value;
    }

    return removed;
}


// We can also do some more complex filtering where we remove time samples that can be
// interpolated. For this we look at the delta between two sets of samples (N-1, N, N+1).
// If these deltas match, they moved in a linear fashion meaning N is redundant.
template <typename T>
size_t filterInterpolated(const std::vector<double>& times, double epsilon, SdfTimeSampleMap& timeSamples)
{
    T lastValue{};
    T lastDiff{};
    double lastDelta = 0;

    size_t removed = 0;

    for (size_t index = 0; index < times.size(); ++index)
    {
        T value = timeSamples[times[index]].UncheckedGet<T>();

        // From the second sample on we can generate a delta
        if (index > 0)
        {
            double delta = times[index] - times[index - 1];

            T diff = value - lastValue;

            // We need to compensate for non-sequential frames, meaning the data is still
            // linear but the delta is different due to jumping more or fewer frames. For now,
            // we keep the original diff (using the potential new delta), then reset to that
            // afterwards. The idea was that maybe there was linear motion that changed speed,
            // so then for the next round we would be comparing to the "new delta". Maybe this
            // was overthinking it...
            T tempLast = diff;

            // For the third sample on, we now have deltas to compare
            if (index > 1)
            {
                // Non-sequential compensation
                if (delta != lastDelta)
                {
                    adjustForDelta(diff, lastDelta / delta);
                }

                // Check whether the values of the deltas match. If so, this is three samples where
                // the middle can be linearly interpolated, and is therefore redundant.
                if (isClose(diff, lastDiff, epsilon))
                {
                    timeSamples.erase(times[index - 1]);
                    ++removed;
                }
            }

            lastDiff = tempLast;
            lastDelta = times[index] - times[index - 1];
        }

        // Final sample
        if (index == times.size() - 1 && index > 0)
        {
            // If the final sample is the same as the previous value, then it's unnecessary
            // The diff compensation has taken care of the rest, this is really just cleaning up
            // the final frame
            if (isClose(value, lastValue, epsilon))
            {
                timeSamples.erase(times[index]);
                ++removed;
            }
        }

        lastValue = value;
    }

    return removed;
}


#define SO_FILTER_TYPE(TYPENAME, T, E)                                                                                 \
    if (typeName == TYPENAME)                                                                                          \
    {                                                                                                                  \
        if (m_removeInterpolated)                                                                                      \
        {                                                                                                              \
            return filterInterpolated<T>(times, E, timeSamples);                                                       \
        }                                                                                                              \
        else                                                                                                           \
        {                                                                                                              \
            return filterFloatingPoint<T>(times, E, timeSamples);                                                      \
        }                                                                                                              \
    }


size_t OptimizeTimeSamplesOperation::filterTimeSamples(const UsdAttribute& attribute, SdfTimeSampleMap& timeSamples) const
{

    // We sometimes want to mutate timeSamples in an odd way (not just the current element) so
    // for simplicity just cache the times so that we can work on them independently.
    std::vector<double> times;
    times.reserve(timeSamples.size());
    for (const auto& it : timeSamples)
    {
        times.push_back(it.first);
    }

    // For non-held attributes (basically floating point values that can be interpolated between)
    // we do more complex filtering.
    if (!isHeldType(attribute))
    {
        const SdfValueTypeName& typeName = attribute.GetTypeName();

        // Switch on the attribute type
        SO_FILTER_TYPE(SdfValueTypeNames->Double, double, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->DoubleArray, VtDoubleArray, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Double2, GfVec2d, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Double2Array, VtVec2dArray, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Double3, GfVec3d, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Double3Array, VtVec3dArray, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Double4, GfVec4d, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Double4Array, VtVec4dArray, m_epsilonD)

        SO_FILTER_TYPE(SdfValueTypeNames->Float, float, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->FloatArray, VtFloatArray, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->Float2, GfVec2f, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->Float2Array, VtVec2fArray, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->Float3, GfVec3f, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->Float3Array, VtVec3fArray, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->Float4, GfVec4f, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->Float4Array, VtVec4fArray, m_epsilonF)

        SO_FILTER_TYPE(SdfValueTypeNames->Matrix2d, GfMatrix2d, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Matrix3d, GfMatrix3d, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Matrix4d, GfMatrix4d, m_epsilonD)

        SO_FILTER_TYPE(SdfValueTypeNames->Quatd, GfQuatd, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->QuatdArray, VtQuatdArray, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->Quatf, GfQuatf, m_epsilonF)
        SO_FILTER_TYPE(SdfValueTypeNames->QuatfArray, VtQuatfArray, m_epsilonF)

        SO_FILTER_TYPE(SdfValueTypeNames->Half, GfHalf, m_epsilonD)
        SO_FILTER_TYPE(SdfValueTypeNames->HalfArray, VtHalfArray, m_epsilonD)
    }

    // For anything else we can do a much simpler duplicate removal.
    return filterHeld(times, timeSamples);
}


void OptimizeTimeSamplesOperation::processAttributes(const AttributeCallback& callback, bool thread)
{
    // Get the list of prims to process
    const std::vector<UsdPrim>& prims = _resolveExpressionsToPrims(getUsdStage(), m_paths);

    // Convert attribute filter strings to tokens
    setAttributeNames(m_attributeNames);

    // When executing from analysis-mode, convert strings to SdfPaths in a set.
    std::set<SdfPath> attributePaths;
    for (const auto& path : m_attributePaths)
    {
        attributePaths.insert(SdfPath(path));
    }

    auto fn = [&](const tbb::blocked_range<size_t>& range)
    {
        for (size_t i = range.begin(); i < range.end(); ++i)
        {
            const auto& prim = prims[i];

            // Can't author to instance proxies
            if (prim.IsInstanceProxy())
            {
                continue;
            }

            for (const auto& attribute : prim.GetAuthoredAttributes())
            {
                // Explicit attribute path
                if (!m_attributePaths.empty())
                {
                    if (attributePaths.find(attribute.GetPath()) == attributePaths.end())
                    {
                        continue;
                    }
                }

                // Optional attribute name filter
                if (!m_attributes.empty())
                {
                    if (m_attributes.find(attribute.GetName()) == m_attributes.end())
                    {
                        continue;
                    }
                }

                // Get the time sample map directly.
                // We can edit this to remove the redundant ones and then set it back.
                SdfTimeSampleMap timeSamples = getTimeSampleMap(attribute);
                if (timeSamples.empty())
                {
                    continue;
                }

                size_t originalSize = timeSamples.size();

                // If there was only one sample it is redundant, we don't need to filter anything.
                if (originalSize == 1)
                {
                    callback(attribute, originalSize, originalSize, timeSamples);
                    continue;
                }

                // Do the actual filtering
                // We get back the number of samples removed and an adjusted map of samples.
                size_t removed = filterTimeSamples(attribute, timeSamples);

                // Always trigger the callback, even if nothing removed. This means we can still track
                // the total number of samples checked etc.
                callback(attribute, originalSize, removed, timeSamples);
            }
        }
    };

    size_t count = prims.size();
    if (thread && !getContext()->singleThreaded)
    {
        tbb::parallel_for(tbb::blocked_range<size_t>(0, count), fn);
    }
    else
    {
        fn(tbb::blocked_range<size_t>(0, count));
    }
}


OperationResult OptimizeTimeSamplesOperation::executeAnalysisImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|OptimizeTimeSamples|Analysis");

    // Simple struct for use with concurrent_vector, so we can multithread
    // the main collection
    struct Result
    {
        SdfPath path;
        size_t originalSize = 0;
        size_t redundant = 0;
    };

    // The results will be of arbitrary order, but they will be converted to JSON later
    // which will order them.
    tbb::concurrent_vector<Result> results;

    std::atomic<size_t> totalRedundant = 0;
    std::atomic<size_t> totalSamples = 0;

    auto callback =
        [&](const UsdAttribute& attribute, size_t originalSize, size_t redundant, const SdfTimeSampleMap& timeSamples)
    {
        // If time-samples were removed, and timeSamples now contains only one time sample, it means it
        // too is redundant as we can just flatten the value. Reset redundant to originalSize which accounts
        // for this.
        if (redundant && timeSamples.size() == 1)
        {
            redundant = originalSize;
        }

        totalSamples += originalSize;
        totalRedundant += redundant;

        // If there was something redundant, add to the results vector
        if (redundant)
        {
            auto result = results.emplace_back();
            result->path = attribute.GetPath();
            result->originalSize = originalSize;
            result->redundant = redundant;
        }
    };

    // Analyse filtered prims/attributes
    constexpr bool multiThread = true;
    processAttributes(callback, multiThread);

    // Convert results to JSON payload
    JsObject analysisResult;

    // Pre-size so we can reuse to avoid repeated small vector allocations
    JsArray counts(2);

    for (const auto& it : results)
    {
        counts[0] = JsValue(it.redundant);
        counts[1] = JsValue(it.originalSize);
        analysisResult[it.path.GetAsString()] = counts;
    }

    JsObject resultJson;
    resultJson["analysis"] = analysisResult;

    OperationResult result{ true };
    result.output = getCStr(JsWriteToString(resultJson));

    SO_LOG_INFO("Found %lu of %lu redundant time samples", totalRedundant.load(), totalSamples.load());
    SO_LOG_VERBOSE("Analysis result: %s", result.output);

    return result;
}


OperationResult OptimizeTimeSamplesOperation::executeImpl()
{
    CARB_PROFILE_ZONE(0, "SceneOptimizer|OptimizeTimeSamples");

    size_t totalChecked = 0;
    size_t totalRemoved = 0;

    auto callback =
        [&](const UsdAttribute& attribute, size_t originalSize, size_t redundant, const SdfTimeSampleMap& timeSamples)
    {
        totalChecked += originalSize;

        // Single time-sample.
        // In this case, we just clear it and set it as the value.
        if (originalSize == 1)
        {
            VtValue value;
            attribute.Get(&value, timeSamples.begin()->first);

            attribute.Clear();
            attribute.Set(value);

            if (getReport() || getContext()->verbose)
            {
                std::ostringstream oss;
                oss << attribute.GetPath() << ": replaced single time sample with value";
                SO_LOG_INFO(oss.str().c_str());
            }

            ++totalRemoved;
            return;
        }

        // If it wasn't a single sample and we found redundant samples, set the new samples now.
        if (redundant)
        {
            // If all the time samples except one were removed, then, there is only one
            // time sample left. In this case we can remove it and just set the last
            // value (much the same as if we initially encounter an attribute with only
            // one sample).
            if (timeSamples.size() == 1)
            {
                attribute.Clear();
                attribute.Set(timeSamples.begin()->second);

                // Just for the log below, since we cleared everything
                ++redundant;
            }
            else
            {
                auto layer = getUsdStage()->GetEditTarget().GetLayer();
                auto propertySpec = layer->GetPropertyAtPath(attribute.GetPath());
                if (!propertySpec)
                {
                    // No propertySpec in the root layer. Create a new primSpec (or get the existing
                    // one). This will default to Over which is good.
                    SdfPrimSpecHandle primSpec = SdfCreatePrimInLayer(layer, attribute.GetPrimPath());

                    // Create the property
                    propertySpec = SdfAttributeSpec::New(primSpec,
                                                         attribute.GetName().GetString(),
                                                         attribute.GetTypeName(),
                                                         SdfVariabilityVarying,
                                                         attribute.IsCustom());
                }

                // Set the new values
                propertySpec->SetField(SdfDataTokens->TimeSamples, VtValue(timeSamples));
            }
        }

        totalRemoved += redundant;

        if (getReport() || getContext()->verbose)
        {
            std::ostringstream oss;
            oss << attribute.GetPath() << ": removed " << redundant << "/" << originalSize << " time samples";
            SO_LOG_INFO(oss.str().c_str());
        }
    };

    // Process filtered prims/attributes with the callback
    {
        SdfChangeBlock _changeBlock;
        processAttributes(callback, false);
    }

    if (getReport() || getContext()->verbose)
    {
        std::ostringstream oss;
        oss << "Removed " << totalRemoved << "/" << totalChecked << " total time samples";
        SO_LOG_INFO(oss.str().c_str());
    }

    return { true };
}


} // namespace omni::scene::optimizer
