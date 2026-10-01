Overview
========

**Usd Optimize** is a standalone C++ and Python library that performs scene
optimization at the `OpenUSD <https://openusd.org/>`_ level. It provides a broad
set of operations for processing and optimizing USD stages: reducing memory
usage, improving load, times, lowering prim and mesh counts, and cleaning up
geometry and materials. This allows complex scenes to be converted into more
lightweight representations that, load, evaluate, and render more quickly.

Key Concepts
------------

Operations
    Each optimization is an *operation*: a self-contained unit identified by a
    string *key* (for example ``meshCleanup``, ``decimateMeshes``,
    ``deduplicateGeometry``). Operations declare typed arguments that control
    their behavior.
    See :doc:`operations` for the full catalog.

Plugin architecture
    Operations are implemented as plugins that subclass the C++
    ``usd_optimize::Operation`` base class and register themselves with the core
    library. The set of operations is therefore extensible. See the plugin
    authoring guide (``PLUGINS.md``) in the repository for details.

Analysis mode
    Many operations support an *analysis* mode that inspects a stage and reports
    what an optimization *would* do, without modifying the stage. Analysis mode
    is the foundation of the :doc:`performance-validators`.

Configuration stacks
    Operations can be run individually or chained together into a *stack* (a JSON
    array of operations) that is applied to a stage in order. This makes it easy
    to build, save, and share reusable optimization pipelines.

Using the Library
-----------------

There are several ways to drive Usd Optimize. The Python entry points are the
quickest way to get started:

.. code-block:: python

   from usd_optimize.core import ExecutionContext, UsdOptimizeCore
   from pxr import Usd

   stage = Usd.Stage.Open("path/to/asset.usd")

   context = ExecutionContext()
   context.set_stage(stage)

   success, error, output = UsdOptimizeCore.getInstance().executeOperation(
       "meshCleanup",
       context,
       {"mergeVertices": True, "removeIsolatedVertices": True},
   )

   if not success:
       raise RuntimeError(f"meshCleanup failed: {error}")

   stage.GetRootLayer().Save()

A list of operations can be applied in a single call (a stack):

.. code-block:: python

   config = [
       {"operation": "meshCleanup", "mergeVertices": True},
       {"operation": "decimateMeshes", "maxMeanError": 0.01, "pinBoundaries": True},
   ]
   results = UsdOptimizeCore.getInstance().executeConfig(context, config)


The C++ public API in ``include/usd_optimize/core/Core.h`` exposes the same
capabilities for native callers; see the :doc:`../api/api` documentation.

Where to Go Next
----------------

* :doc:`operations` — the complete catalog of optimization operations and
  their arguments, with JSON configuration examples.
* :doc:`choosing-operations` — guidance on *which* operations to apply for a
  given problem (memory, interactive performance, load time).
* :doc:`performance-validators` — validation rules that analyze a stage and
  flag the optimizations that would benefit it.
* :doc:`python` — the Python API reference.
* :doc:`../api/api` — the C++ API reference.
* :doc:`developer` — how to consume the published Usd Optimize package from
  your own build.
