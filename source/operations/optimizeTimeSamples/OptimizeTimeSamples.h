// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>


namespace omni::scene::optimizer
{


/// Optimize Time Samples Operation
///
/// Remove redundant time-samples found on attributes.
class OptimizeTimeSamplesOperation : public Operation
{
public:
    /// Constructor
    explicit OptimizeTimeSamplesOperation();

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    /// Support Analysis
    bool getSupportsAnalysis() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    /// Remove redundant time samples for the specified attribute
    size_t filterTimeSamples(const PXR_NS::UsdAttribute& attribute, PXR_NS::SdfTimeSampleMap& timeSamples) const;

    /// Set the names of attributes to consider.
    void setAttributeNames(const std::vector<std::string>& attributes);

    /// Callback for use after filtering time samples on an attribute
    ///
    /// This function is called for any attribute that has time samples, regardless of whether any
    /// of them are redundant. The \p originalSize and \p redundant parameters describe this.
    ///
    /// \param attribute The Usd Attribute that was checked
    /// \param originalSize The original number of time samples the attribute had
    /// \param redundant The number of time samples deemed redundant
    /// \param timeSamples The new time samples map, with redundant samples removed
    using AttributeCallback = std::function<void(const PXR_NS::UsdAttribute& attribute,
                                                 size_t originalSize,
                                                 size_t redundant,
                                                 const PXR_NS::SdfTimeSampleMap& timeSamples)>;

    /// Process all filtered prims/attributes.
    ///
    /// \param callback Callback function to trigger with results per-attribute
    /// \param thread Whether to multi-thread the prim/attribute checks
    void processAttributes(const AttributeCallback& callback, bool thread);

    std::vector<std::string> m_paths;
    std::vector<std::string> m_attributeNames;
    std::vector<std::string> m_attributePaths;
    std::set<PXR_NS::TfToken> m_attributes;
    bool m_removeInterpolated = false;
    double m_epsilonD = 1e-12;
    double m_epsilonF = 1e-6;
};


} // namespace omni::scene::optimizer
