.. AUTO GENERATED FILE - DO NOT EDIT

==========
Shrinkwrap
==========

**Key**: ``shrinkwrap``

This operation converts meshes to a level set volume using
`OpenVDB <https://www.openvdb.org/>`_ and extracts a watertight mesh back out. It is useful for closing
holes, simplifying topology, and creating LOD meshes. The algorithm rasterizes the input mesh into a
narrow-band level set, optionally erodes the surface to close gaps and holes, and extracts a new polygon
mesh from the resulting volume. The output mesh is written as a new sibling prim alongside the original,
which is preserved.

Input modes
-----------

The default ``Per Mesh`` mode processes each selected mesh independently at ``UsdTimeCode::Default`` and
writes a sibling output for each mesh. ``Temporal Combined`` mode evaluates every selected mesh across an
inclusive time range, combines all samples in world space, and writes one static swept-envelope mesh at
``outputPath``. ``startTime`` and ``endTime`` default to the stage time range when left as NaN. The end
time is always included even when ``timeStep`` does not land on it exactly.

Temporal combined output is a discrete sampled envelope, not an analytical continuous sweep. A smaller
``timeStep`` captures fast, rotating, or nonlinear motion more faithfully, but memory and processing cost
grow approximately with the number of meshes times the number of samples. An empty ``paths`` selection
samples every mesh on the stage and can therefore be expensive. The output is authored in world space
with transform inheritance reset so it remains static.

With ``connectTimeSamples`` disabled, each sample contributes only its own surface, with nothing bridging
the gap to the next one. If ``timeStep`` is too coarse for the motion, consecutive poses do not overlap in
space and the envelope is a set of disjoint shells, one per sample, rather than a single enclosing surface;
this looks like a plausible closed shape in a viewport while actually having holes between samples. To
check, decrease ``timeStep`` and confirm the envelope stops changing, or enable ``connectTimeSamples`` to
bridge topology-compatible samples with swept prisms instead.

When ``connectTimeSamples`` is enabled, corresponding triangles at adjacent samples are connected as
closed swept prisms before voxelization. This fills the interval between samples and produces a more
continuous envelope without requiring extremely small time steps. It requires stable point and topology
correspondence. Intervals with changing topology fall back to the sampled surfaces instead of prisms;
without prisms bridging the gap, poses that do not already overlap in space produce a **disconnected**
envelope with one shell per sample, which can look like a single closed surface in a viewport while
actually having holes running through it. If an interval changes topology, reduce ``timeStep`` around it so
consecutive samples' poses overlap directly. The operation logs an info-level count of how many intervals
fell back to sampled surfaces this way; a non-zero count on an asset that mixes deforming topology with
other motion is a signal to check for gaps. Swept prisms linearly approximate vertex motion: a triangle's
corners move in straight chords between samples, so on a rotating or otherwise nonlinear path the prism can
both overfill the inside of the arc and underfill its outside (the true swept extent lies outside the
chord). ``timeStep`` must be fine enough relative to the fastest rotation on the stage to keep both errors
small; when in doubt, decrease it and confirm the envelope extent stops changing.

``adaptivity`` trades containment margin for triangle count in this mode: at higher values it can pull
the envelope surface inward of positions the volume step actually swept, most visibly on flat regions.
The bounding box does not catch this — it is computed before the simplification pass runs. If the
envelope must contain the swept geometry, leave ``adaptivity`` at ``0`` in this mode, or verify
empirically that sampled points fall inside the output.

Choosing resolution
-------------------

``voxelSize`` is the dominant control and should be set first. It is the edge length of a level-set voxel,
in **stage units**: smaller values capture finer detail but cost cubically more memory and time, larger
values smooth the result and close bigger gaps. ``adaptivity`` (0-1) then simplifies flat regions of the
extracted mesh to reduce triangle count without re-running the volume step.

Scale and units
---------------

``voxelSize``, ``erode``, and ``threshold`` are all in stage units, so the right values depend on the
stage's ``metersPerUnit``. To get the same physical size in stage units, divide the desired size in metres
by ``metersPerUnit``: ``stageUnits = metres / metersPerUnit``. For example, a target resolution of 5 cm
(0.05 m) is ``voxelSize = 0.05`` on a stage authored in metres (``metersPerUnit`` = 1), but
``voxelSize = 5.0`` on a stage authored in centimetres (``metersPerUnit`` = 0.01) — 100x larger, since
each stage unit is 100x smaller. The starting configurations below use metre-scale examples; scale them by
``1 / metersPerUnit`` for other units.

Tuning order
------------

