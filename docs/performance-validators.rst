======================
Performance Validators
======================

When evaluating the data in a USD stage, it can be hard to know which
optimizations would actually help. To support this, Usd Optimize ships a set of
**performance validators**: validation rules that use the operations analysis
mode to inspect a stage and flag potential problems, without modifying it.
Many of the validators also provide suggestions that can be used by
``usd-validation-nvidia`` to automatically fix the discovered issues in the
stage.

The validators integrate with NVIDIA's ``usd-validation-nvidia`` framework.
They live in the ``usd_optimize.validators`` Python package and are registered
as rules under two categories: **Omni:Geometry** and **Usd:Performance**.

Registering and running the validators
---------------------------------------

The rules are discovered through ``importlib.metadata`` entry points, so
installing the ``usd-optimize`` wheel is all that is required. ``nvidia_usd_validate``
and any other ``usd-validation-nvidia`` host then pick them up on import, with no
registration call:

.. code-block:: python

   from usd_validation_nvidia import ValidationEngine
   from pxr import Usd

   engine = ValidationEngine()          # Usd Optimize rules are already registered
   results = engine.validate(Usd.Stage.Open("scene.usd"))

A source checkout registers no entry-point metadata, so ``PYTHONPATH`` alone is
**not** enough there. Register explicitly in that case, importing
``usd_validation_nvidia`` before ``usd_optimize.validators``:

.. code-block:: python

   import usd_validation_nvidia          # import first
   from usd_optimize.validators import register_all, unregister_all

   register_all()                        # idempotent; returns the rules it registered
   ...
   unregister_all()                      # optional, e.g. to isolate tests

Once registered, the rules run like any other ``usd-validation-nvidia`` rule, and
report the affected prims along with the operation that would fix them.

Rule names
----------

Rules are reported as ``UsdOptimize`` plus the class name -- for example
``UsdOptimizeNonManifoldChecker`` -- so they stay attributable and do not collide
with identically named upstream rules. The headings below drop that prefix because
this page is already scoped to Usd Optimize, but the **prefixed** form is what
appears in ``--help``, in the CSV ``rule`` column, and in the parameter names
described next.

Setting parameters
------------------

Many rules expose parameters, listed with each rule below. A parameter can be set
for every rule that defines it, or for one rule only:

``NAME``
   applies to every rule defining a parameter of that name.

``UsdOptimizeSomeChecker.NAME``
   applies to that rule alone, and **overrides** the unqualified form.

Anything left unset keeps the default shown with the rule. From the CLI, pass
``--parameter NAME=VALUE`` (repeatable):

.. code-block:: bash

   nvidia_usd_validate scene.usd \
       --parameter VERBOSE=true \
       --parameter UsdOptimizeNonManifoldChecker.VERBOSE=false

Parallel validation
-------------------

``usd-validation-nvidia`` can spread rules across a process pool with
``--process COUNT``, and pools only rules marked ``@multiprocess_safe``. Every
Usd Optimize rule qualifies, since the marker sits on
``BaseUsdOptimizeChecker.CheckStage`` and no rule overrides it. Unmarked rules
still run inline, so findings are identical either way.

This needs ``usd-validation-nvidia`` >= 1.21.0, which the wheel pins. Against an
older version the marker degrades to a no-op and the rules simply run serially --
no error, just no speedup.

.. Note:: ``--process`` is upstream-experimental and off by default. The gain
   depends on how much of the enabled rule set is poolable, so pair it with
   ``-c Usd:Performance`` / ``-c Omni:Geometry`` rather than running the default
   rule set. On Linux the pool forks, so a worker inherits a dead CUDA context if
   the parent already initialized one, and GPU-backed rules then report
   ``CUDA error: initialization error``; use the serial path for GPU rules and
   when combining with fixing.

.. GENERATED_DOCS_BEGIN - do not edit manually - see tools/repoman/docs_gen.py

Usd:Performance
---------------

These rules check for stage- and scene-level conditions that affect performance.

CoincidingGeometryChecker
^^^^^^^^^^^^^^^^^^^^^^^^^
Finds cases where two or more prims have coinciding geometry that exists within the same world space in the scene. 

**Parameters:** 


