# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


# expose the implementation as the API
from omni.scene.optimizer.impl.core import *

# Force the C++ core's lazy plugin load (which ``PyImport_Import``s the
# Python-implemented operations under
# ``omniverse_scene_optimizer.libs/operations/<name>/``) to happen on the
# importing thread, before any caller can dispatch ``executeOperation`` from
# a worker thread.
#
# The binding's ``getInstance`` wrapper guards ``loadPlugins`` with
# ``std::call_once``, but the imports inside ``loadPlugins`` release the GIL
# during file I/O, which lets other threads enter Python's import machinery
# for the same modules. Combined with CPython's per-module import lock
# that's a textbook GIL/import-lock deadlock — observed when
# ``asset-validator``'s ``AsyncComplianceCheckerRunner`` dispatched
# ``CheckStage`` rules to a multi-worker ``ThreadPoolExecutor``.
#
# Triggering ``getInstance()`` here serialises the plugin imports on the
# single thread that's importing this package (Python's import machinery
# already serialises this), so by the time any subsequent caller (worker
# thread or otherwise) reaches the binding the singleton is fully
# initialised and ``call_once`` short-circuits.
SceneOptimizerCore.getInstance()  # noqa: F405
