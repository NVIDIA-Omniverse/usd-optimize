# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import os

# try import the core implementation, if we're in a extension then we won't need to add the dll directories since
# they will already be set up, otherwise we need to add them here so the import can find the required dlls
try:
    from pxr import Usd, UsdUtils

    from ._omni_scene_optimizer_impl_core import *
    from ._omni_scene_optimizer_impl_core import _ExecutionContextImpl
except ImportError as error:
    if hasattr(os, "add_dll_directory"):
        script_dir = os.path.dirname(os.path.realpath(__file__))
        lib_dir = os.path.abspath(os.path.join(script_dir, "../../../../../../lib"))
        extralibs_dir = os.path.abspath(os.path.join(script_dir, "../../../../../../extraLibs"))
        with os.add_dll_directory(lib_dir), os.add_dll_directory(extralibs_dir):
            from pxr import Usd, UsdUtils

            from ._omni_scene_optimizer_impl_core import *
            from ._omni_scene_optimizer_impl_core import _ExecutionContextImpl
    else:
        raise error


__all__ = [
    "ExecutionContext",
    "SceneOptimizerCore",
    "SOPluginVersion",
]


class ExecutionContext(object):

    def __init__(self):
        """
        An object describing the context in which a Scene Optimization should be performed.

        Note: The stage to execute on is expected to be in the Usd StageCache so a stage id is required rather than the
              the actual stage. However the help `set_stage` and `remove_stage` functions are provided.

        :param int usdStageId: The id of the stage in the UsdStageCache that the operations should be performed on.
        :param int generateReport: If true, a report will be generated that can be viewed via the Scene Optimizer UI
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