- `TOLERANCE`: Tolerance value when comparing points values in world space. Default: `0.001`. 
- `OFFSET`: An offset to allow prims to be considered coincident. Describes a percentage relative to the prim bounds. Default: `0`. 
- `FUZZY`: Find geometry that is the same shape but may have different vertex positions/connectivity. Default: `False`. 

DuplicateGeometryChecker
^^^^^^^^^^^^^^^^^^^^^^^^
Find geometric prims that are duplicates; fixed by creating instances. 

FuzzyDuplicateGeometryChecker
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Finds geometric prims that match within a tolerance. The vertex positions need not match — only the surface shape. 

DuplicateMaterialsChecker
^^^^^^^^^^^^^^^^^^^^^^^^^
Finds duplicate materials; fixed by deduplicating them. 

EmptyLeafChecker
^^^^^^^^^^^^^^^^
Check a stage for redundant leaf primitives (Scopes, Xforms). 

FindOverlappingMeshesChecker
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Reports meshes that overlap one another in the scene. 

FlatHierarchiesChecker
^^^^^^^^^^^^^^^^^^^^^^
Reports prims with a large number of children (a flat hierarchy). 

**Parameters:** 


- `MAX_CHILDREN`: The maximum number of children a prim can have until it is considered a flat hierarchy. Default: `500`. 
- `CONSIDER_ALL_CHILDREN`: Whether to consider all children or only active, loaded, defined, non-abstract children. Default: `True`. 

HighVertexCountChecker
^^^^^^^^^^^^^^^^^^^^^^
Check a stage for meshes with high or extreme vertex counts. 

**Parameters:** 


- `LEVEL_HIGH`: Consider prims with this many vertices to have a high vertex count. Default: `100000`. 
- `LEVEL_VERY_HIGH`: Consider prims with this many vertices to have a very high vertex count. Default: `500000`. 
- `LEVEL_EXTREME`: Consider prims with this many vertices to have an extreme vertex count. Default: `1000000`. 

InvisiblePrimsChecker
^^^^^^^^^^^^^^^^^^^^^
Finds invisible prims in the stage that can be deactivated instead. 

NormalsChecker
^^^^^^^^^^^^^^
Checks mesh prims for normals aligned to face orientation. 

Returns all prims with normals not aligned with face winding order as a single warning, with an option to fix using Usd Optimize operations. 

OccludedMeshesChecker
^^^^^^^^^^^^^^^^^^^^^
Uses Usd Optimize to analyze a scene checking for occluded meshes. 

**Parameters:** 


- `USE_GPU`: Choose whether to use GPU or CPU algorithm. Default: `True`. 
- `CHECK_TRANSPARENCY`: Exclude meshes with opacity < 1.0 from occlusion testing. Default: `True`. 
- `CLUSTERED`: Split the stage into clusters of meshes with overlapping bounding boxes and check visibility per cluster, improving both accuracy and performance by reducing the number of meshes compared at the same time. Default: `True`. 
- `MINIMUM_GAP_SIZE`: The minimum gap size corresponding to the spacing of the background grid. Gaps smaller than this value are considered closed for occlusion culling. The actual grid spacing is max(minimumGapSize, maxDim/maximumGridResolution). Very small values defer to maximumGridResolution for spacing, producing a finer grid that detects smaller gaps and results in fewer meshes being flagged as occluded. It is essentially a tolerance for how sealed an enclosure needs to be: e.g. a value of 3.5 means ignore any opening smaller than 3.5 scene units when deciding if something is hidden. Default: `0.01`. 
- `MAXIMUM_GRID_RESOLUTION`: The maximum number of cells along the longest axis of the grid used for visibility checking. This caps the grid resolution to prevent excessive memory and compute costs (the grid is 3D, so memory scales with the cube of resolution). A value of 500 is suitable for powerful GPUs, use smaller values for less powerful GPUs or CPUs. Default: `500`. 

PrimitiveFitChecker
^^^^^^^^^^^^^^^^^^^
Check mesh prims that could be replaced with a USD primitive prim, with an option to apply the fix from a Usd Optimize operation. 

**Parameters:** 


