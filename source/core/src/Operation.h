// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Argument.h"
#include "omni/scene.optimizer/core/Defs.h"
#include "omni/scene.optimizer/core/UsdIncludes.h"

// Carbonite
#include <carb/logging/Log.h>


namespace omni::scene::optimizer
{

// Forward declarations
class Report;

/// Constants
constexpr const char* s_categoryStatus = "STATUS";

constexpr const char* s_displayGroupGeometry = "Geometry";
constexpr const char* s_displayGroupMaterials = "Materials";
constexpr const char* s_displayGroupStage = "Stage";
constexpr const char* s_displayGroupUtilities = "Utilities";

/// Base Operation Class
///
/// This class provides the base class for implementing Scene Optimizer Operations as plugins.
///
/// It has minimal requirements. Inside your implementation, you can use the following convenience
/// macro to have your plugin registered:
///
/// \code{.cpp}
/// SO_PLUGIN_INIT(omni::scene::optimizer::MyOperationClass);
/// \endcode
///
/// You must implement \ref getAuthor() to provide a useful contact in the case users have
/// questions or issues with your plugin, and also \ref getVersion() to return the version
/// of your plugin. You then implement \ref executeImpl() to perform your actual business logic.
///
/// Optionally you can declare arguments inside your constructor. These will be propagated to the
/// user interface.
class OMNI_SO_EXPORT Operation
{

public:
    /// Standard Constructor
    ///
    /// \param name The internal name of the operation
    /// \param displayName The display name of the operation
    /// \param description A description of what the operation does
    Operation(const std::string& name, const std::string& displayName, const std::string& description);

    /// Disable copying.
    Operation(const Operation&) = delete;
    void operator=(const Operation&) = delete;

    /// Public entry point to tell an operation to do its thing.
    virtual OperationResult execute(ExecutionContext* context, const PXR_NS::JsObject& args);

    /// Get the name of this operation.
    std::string getName() const;

    /// Get the display name of this operation.
    std::string getDisplayName() const;

    /// An optional UI grouping for the operation.
    virtual std::string getDisplayGroup() const;

    /// Get the description of this plugin.
    std::string getDescription() const;

    /// Get the author of this operation.
    virtual std::string getAuthor() const = 0;

    // Get the version of this operation.
    virtual SOPluginVersion getVersion() const = 0;

    // Get the category to use for reporting
    virtual std::string getCategory() const = 0;

    /// Returns whether this operation is visible.
    ///
    /// This function has no effect internally, it is intended for a user interface to be able
    /// to hide an operation. Plugins can override this function to return false if necessary.
    ///
    /// \return Whether the operation is visible to the user interface
    virtual bool getVisible() const;

    /// Returns whether this operation supports analysis mode
    virtual bool getSupportsAnalysis() const;

    /// Get the execution context
    ExecutionContext* getContext() const;

    /// Get the USD Stage that has been set on this operation
    PXR_NS::UsdStageWeakPtr getUsdStage() const;

    /// Get the Report object
    std::shared_ptr<Report> getReport() const;

    /// Get a specific argument.
    const Argument* getArgument(const std::string& name) const;

    /// Get the arguments for this operation.
    std::vector<const Argument*> getArgs() const;

    /// Add an argument to this operation.
    ///
    /// Adds an argument to the operation. A reference to the argument is returned so that any
    /// of the less common settings can be configured afterwards.
    template <typename T>
    Argument& addArgument(const std::string& name,
                          const std::string& displayName,
                          const std::string& displayType,
                          const std::string& description,
                          T& target)
    {
        // Ensure arguments are unique, by name
        Argument* invalidArg = checkArgument(name);
        if (invalidArg != nullptr)
        {
            return *invalidArg;
        }

        PXR_NS::JsValue _defaultValue;
        ArgumentArrayType arrayType = ArgumentArrayType::eNone;

        // Convert vectors to JsArray of JsValue<T>
        if constexpr (is_vector<T>::value)
        {
            PXR_NS::JsArray list;
            list.reserve(target.size());
            for (const auto& val : target)
            {
                list.emplace_back(val);
            }

            // Arguments can have a default value of an empty array. This has no further type
            // info in JSON, so we need to record the actual member type in order to correctly
            // reset the default value.
            if constexpr (std::is_same<T, std::vector<int>>())
            {
                arrayType = ArgumentArrayType::eIntArray;
            }
            else if constexpr (std::is_same<T, std::vector<double>>())
            {
                arrayType = ArgumentArrayType::eDoubleArray;
            }
            else if constexpr (std::is_same<T, std::vector<float>>())
            {
                arrayType = ArgumentArrayType::eFloatArray;
            }
            else if constexpr (std::is_same<T, std::vector<std::string>>())
            {
                arrayType = ArgumentArrayType::eStringArray;
            }
            else
            {
                // Developer error
                CARB_LOG_WARN("Unsupported array type for %s", name.c_str());
                return *invalidArg;
            }

            _defaultValue = list;
        }
        // types with direct JsValue constructors
        else if constexpr (std::is_same<T, bool>::value || std::is_same<T, char const*>::value ||
                           std::is_same<T, std::string>::value)
        {
            _defaultValue = PXR_NS::JsValue(target);
        }
        // floats and doubles are handled as doubles
        else if constexpr (std::is_floating_point<T>::value)
        {
            _defaultValue = PXR_NS::JsValue(static_cast<double>(target));
        }
        // Enums and integrals are handled as ints
        else if constexpr (std::is_enum<T>::value || std::is_integral<T>::value)
        {
            _defaultValue = PXR_NS::JsValue(static_cast<int>(target));
        }
        else
        {
            CARB_LOG_WARN("Unsupported argument type for %s", name.c_str());
            return *invalidArgument();
        }

        auto& arg = addArgument(name, displayName, displayType, description, _defaultValue, &target);

        // JSON only supports double, we need to know later to cast to the correct type
        if constexpr (std::is_same<T, float>() || std::is_same<T, std::vector<float>>())
        {
            arg.setIsFloat(true);
        }

        arg.setArrayType(arrayType);

        return arg;
    }

