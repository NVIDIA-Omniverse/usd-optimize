.. AUTO GENERATED FILE - DO NOT EDIT

===============
Decimate Meshes
===============

**Key**: ``decimateMeshes``

Reduce the polygon count of ``UsdGeom.Mesh`` prims while preserving shape as much
as possible. Decimation uses QEM (Quadric Error Metrics) edge-collapse simplification: edges are collapsed
in order of least geometric error and the mesh is locally re-triangulated after each collapse. CPU
(parallel and sequential) and GPU paths are selected automatically based on vertex count thresholds.

Output mesh structure
---------------------

Each targeted mesh goes through three steps; the first two always run:

1. Colocated vertices are merged and degenerate faces are removed.
2. The mesh is triangulated.
3. If the ``reductionFactor`` or ``maxMeanError`` requires decimation, edges are collapsed until a stop
   condition is reached. ``reductionFactor`` applies to the vertex count after step 1.

As a result:

- Output is always triangles.
- Step 1 means that the vertex count can drop even if no decimation is performed.
- Step 2 doubles a quad mesh's face count, so face count can stay level or even rise as vertices fall.
- Flat or symmetric regions have many edges of equal cost, so small differences in input locations or
  numbering can change the result.

Choosing a stop condition
--------------------------

``reductionFactor`` and ``maxMeanError`` are the two stop conditions; either can be used alone or together
(whichever is reached first stops decimation of a given mesh). Set one to ``0.0`` to disable it.

- **Prefer an error bound for silhouette-preserving decimation.** ``maxMeanError`` bounds the geometric
  error, so the decimator stops before visible features are lost. This is the recommended default: set a
  non-zero ``maxMeanError`` and ``reductionFactor`` to ``0.0``.
- **Use a reduction rate only to hit a fixed target** (e.g. a memory budget or a fixed LOD level).
  ``reductionFactor`` is a percentage in the range 0-100, **not** a fraction: ``50`` keeps 50% of the
  vertices, while ``0.5`` keeps 0.5% and destroys the mesh. Values below 10 typically ruin the silhouette.

Use float literals for these float arguments; some bindings reject an integer ``0``.

Scale and units
---------------

``maxMeanError`` is the maximum mean geometric distance (in **stage units**) the decimated surface may
drift from the original. To target a physical tolerance, convert from millimetres using the stage's
``metersPerUnit``::

    maxMeanError = (tolerance_mm / 1000) / metersPerUnit

``UsdGeom.GetStageMetersPerUnit`` returns USD's default of ``0.01`` (centimetres) when the metadata is
unset, so a tolerance-sensitive config depends on the stage being authored at a known scale.

Important defaults and footguns
-------------------------------

- ``pinBoundaries`` defaults to ``false``. Set it to ``true`` explicitly whenever mesh outlines matter
  (architectural walls, tiles that must align along edges); otherwise boundary edges can collapse.
- ``guideDecimation`` lets a vertex-colour or corner-normal attribute steer which regions are simplified
  more aggressively. ``allowCutAndGlue`` permits topology changes for better quality at aggressive
  reduction.
- The GPU path requires CUDA and engages above ``gpuVertexCountThreshold`` (default 500K vertices); a CPU
  parallel path engages above ``cpuVertexCountThreshold`` (default 100K). Keep both defaults: the GPU pays
  off only on large meshes, and forcing small meshes onto it has caused intermittent aborts.

Decimation rewrites the vertex data of the targeted meshes. On stages with references, payloads, or
scenegraph instances this writes overrides on the composed stage while the source asset stays high-poly;
source-level optimization or proxy variants are often the better publishing path. Skinned (``UsdSkel``)
meshes bind joint weights to vertex order, so decimation invalidates those bindings unless skin weights
are regenerated.

Recommended pipelines
---------------------

Commonly paired as ``meshCleanup`` -> ``decimateMeshes`` (clean topology decimates more predictably), and
used for LOD generation or thinning meshes imported at excessive resolution.

Starting configurations
-----------------------

Silhouette-preserving (recommended default), error-budget driven:

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.01, "pinBoundaries": true}]

Conservative (tighter error budget):

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 0.0, "maxMeanError": 0.001, "pinBoundaries": true}]

Target reduction rate (keep 50% of vertices); disable the error cap:

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 50.0, "maxMeanError": 0.0, "pinBoundaries": true}]

Aggressive LOD (expect visible silhouette change; use only for small-screen LODs):

.. code-block:: json

    [{"operation": "decimateMeshes", "reductionFactor": 10.0, "maxMeanError": 0.0, "pinBoundaries": true, "allowCutAndGlue": true}]


Arguments
---------

Meshes to Decimate
^^^^^^^^^^^^^^^^^^

Optional list of prim paths/expressions to decimate

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Reduce to Percentage
^^^^^^^^^^^^^^^^^^^^

Reduce to end result percentage from original vertex count, 0.0-100.0 values accepted. Set to 0 if using Maximum Mean Error.

- Name: ``reductionFactor``
- Type: ``float``
- Default Value: ``50``
- Min Value: ``0.0``
- Max Value: ``100.0``

Maximum Mean Error
^^^^^^^^^^^^^^^^^^

Maximum mean error for the decimation, 0.0-100.0 values accepted. Set 0 to disable this option.

- Name: ``maxMeanError``
- Type: ``float``
- Default Value: ``0``
- Min Value: ``0.0``
- Max Value: ``10.0``

Guide Decimation
^^^^^^^^^^^^^^^^

Guide Decimation by using Normals or Colors (if available)

- Name: ``guideDecimation``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: By normals``
  - ``1: By colors``
  - ``2: Off``


Pin mesh boundaries
^^^^^^^^^^^^^^^^^^^

Preserve the mesh boundaries

- Name: ``pinBoundaries``
- Type: ``bool``
- Default Value: ``False``

Topology Simplification
^^^^^^^^^^^^^^^^^^^^^^^

Allow changes to mesh topology when decimating. Note that this will take more time

- Name: ``allowCutAndGlue``
- Type: ``bool``
- Default Value: ``False``

CPU Vertex Threshold
^^^^^^^^^^^^^^^^^^^^

Use CPU Parallel algorithm if vertex count is greater than this value

- Name: ``cpuVertexCountThreshold``
- Type: ``int``
- Default Value: ``100000``
- Min Value: ``0.0``

GPU Vertex Threshold
^^^^^^^^^^^^^^^^^^^^

Use GPU algorithm if vertex count is greater than this value

- Name: ``gpuVertexCountThreshold``
- Type: ``int``
- Default Value: ``500000``
- Min Value: ``0.0``