- `GPU_FACE_COUNT_THRESHOLD`: For meshes with at least this many faces, use GPU algorithm.  A value of zero forces CPU. Default: `0`. 
- `VERTEX_TOLERANCE`: Relative tolerance of RMS distance from fit vertices to primitive surface. Default: `0.01`. 
- `VOLUME_TOLERANCE`: Relative tolerance of volume between faces and the fitting primitive. Default: `0.01`. 
- `IGNORE_SUBSETS`: If set, a mesh with subsets is allowed to be fit.  If replaced by a primitive, any subsets will be lost. Default: `True`. 
- `ALLOW_NEGATIVE_VOLUME`: If set, a mesh with negative volume (inward-pointing normals) is allowed to be fit. Default: `True`. 
- `ALLOW_MISSING_ENDCAPS`: If set, a cylinder, cone, or box mesh without endcaps is allowed to be fit. Default: `True`. 

RedundantTimeSamplesChecker
^^^^^^^^^^^^^^^^^^^^^^^^^^^
Uses Usd Optimize to analyze a scene checking for redundant time samples. 

**Parameters:** 


- `EPSILON_DOUBLE`: Threshold for which to consider double numbers equal. Default: `1e-12`. 
- `EPSILON_FLOAT`: Threshold for which to consider floating point numbers equal. Default: `1e-06`. 

RtxMeshCountChecker
^^^^^^^^^^^^^^^^^^^
Check if the number of RTX meshes exceeds recommended limits. 

**Parameters:** 


- `RTX_UNIQUE_MESH_COUNT_LIMIT`: The recommended limit for the number of unique RTX meshes in a scene. If the number of unique RTX meshes exceeds this limit, a warning will be issued. Default: `438000`. 

SmallMeshChecker
^^^^^^^^^^^^^^^^
Uses Usd Optimize to analyze a scene checking for meshes with extents below a configurable size threshold. 

**Parameters:** 


- `SIZE_THRESHOLD`: The minimum extent size a mesh can have before it is considered small. Default: `0.001`. 

SparseMeshChecker
^^^^^^^^^^^^^^^^^
Finds mesh prims that are considered sparse, this can be due to density of the geometry volume in relation to the extent volume, or prims with many sparse disjoint meshes. 

These prims will be either identified as needing to be diced, split, or clustered together with other similar sparse meshes within the scene. 

UnusedUVsChecker
^^^^^^^^^^^^^^^^
Check a stage for unused texture coordinate primvars. 

WindingsChecker
^^^^^^^^^^^^^^^
Finds and fixes meshes with inconsistent windings. 

ZeroExtentChecker
^^^^^^^^^^^^^^^^^
Uses Usd Optimize to analyze a scene checking for geometry that has zero sized extents. 

Omni:Geometry
-------------

These rules check for low-level geometric defects on meshes. Most are fixed with the :doc:`Mesh Cleanup<operations/meshCleanup>` operation.

ColocatedVerticesChecker
^^^^^^^^^^^^^^^^^^^^^^^^
Check mesh prims for colocated vertices, returns all prims with colocated vertices as a single warning with an option to fix via a Usd Optimize operation. 

**Parameters:** 


- `TOLERANCE`: The tolerance (distance) apart for vertices to be considered equal. Default: `0`. 

DuplicateFaceChecker
^^^^^^^^^^^^^^^^^^^^
Check mesh prims for duplicate faces, returns all prims as a single warning with an option to fix via a Usd Optimize operation. 

IndexedPrimvarChecker
^^^^^^^^^^^^^^^^^^^^^
For Primvars with non-constant values of interpolation, it is often the case that the same value is repeated many times in the array. 

An indexed primvar can be used in such cases to optimize for data storage if the primvar's interpolation is non-constant (i.e. uniform, varying, face varying or vertex). 

This Checker also looks for indexed primvars whose indices are out of bounds, or use a non-constant interpolation but do not contain array-type data. 

IsolatedVerticesChecker
^^^^^^^^^^^^^^^^^^^^^^^
Check mesh prims for isolated vertices, returns all prims as a single warning with an option to fix via a Usd Optimize operation. 

NonManifoldChecker
^^^^^^^^^^^^^^^^^^
Check mesh prims for non-manifold geometry, returns all non-manifold prims as a single warning with an option to fix via a Usd Optimize operation. 

ZeroAreaFacesChecker
^^^^^^^^^^^^^^^^^^^^
Check mesh prims for any zero area faces, returns all prims that have zero area faces as a single warning with an option to fix using a Usd Optimize operation. 

.. GENERATED_DOCS_END
