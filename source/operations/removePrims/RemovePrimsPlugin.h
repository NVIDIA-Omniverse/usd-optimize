// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include <omni/scene.optimizer/core/Operation.h>
#include <omni/scene.optimizer/core/RemovePrims.h>
#include <omni/scene.optimizer/core/UsdIncludes.h>


namespace omni::scene::optimizer
{


/// Object that represents a node in the composition graph for tracking composition relationships between prims
struct CompositionNode
{
    /// The prim path this node represents
    PXR_NS::SdfPath path;
    /// Whether this prim is concrete
    bool concrete;
    /// Whether this node has been connected to by one or more concrete prims
    bool connected = false;
    /// Pointers to child nodes in the composition graph - note: this object does not own these pointers
    std::vector<CompositionNode*> children;
    /// Pointers to parent nodes in the composition graph - note: this object does not own these pointers
    std::vector<CompositionNode*> parents;

    CompositionNode(const PXR_NS::SdfPath& p, bool c)
        : path(p)
        , concrete(c)
    {
    }

    /// Resolves whether this node is concrete by checking up and down the graph and caches the result
    bool resolveConcrete() const
    {
        if (!m_cachedResolveConcrete)
        {
            m_resolvedConcrete = _resolveConcreteUp() || _resolveConcreteDown();
            m_cachedResolveConcrete = true;
        }
        return m_resolvedConcrete;
    }

    /// Marks this node and all its children as connected and sets their resolved concrete state to true
    void connect()
    {
        connected = true;
        m_resolvedConcrete = true;
        m_cachedResolveConcrete = true;
        for (CompositionNode* child : children)
        {
            if (child != nullptr && !child->connected)
            {
                child->connect();
            }
        }
    }

private:
    mutable bool m_cachedResolveConcrete = false;
    mutable bool m_resolvedConcrete = false;

    bool _resolveConcreteUp() const
    {
        if (concrete)
        {
            return true;
        }

        for (const CompositionNode* parent : parents)
        {
            if (parent != nullptr && parent->_resolveConcreteUp())
            {
                return true;
            }
        }

        return false;
    }

    bool _resolveConcreteDown() const
    {
        if (concrete)
        {
            return true;
        }

        for (const CompositionNode* child : children)
        {
            if (child != nullptr && child->_resolveConcreteDown())
            {
                return true;
            }
        }

        return false;
    }
};

typedef std::map<PXR_NS::SdfPath, CompositionNode> CompositionMap;


/// Operation for identifying and removing prims from the stage for various reasons.
class RemovePrimsOperation : public Operation
{
public:
    /// Constructor
    explicit RemovePrimsOperation();

    /// Destructor
    ~RemovePrimsOperation() override;

    /// Get the author of this plugin
    std::string getAuthor() const override;

    /// Get the version of this plugin
    SOPluginVersion getVersion() const override;

    /// Get the category for reporting.
    std::string getCategory() const override;

    /// Get the display group.
    std::string getDisplayGroup() const override;

    // Returns whether or not this operation supports analysis mode
    bool getSupportsAnalysis() const override;

protected:
    /// Entry-point for execution
    OperationResult executeImpl() override;

    /// Entry-point for analysis
    OperationResult executeAnalysisImpl() override;

private:
    std::vector<std::string> m_paths;
    bool m_removeInvisible = true;
    RemoveMethod m_invisibleRemoveMethod = RemoveMethod::eDeactivate;
    bool m_removeOrphanedOvers = true;
    RemoveMethod m_removeOrphanedOversMethod = RemoveMethod::eDelete;
    bool m_explicitMode = false;
    std::vector<std::string> m_explicitInvisiblePaths;
    std::vector<std::string> m_explicitOrphanedPaths;

    // tracks composition relationships between prims
    CompositionMap m_compositionMap;

    // invisible prims that have been found by `findHiddenPrims`
    std::set<PXR_NS::SdfPath> m_invisiblePrims;
    // orphaned overs that have been found by `findHiddenPrims`
    std::set<PXR_NS::SdfPath> m_orphanedOvers;

    // Finds the prims in the stage to remove based on the current settings and stores them in the member variables
    void findPrimsToRemove();

    // Clears all member variable data that has been collected by an execute call
    void clear();
};

} // namespace omni::scene::optimizer
