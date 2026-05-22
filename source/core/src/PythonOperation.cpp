// SPDX-FileCopyrightText: Copyright (c) 2020-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/PythonOperation.h"

// Carbonite
#include <carb/profiler/Profile.h>


namespace omni::scene::optimizer
{

constexpr const char* s_category = "PYTHON";

PythonOperation::PythonOperation(const std::string& name,
                                 const std::string& displayName,
                                 const std::string& description,
                                 PyObject* pyObject)
    : Operation(name, displayName, description)
    , m_pyObject(pyObject)
    , m_visible(true)
{
    // get the author of the plugin
    static const std::string kAuthorAttribute = "author";
    try
    {
        m_author = _pyAsString(_pyGetObjectAttribute(m_pyObject.obj, kAuthorAttribute).obj);
    }
    catch (const std::exception& exc)
    {
        throw std::runtime_error("Error getting \"" + kAuthorAttribute + "\" attribute: " + std::string(exc.what()));
    }

    /// get the version of the plugin
    static const std::string kVersionAttribute = "version";
    RefCountedPyObject pyVersion = nullptr;
    try
    {
        pyVersion = _pyGetObjectAttribute(m_pyObject.obj, kVersionAttribute);
    }
    catch (const std::exception& exc)
    {
        throw std::runtime_error("Error getting \"" + kVersionAttribute + "\" attribute: " + std::string(exc.what()));
    }

    if (!PyTuple_Check(pyVersion.obj))
    {
        throw std::runtime_error("\"" + kVersionAttribute + "\" must return a tuple");
    }

    if (PyTuple_Size(pyVersion.obj) != 3)
    {
        throw std::runtime_error("\"" + kVersionAttribute + "\" must return a tuple of 3 ints");
    }

    try
    {
        m_version.major = _pyAsInt(PyTuple_GetItem(pyVersion.obj, 0));
        m_version.minor = _pyAsInt(PyTuple_GetItem(pyVersion.obj, 1));
        m_version.rev = _pyAsInt(PyTuple_GetItem(pyVersion.obj, 2));
    }
    catch (const std::exception&)
    {
        throw std::runtime_error("\"" + kVersionAttribute + "\" must return a tuple of 3 ints");
    }

    // get the visibility of the plugin
    static const std::string kVisibleAttribute = "visible";
    try
    {
        m_visible = _pyAsBool(_pyGetObjectAttribute(m_pyObject.obj, kVisibleAttribute).obj);
    }
    catch (const std::exception& exc)
    {
        throw std::runtime_error("Error getting \"" + kVisibleAttribute + "\" attribute: " + std::string(exc.what()));
    }

    std::string pyErrorMessage;

    // get the arguments from the python object
    static const std::string kSerializedArgsFuncName = "_serialize_arguments";
    ScopedPyObject pySerializedArgsFuncName = PyUnicode_FromString(kSerializedArgsFuncName.c_str());
    ScopedPyObject pyArgList = PyObject_CallMethodNoArgs(m_pyObject.obj, pySerializedArgsFuncName.obj);

    if (_pyCheckAndGetExceptionMessage(pyErrorMessage))
    {
        throw std::runtime_error("Error calling \"" + kSerializedArgsFuncName + "\" function: " + pyErrorMessage);
    }

    if (!PyList_Check(pyArgList.obj))
    {
        throw std::runtime_error("\"" + kSerializedArgsFuncName + "\" function did not return a list");
    }

    Py_ssize_t nArg = PyList_Size(pyArgList.obj);
    for (Py_ssize_t i = 0; i < nArg; ++i)
    {
        try
        {
            PyObject* pyArg = PyList_GetItem(pyArgList.obj, i);
            const std::string argJsonStr = _pyAsString(pyArg);
            Argument& arg = addArgument(Argument::deserialize(argJsonStr));
            m_argDefaults[arg.getName()] = arg.getDefaultValue();
        }
        catch (const std::exception& exc)
        {
            throw std::runtime_error("Failed to deserialize argument at " + std::to_string(i) + ": " + exc.what());
        }
    }
}


PythonOperation::~PythonOperation()
{
}


std::string PythonOperation::getAuthor() const
{
    return m_author;
}


SOPluginVersion PythonOperation::getVersion() const
{
    return m_version;
}


std::string PythonOperation::getCategory() const
{
    return getName();
}


bool PythonOperation::getVisible() const
{
    return m_visible;
}


OperationResult PythonOperation::execute(ExecutionContext* context, const PXR_NS::JsObject& args)
{
    // get the stage id from the context to pass through to python
    auto stageId = context->usdStageId;

    // merge default args with the execution args
    PXR_NS::JsObject mergedArgs = args;
    for (const auto& argDefault : m_argDefaults)
    {
        // Insert the default arg.
        // The insert will not proceed if the arg already exists, so we won't overwrite
        // any of the actual args.
        mergedArgs.insert(std::make_pair(argDefault.first, argDefault.second));
    }

    // dump json args to string so we can pass them to python
    std::string const argJsonStr = JsWriteToString(mergedArgs);

    // don't start the profile zone until we actually call the python operation
    CARB_PROFILE_ZONE(0, "SceneOptimizer|PythonOperation|Execute");

    // call execute on the python object
    ScopedPyObject pyExecuteRet = PyObject_CallMethod(m_pyObject.obj, "_execute", "(is)", stageId, argJsonStr.c_str());

    // did an exception occur during execute?
    std::string pyErrorMessage;
    if (_pyCheckAndGetExceptionMessage(pyErrorMessage))
    {
        CARB_LOG_ERROR("%s: %s", getName().c_str(), pyErrorMessage.c_str());
        return { false, nullptr, nullptr };
    }

    // handle the return value from execute
    if (!PyBool_Check(pyExecuteRet.obj))
    {
        CARB_LOG_ERROR("%s: Return value from execute must be a bool", getName().c_str());
        return { false, nullptr, nullptr };
    }

    // TODO: Add error/output from python object
    return { pyExecuteRet.obj == Py_True, nullptr, nullptr };
}

// LCOV_EXCL_START
OperationResult PythonOperation::executeImpl()
{
    // unused
    return { true, nullptr, nullptr };
}
// LCOV_EXCL_STOP


} // namespace omni::scene::optimizer
