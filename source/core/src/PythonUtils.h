// SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0
//

#pragma once

// Scene Optimizer Core
#include "omni/scene.optimizer/core/Defs.h"

// Python
// Note: we include python via USD/UsdIncludes because it deals with issues including
// that header on windows. Don't include Python.h directly unless you enjoy suffering
#include "omni/scene.optimizer/core/UsdIncludes.h"


namespace omni::scene::optimizer
{


/// A simple RAII wrapper for a PyObject* that decrements the reference count when it goes out of scope.
class ScopedPyObject
{
public:
    PyObject* obj;

    ScopedPyObject()
        : obj(nullptr)
    {
    }

    ScopedPyObject(PyObject* pyObject)
        : obj(pyObject)
    {
    }

    ScopedPyObject(const ScopedPyObject&) = delete;

    ScopedPyObject& operator=(const ScopedPyObject&) = delete;

    ~ScopedPyObject()
    {
        Py_XDECREF(obj);
    }
};


/// A simple RAII wrapper for a PyObject* that increments the reference count when it is constructed and decrements
/// the reference count when it goes out of scope.
class RefCountedPyObject
{
public:
    PyObject* obj;

    RefCountedPyObject()
        : obj(nullptr)
    {
    }

    RefCountedPyObject(PyObject* pyObject)
        : obj(pyObject)
    {
        Py_XINCREF(obj);
    }

    RefCountedPyObject(const RefCountedPyObject& other)
        : obj(other.obj)
    {
        Py_XINCREF(obj);
    }

    RefCountedPyObject& operator=(PyObject* other)
    {
        if (obj != other)
        {
            Py_XDECREF(obj);
            obj = other;
            Py_XINCREF(obj);
        }
        return *this;
    }

    RefCountedPyObject& operator=(const RefCountedPyObject& other)
    {
        if (this != &other)
        {
            Py_XDECREF(obj);
            obj = other.obj;
            Py_XINCREF(obj);
        }
        return *this;
    }

    ~RefCountedPyObject()
    {
        // I don't love this.... but on Windows when running Scene Optimizer from standalone Python, the Python
        // Interpreter is shut down before the static SceneOptimizerCore singleton is destroyed. That means that by the
        // time the destructor for RefCountedPyObject is called Python may already be finalized and our objects deleted.
        // So calling Py_XDECREF causes a crash.
        if (Py_IsInitialized())
        {
            Py_XDECREF(obj);
        }
    }
};


/// Checks if a Python exception has been raised and, if so, raises a std::runtime_error with the message of the python
/// exception.
///
/// \note This clears the raised Python exception.
OMNI_SO_EXPORT
void _pyCheckAndRaiseException();


/// Checks if a Python exception has been raised and, if so, returns the message and stacktrace of the python exception.
///
/// \note This clears the raised Python exception.
///
/// \param message Returns the message of the python exception if one has occurred.
///
/// \return True if a Python exception has occurred, false otherwise.
OMNI_SO_EXPORT
bool _pyCheckAndGetExceptionMessage(std::string& message);


/// Attempts to get named attribute from a Python object
///
/// \param pyObject The Python object to retrieve the attribute from
/// \param attributeName The name of the attribute to retrieve
///
/// \throws std::runtime_error if the attribute does not exist or retrieving the attribute raises an exception
OMNI_SO_EXPORT
RefCountedPyObject _pyGetObjectAttribute(PyObject* pyObject, const std::string& attributeName);


/// Attempts to return the given python object as a boolean
///
/// \throws std::runtime_error if the object is not a boolean
OMNI_SO_EXPORT
bool _pyAsBool(PyObject* pyObject);


/// Attempts to return the given python object as an int
///
/// \throws std::runtime_error if the object is not an int
OMNI_SO_EXPORT
int _pyAsInt(PyObject* pyObject);


/// Attempts to return the given python object as a std::string.
///
/// \throws std::runtime_error if the object is not a Python unicode string
OMNI_SO_EXPORT
std::string _pyAsString(PyObject* pyObject);


} // namespace omni::scene::optimizer
