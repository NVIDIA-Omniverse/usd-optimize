// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#include "omni/scene.optimizer/core/PythonUtils.h"


namespace omni::scene::optimizer
{


void _pyCheckAndRaiseException()
{
    if (!PyErr_Occurred())
    {
        return;
    }

    ScopedPyObject ptype;
    ScopedPyObject pvalue;
    ScopedPyObject ptraceback;
    PyErr_Fetch(&ptype.obj, &pvalue.obj, &ptraceback.obj);

    ScopedPyObject ptypeStr = PyObject_Str(ptype.obj);
    ScopedPyObject pvalueStr = PyObject_Str(pvalue.obj);

    std::string message = "Python exception raised: " + std::string(PyUnicode_AsUTF8(ptypeStr.obj)) + ": " +
                          std::string(PyUnicode_AsUTF8(pvalueStr.obj));
    throw std::runtime_error(message);
}


bool _pyCheckAndGetExceptionMessage(std::string& message)
{
    if (!PyErr_Occurred())
    {
        return false;
    }

    // get the exception information
    ScopedPyObject ptype;
    ScopedPyObject pvalue;
    ScopedPyObject ptraceback;
    PyErr_Fetch(&ptype.obj, &pvalue.obj, &ptraceback.obj);
    PyErr_NormalizeException(&ptype.obj, &pvalue.obj, &ptraceback.obj);
    ScopedPyObject ptypeStr = PyObject_Str(ptype.obj);
    ScopedPyObject pvalueStr = PyObject_Str(pvalue.obj);

    // format an initial message to return, this however will be overwritten if we can get a traceback
    message = std::string(PyUnicode_AsUTF8(ptypeStr.obj)) + ": " + std::string(PyUnicode_AsUTF8(pvalueStr.obj));

    // attempt to get the traceback module to format the exception and traceback
    ScopedPyObject tbModuleName = PyUnicode_FromString("traceback");
    ScopedPyObject tbModule = PyImport_Import(tbModuleName.obj);
    if (tbModule.obj == nullptr)
    {
        return true;
    }

    // acquire the format_exception function from the traceback module
    ScopedPyObject formatFunction = PyObject_GetAttrString(tbModule.obj, "format_exception");
    if (formatFunction.obj && PyCallable_Check(formatFunction.obj))
    {
        // format the exception and traceback
        ScopedPyObject pyFormatted =
            PyObject_CallFunctionObjArgs(formatFunction.obj, ptype.obj, pvalue.obj, ptraceback.obj, nullptr);

        // traceback.format_exception shouldn't fail, but check for an exception just in case
        if (PyErr_Occurred())
        {
            PyErr_Clear();
            return true;
        }

        // if we have a formatted exception, iterate through the list items and return as the message
        if (pyFormatted.obj != nullptr && PyList_Check(pyFormatted.obj) && PyList_Size(pyFormatted.obj) > 0)
        {
            for (Py_ssize_t i = 0; i < PyList_Size(pyFormatted.obj); ++i)
            {
                if (!message.empty())
                {
                    message += "\n";
                }
                message += PyUnicode_AsUTF8(PyList_GetItem(pyFormatted.obj, i));
            }
        }
    }
    return true;
}


RefCountedPyObject _pyGetObjectAttribute(PyObject* pyObject, const std::string& attributeName)
{
    ScopedPyObject pyAttribute = PyObject_GetAttrString(pyObject, attributeName.c_str());
    // check if an exception occurred
    _pyCheckAndRaiseException();
    // check if the attribute actually exists on the object
    if (pyAttribute.obj == nullptr)
    {
        throw std::runtime_error("Object does not have attribute: " + attributeName); // LCOV_EXCL_LINE
    }
    return RefCountedPyObject(pyAttribute.obj);
}


bool _pyAsBool(PyObject* pyObject)
{
    if (!PyBool_Check(pyObject))
    {
        throw std::runtime_error("Not a boolean"); // LCOV_EXCL_LINE
    }
    return pyObject == Py_True;
}


int _pyAsInt(PyObject* pyObject)
{
    if (!PyLong_Check(pyObject))
    {
        throw std::runtime_error("Not an int");
    }
    return (int)PyLong_AsLong(pyObject);
}


std::string _pyAsString(PyObject* pyObject)
{
    if (!PyUnicode_Check(pyObject))
    {
        throw std::runtime_error("Not a string");
    }
    return std::string(PyUnicode_AsUTF8(pyObject));
}


} // namespace omni::scene::optimizer
