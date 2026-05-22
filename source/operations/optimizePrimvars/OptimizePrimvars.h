// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>

// C++
#include <vector>


namespace omni::scene::optimizer
{


/// Enum to define Optimize Primvar operations
///
/// \ref OptimizePrimvarsMode::eIgnore Does nothing, intended for leaving indexing alone when simplifying
/// \ref OptimizePrimvarsMode::eIndex Index any non-indexed primvars
/// \ref OptimizePrimvarsMode::eIndexForced Index any non-indexed primvars, and re-index any indexed primvars
/// \ref OptimizePrimvarsMode::eFlatten Flatten any indexed primvars
/// \ref OptimizePrimvarsMode::eRemove Removes the primvar and any indices
enum class OptimizePrimvarsMode
{
    eIgnore = 0, // Do not change the indexing of a primvar
    eIndex = 1, // Index a flattened primvar
    eIndexForced = 2, // Index any primvars, including reindexing if they are already indexed
    eFlatten = 3, // Flatten an indexed primvar
    eRemove = 4, // Remove primvars
};

/// Optimize Primvars Operation
class OptimizePrimvarsOperation : public Operation
{
public:
    /// Constructor
    explicit OptimizePrimvarsOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    /// Analysis mode
    bool getSupportsAnalysis() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    // Forward declaration
    struct Counters;

    PXR_NS::TfToken simplifyPrimvar(PXR_NS::UsdGeomPrimvar& primvar,
                                    const PXR_NS::VtIntArray& faceVertexCounts,
                                    PXR_NS::VtValue& values);

    /// Set the list of primvar names to consider. If empty, all primvars are considered.
    ///
    /// Expects base primvar names without the primvars: prefix.
    ///
    /// \param primvarNames List of names
    void setPrimvars(const std::vector<std::string>& primvarNames);

    /// Process an individual primvar based on the arguments
    ///
    /// \param primvar The primvar to process
    /// \param isBound Whether the prim the primvar belongs to is bound to a material
    /// \param faceVertexCounts The faceVertexCounts, if required by the operation mode
    /// \param counters Struct of counters for tracking what was done
    void processPrimvar(PXR_NS::UsdGeomPrimvar& primvar,
                        bool isBound,
                        const PXR_NS::VtIntArray& faceVertexCounts,
                        Counters& counters);

    std::vector<std::string> m_primPaths;
    std::vector<std::string> m_primvars;
    std::vector<std::string> m_primvarPaths;
    std::set<PXR_NS::TfToken> m_primvarTokens;
    OptimizePrimvarsMode m_mode = OptimizePrimvarsMode::eIgnore;
    bool m_simplify = false;
    bool m_removeIfBound = false;
};


} // namespace omni::scene::optimizer
