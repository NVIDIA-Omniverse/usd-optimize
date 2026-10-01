# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import glob
import os
import sys

# Handles returned by os.add_dll_directory. Kept at module scope so the directories
# remain on the DLL search path for the lifetime of the process.
_dll_directories = []

_EXTENSION_STEM = "_usd_optimize_impl_core"
_MODULE_SUFFIXES = (".pyd", ".so")


def _abi_error(shipped, expected):
    """Why the `shipped` extension suffixes cannot be imported by an interpreter
    whose own tagged suffix is `expected`, or None if they can."""
    if not shipped:
        return "no extension module found"
    if expected in shipped:
        return None
    if all(suffix in _MODULE_SUFFIXES for suffix in shipped):
        return "extension module carries no ABI tag"
    return "built for {}, but this interpreter expects {}".format(", ".join(sorted(shipped)), expected)


def _check_python_abi(directory=None):
    """Raise on an interpreter/ABI mismatch before the import turns it into a
    misleading ``ModuleNotFoundError``.

    The Linux drop bundles an exact-match ``libpython3.X.so.1.0``, so the loader
    resolves it under the wrong interpreter and nothing objects. The expected ABI
    is read from the shipped filename, never hardcoded. `directory` is for tests.
    """
    import importlib.machinery
    import warnings

    if directory is None:
        directory = os.path.dirname(os.path.realpath(__file__))
    shipped = [
        os.path.basename(path)[len(_EXTENSION_STEM) :]
        for path in glob.glob(os.path.join(directory, _EXTENSION_STEM + ".*"))
        if path.endswith(_MODULE_SUFFIXES)
    ]
    reason = _abi_error(shipped, importlib.machinery.EXTENSION_SUFFIXES[0])
    if reason is None:
        return

    if not shipped or "no ABI tag" in reason:
        # Nothing to compare: warn, but let the import raise its own error.
        warnings.warn(
            "Cannot verify the Python ABI of {}: {}.".format(_EXTENSION_STEM, reason),
            ImportWarning,
            stacklevel=2,
        )
        return

    raise ImportError(
        "{} {}. Running Python {}. Use an interpreter matching the py_<version> "
        "token in the package name.".format(_EXTENSION_STEM, reason, sys.version.split()[0])
    )


def _setup_windows_dll_dirs():
    """Prepare the native libraries so the extension module AND the operation
    plugins (dlopen'd later by the C++ core) can be loaded.

    Two problems have to be solved on Windows:

    1. The extension module and plugins need their dependency directories on the
       loader search path. We add them via ``os.add_dll_directory`` (persistently
       — see ``_dll_directories``).
    2. ``os.add_dll_directory`` only affects loads that opt into
       ``LOAD_LIBRARY_SEARCH_USER_DIRS``. The C++ core dlopen's each plugin with a
       plain ``LoadLibrary``, whose dependency resolution ignores those dirs. So we
       also *preload* the first-party libraries from ``usd_optimize.libs``: once a
       DLL of a given basename is resident, every later ``LoadLibrary`` reuses it by
       name regardless of search flags, so a plugin's first-party dependencies
       (``usd_optimize.core.dll`` etc., shipped unmangled — the wheel is not
       delvewheel-repaired) resolve even though its own dependency directory is
       not searched.
    """
    script_dir = os.path.dirname(os.path.realpath(__file__))
    # Candidate directories holding the native libraries, covering two layouts:
    #   * build tree: <config>/python/usd_optimize/impl/core with lib/ and
    #     extraLibs/ as siblings of python/ (four parents up).
    #   * installed wheel: site-packages/usd_optimize/impl/core with the libs
    #     bundled in site-packages/usd_optimize.libs (three parents up).
    # On Windows the USD DLLs are excluded from our wheel and supplied by the
    # usd-exchange dependency, whose libraries live in the sibling
    # site-packages/usd_exchange.libs — add it so the USD imports resolve.
    # Only directories that actually exist are added — os.add_dll_directory
    # raises FileNotFoundError on a missing path.
    wheel_libs = os.path.abspath(os.path.join(script_dir, "../../../usd_optimize.libs"))
    candidate_dirs = [
        os.path.abspath(os.path.join(script_dir, "../../../../lib")),
        os.path.abspath(os.path.join(script_dir, "../../../../extraLibs")),
        wheel_libs,
        os.path.abspath(os.path.join(script_dir, "../../../usd_exchange.libs")),
    ]
    for candidate in candidate_dirs:
        if os.path.isdir(candidate):
            _dll_directories.append(os.add_dll_directory(candidate))

    # Preload the first-party shared libraries (installed-wheel layout only) so they
    # are resident before the C++ core dlopen's the operation plugins that depend on
    # them. Best-effort: ctypes honours the directories added above to resolve each
    # library's own dependencies (USD from usd_exchange.libs, etc.).
    if os.path.isdir(wheel_libs):
        import ctypes

        for dll in glob.glob(os.path.join(wheel_libs, "*.dll")):
            try:
                ctypes.WinDLL(dll)
            except OSError:
                pass