    /// Create a group of arguments.
    ///
    /// Creates a series of arguments that should be grouped together e.g. in a UI.
    /// The group itself can have its own conditional expression to save having to
    /// specify it per argument.
    ///
    /// Example usage:
    /// \code
    /// addGroup("myGroup",
    ///          addArgument(...),
    ///          addArgument(...)
    /// ).setVisibleIf(...);
    /// \endcode
    ///
    /// Groups will be returned from Operation::getArgs, and will be ordered prior to
    /// their children. You can use Argument::getIsGroup to check if an argument is
    /// a group.
    template <class... Args>
    Group& addGroup(const std::string& name, Args&&... args)
    {
        auto group = new Group(name);
        addArgument(group);

        // As the order of function argument evaluation is not specified, arguments may
        // have been added in a random order. The group is always added last as well, as
        // it happens inside this function - and we want that first. Luckily unrolling the
        // variadic args is in the correct order so we can use that info to sort them after
        // the fact. A bit clunky, but allows us to have much nicer syntax for declaring
        // arguments.
        Argument* afterArg = group;
        (
            [&]
            {
                group->addArgument(&args);
                positionArgument(&args, afterArg);
                afterArg = &args;

                // For nested joins, ensure their children are sorted *after*.
                if (auto _group = dynamic_cast<Group*>(&args))
                {
                    for (auto& _groupArg : _group->getChildren())
                    {
                        positionArgument(_groupArg, afterArg);
                        afterArg = _groupArg;
                    }
                }
            }(),
            ...);

        return *group;
    }

    /// Set custom user data for this operation.
    ///
    /// This function can be used to let operations communicate with each other in a basic
    /// way. The data is operation specific.
    ///
    /// \param userData Plugin-specific user data
    virtual void setUserData(void* userData);


    /// Entry point for the SO_LOG_X macros. Generally you don't need to call this function directly.
    /// @param level The level. See carb::logging::kLevelVerbose etc.
    /// @param fmt The format string
    static void log(int32_t level, const char* fmt, ...);

protected:
    /// Internal entry point for derived operations to begin their logic.
    virtual OperationResult executeImpl() = 0;

    /// Internal entry point for derived operations to perform analysis logic.
    virtual OperationResult executeAnalysisImpl();

    /// Destructor
    virtual ~Operation();

#ifndef DOXYGEN_SHOULD_SKIP_THIS
    template <typename C>
    struct is_vector : std::false_type
    {
    };

    template <typename T, typename A>
    struct is_vector<std::vector<T, A>> : std::true_type
    {
    };
#endif

    /// Adds the given Argument to this Operation
    ///
    /// \note The Operation will take ownership of the Argument after this function has been called
    ///
    /// \return A reference to the added Argument
    Argument& addArgument(Argument* arg);

    /// Create a group of joined arguments.
    ///
    /// Convenience function to use the Join Next functionality to show UI widgets
    /// on the same row.
    ///
    /// May be used within \ref addGroup also.
    template <class... Args>
    Group& addJoin(const std::string& name, const std::string& description, Args&&... args)
    {
        Group& group = addGroup(name, std::forward<Args>(args)...);
        group.setJoinNext(name, description);

        ([&] { args.setJoinNext(name, description); }(), ...);

        return group;
    }

    /// Reposition an argument.
    ///
    /// \param argument The argument to move
    /// \param after The argument to position it after
    void positionArgument(const Argument* argument, const Argument* after);

private:
    class Impl;
    Impl* pImpl;

    Argument* invalidArgument();
    Argument* checkArgument(const std::string& name);

    Argument& addArgument(const std::string& name,
                          const std::string& displayName,
                          const std::string& displayType,
                          const std::string& description,
                          const PXR_NS::JsValue& defaultValue,
                          void* target);

    void resetArgs();
    bool populateExecutionArguments(const PXR_NS::JsObject& args);
};


} // namespace omni::scene::optimizer
