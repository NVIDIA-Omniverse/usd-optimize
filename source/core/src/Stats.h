// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"

// C++
#include <map>
#include <set>
#include <string>


namespace omni::scene::optimizer
{


/// Object to store info per prim type.
class OMNI_SO_EXPORT PrimInfo
{
public:
    /// Constructor
    PrimInfo() = default;

    /// Operator+=
    void operator+=(const PrimInfo& other);

    size_t count = 0;
    size_t inactive = 0;
    size_t leaf = 0;
    size_t invisible = 0;
    size_t disjoint = 0;
    std::set<size_t> uniqueMaterials;
    size_t unique = 0;
};


class OMNI_SO_EXPORT PrimvarStats
{
public:
    /// Operator+
    void operator+=(const PrimvarStats& other);

    size_t count = 0;
    size_t valueCount = 0;
    size_t sizeOf = 0;
};

/// Object to count stats
///
/// Helper object that works with tbb::combinable
class OMNI_SO_EXPORT StatCounters
{

public:
    /// Constructor
    StatCounters() = default;

    /// Define operator+ to use with combine()
    StatCounters operator+(const StatCounters& other) const;

    size_t prims = 0;
    size_t instanceable = 0;
    size_t instances = 0;
    size_t inactive = 0;
    size_t invisible = 0;
    size_t prototypes = 0;
    size_t timeSamples = 0;
    size_t vertices = 0;
    size_t faces = 0;
    size_t geometries = 0;

    std::map<std::string, PrimInfo> primTypes;
    std::map<PXR_NS::TfToken, PrimvarStats> primvars;
};

/// Typedefs
using StatCountersUPtr = std::unique_ptr<StatCounters>;


/// Toggles for what statistics to capture.
class OMNI_SO_EXPORT StatArgs
{
public:
    /** \name Lifecycle
     * Basic functions
     */
    ///@{

    /// Constructor
    StatArgs();

    /// Destructor
    virtual ~StatArgs();

    /// Disable copy
    StatArgs(const StatArgs&) = delete;

    /// Disable assign
    StatArgs& operator=(const StatArgs&) = delete;
    ///@}

    /** \name Calculate Disjoint Meshes
     * Can be expensive to calculate.
     * Default: false
     */
    ///@{
    /// Getter
    bool getDisjoint() const;

    /// Setter
    void setDisjoint(bool value);
    ///@}

    /** \name Count Primvars
     * Counts the number of primvar attributes and the number of values they have.
     * The value count is based on the flattened number of values (i.e. for indexed
     * primvars the number of indices are counted, not the unique values).
     * Default: false
     */
    ///@{
    /// Getter
    bool getCountPrimvars() const;

    /// Setter
    void setCountPrimvars(bool value);
    ///@}

    /** \name Count Time Samples
     * Default: false
     */
    ///@{
    /// Getter
    bool getTimeSamples() const;

    /// Setter
    void setTimeSamples(bool value);
    ///@}

    /** \name Split Collocated
     * Whether to consider collocated points part of a disjoint mesh.
     * Default: false
     */
    ///@{
    /// Getter
    bool getSplitCollocated() const;

    /// Setter
    void setSplitCollocated(bool value);
    ///@}

    /** \name TimeCode
     * The UsdTimeCode to collect stats at.
     * Default: UsdTimeCode::Default()
     */
    ///@{
    /// Getter
    PXR_NS::UsdTimeCode getTimeCode() const;

    /// Setter
    void setTimeCode(const PXR_NS::UsdTimeCode& timeCode);
    ///@}


private:
    class Impl;
    std::unique_ptr<Impl> pImpl;
};


/// Calculate various statistics based on the specified stage.
///
/// \param usdStage The USD stage to process
/// \param args Optional configuration
/// \return A bunch of raw statistic values based on the scene.
OMNI_SO_EXPORT
StatCountersUPtr _collectSceneStats(const PXR_NS::UsdStageWeakPtr& usdStage, const StatArgs& args = StatArgs());


} // namespace omni::scene::optimizer