1. Set ``voxelSize`` for the target detail level (start small and increase until cost is acceptable).
2. Increase ``erode`` to close larger gaps and holes.
3. Adjust ``threshold`` to shift the extracted iso-surface inward or outward.
4. Raise ``adaptivity`` to thin out triangles on flat areas (costs containment margin in
   ``Temporal Combined`` mode; see `Input modes`_).

``erode`` is not a "how much to shrink" dial with a neutral value at 0; it is the number of constrained
erosion steps the level set is allowed to take, coarse to fine, while conforming toward the true surface.
At ``erode=0`` those conforming steps never run, so the output is the coarsest offset surface the algorithm
builds internally (governed by the mesh's bounding box, not by ``voxelSize``) merely upsampled to the
target resolution: a heavily bloated envelope, often several times the size of the input, not a
close-fitting wrap. Use the default of 8 as a starting point and raise it only to close larger gaps; do not
lower it toward 0 expecting a tighter result.

Recommended pipelines
---------------------

Often run after ``merge`` so a group of parts becomes one watertight shell; target the merged prims with
``paths``. Pairs well with ``decimateMeshes`` afterward for LOD generation.

Starting configurations
-----------------------

Values below assume a metre-scale stage (``metersPerUnit`` = 1); see `Scale and units`_ to rescale for
other units.

Conservative detail preservation:

.. code-block:: json

    [{"operation": "shrinkwrap", "voxelSize": 0.05, "erode": 8.0}]

Gap-closing / hole-filling (coarser, more erosion):

.. code-block:: json

    [{"operation": "shrinkwrap", "voxelSize": 0.2, "erode": 16.0, "adaptivity": 0.5}]

Motion envelope over a fixed time range (``Temporal Combined`` mode):

.. code-block:: json

    [{"operation": "shrinkwrap", "inputMode": 1, "startTime": 1, "endTime": 48, "timeStep": 1,
      "connectTimeSamples": true, "outputPath": "/World/Envelope", "voxelSize": 0.05}]


Arguments
---------

Meshes to Shrinkwrap
^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths/expressions to shrinkwrap

    - Name: ``paths``
    - Type: ``[string]``
    - Default Value: ``[]``

Input Mode
^^^^^^^^^^

Process each mesh at default time, or combine all selected meshes sampled over a time range

    - Name: ``inputMode``
    - Type: ``int``
    - Default Value: ``0``
    - Enum Values:
        - ``0: Per Mesh``
        - ``1: Temporal Combined``

Start Time
^^^^^^^^^^

First time code to sample. NaN uses the stage start time code.

    - Name: ``startTime``
    - Type: ``float``
    - Default Value: ``nan``

End Time
^^^^^^^^

Last time code to sample. NaN uses the stage end time code.

    - Name: ``endTime``
    - Type: ``float``
    - Default Value: ``nan``

Time Step
^^^^^^^^^

Interval between temporal samples. The end time is always sampled.

    - Name: ``timeStep``
    - Type: ``float``
    - Default Value: ``1``
    - Min Value: ``1.1754943508222875e-38``

Connect Time Samples
^^^^^^^^^^^^^^^^^^^^

Fill motion between topology-compatible adjacent samples with swept triangle prisms

    - Name: ``connectTimeSamples``
    - Type: ``bool``
    - Default Value: ``True``

Output Path
^^^^^^^^^^^

Required new absolute prim path for the combined static shrinkwrap mesh

    - Name: ``outputPath``
    - Type: ``str``
    - Default Value: ``""``

Voxel Size
^^^^^^^^^^

Explicit voxel size. Smaller values produce finer detail but use more memory. Set to 0 to use Grid Dimension instead.

    - Name: ``voxelSize``
    - Type: ``float``
    - Default Value: ``0.1``
    - Min Value: ``0.0``

Erosion Steps
^^^^^^^^^^^^^

Number of erosion steps. Controls how much the level set can shrink/erode the shape.

    - Name: ``erode``
    - Type: ``float``
    - Default Value: ``8``
    - Min Value: ``0.0``

Closing Threshold
^^^^^^^^^^^^^^^^^

Size of the largest geometric feature or gap beyond which the algorithm should erode no further. Higher values close more holes and gaps in the mesh.

    - Name: ``threshold``
    - Type: ``float``
    - Default Value: ``0``
    - Min Value: ``0.0``

Adaptive Meshing Threshold
^^^^^^^^^^^^^^^^^^^^^^^^^^

Controls adaptive meshing. 0 = no adaptive meshing (uniform tessellation), 1 = most adaptive (fewest triangles).

    - Name: ``adaptivity``
    - Type: ``float``
    - Default Value: ``0``
    - Min Value: ``0.0``
    - Max Value: ``1.0``