# Before the import below, which would otherwise report a missing module.
_check_python_abi()

# try import the core implementation, if we're in a extension then we won't need to add the dll directories since
# they will already be set up, otherwise we need to add them here so the import can find the required dlls
try:
    from pxr import Usd, UsdUtils

    from ._usd_optimize_impl_core import *
    from ._usd_optimize_impl_core import _ExecutionContextImpl
except ImportError as error:
    if hasattr(os, "add_dll_directory"):
        _setup_windows_dll_dirs()

        from pxr import Usd, UsdUtils

        from ._usd_optimize_impl_core import *
        from ._usd_optimize_impl_core import _ExecutionContextImpl
    else:
        raise error


__all__ = [
    "ExecutionContext",
    "UsdOptimizeCore",
    "UsdOptimizePluginVersion",
]


# Deprecated class-name alias. Kept out of __all__ so `from impl.core import *`
# (used by usd_optimize/core/__init__.py) doesn't fire the warning for every
# importer — only direct attribute access through this module does.
def __getattr__(name):
    if name == "SceneOptimizerCore":
        import warnings

        warnings.warn(
            "`SceneOptimizerCore` is deprecated; use `UsdOptimizeCore` instead.",
            DeprecationWarning,
            stacklevel=2,
        )
        return UsdOptimizeCore  # noqa: F405
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")


class ExecutionContext(object):

    def __init__(self):
        """
        An object describing the context in which a Scene Optimization should be performed.

        Note: The stage to execute on is expected to be in the Usd StageCache so a stage id is required rather than the
              the actual stage. However the help `set_stage` and `remove_stage` functions are provided.

        :param int usdStageId: The id of the stage in the UsdStageCache that the operations should be performed on.
        :param int generateReport: If true, a report will be generated that can be viewed via the Usd Optimize UI
        :param int verbose: If true, log extended information (may result in slower performance)
        :param int singleThreaded: If true, run operation single threaded
        :param int captureStats: If true, capture and report on the contents of the stage before and after the operations run
        :param str reportPath: File path where the report will be written, if undefined a path will be generated on execute
        """
        self.__impl = _ExecutionContextImpl()
        self.__stage_cached = False

    @property
    def _impl(self):
        return self.__impl

    def set_stage(self, stage):
        """
        Takes the given stage and checks whether it is in the Usd StageCache and if not add its to the cache, then sets
        the usdStageId attribute.

        If the stage is added to the cache, then it will need to be removed from the cache later, `remove_stage` can be
        used for this.

        :return Whether the stage was added to the cache.
        """
        self.__stage_cached = False
        # first attempt to get stage id from the cache if its already cached
        self.__impl.usdStageId = UsdUtils.StageCache.Get().GetId(stage).ToLongInt()
        # not cached? Then cache ourselves
        if self.__impl.usdStageId == -1:
            self.__impl.usdStageId = UsdUtils.StageCache.Get().Insert(stage).ToLongInt()
            self.__stage_cached = True
        return self.__stage_cached

    def remove_stage(self):
        """
        If this ExecutionContext cached a stage in the `set_stage` function, then this will remove it from the Usd
        StageCache, otherwise it does nothing.

        Warning: Removing a stage from the cache will invalidate the original Python reference (unless there is another
                 strong reference to it).
        """
        if self.__stage_cached:
            UsdUtils.StageCache.Get().Erase(Usd.StageCache.Id.FromLongInt(self.__impl.usdStageId))
            self.__impl.usdStageId = -1
            self.__stage_cached = False

    def __getattribute__(self, name):
        # we manually override getting attributes so that anything that we don't have defined on the ExecutionContext
        # object itself will be forwarded to the underlying _ExecutionContextImpl object
        if name == "_impl":
            return super().__getattribute__("_ExecutionContext__impl")
        if name in ["set_stage", "remove_stage", "_ExecutionContext__impl", "_ExecutionContext__stage_cached"]:
            return super().__getattribute__(name)
        return self.__impl.__getattribute__(name)

    def __setattr__(self, name, value):
        # we manually override setting attributes so that anything that we don't have defined on the ExecutionContext
        # object itself will be forwarded to the underlying _ExecutionContextImpl object
        if name in ["_ExecutionContext__impl", "_ExecutionContext__stage_cached"]:
            return super().__setattr__(name, value)
        return self.__impl.__setattr__(name, value)
