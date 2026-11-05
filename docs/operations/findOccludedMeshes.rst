.. AUTO GENERATED FILE - DO NOT EDIT

====================
Find Occluded Meshes
====================

**Key**: ``findOccludedMeshes``

Analyses a scene to find meshes that are globally occluded: meshes not visible from any
camera that does not have to cross other geometry to see them (for example, geometry sealed inside a
closed enclosure). It is an analysis operation that flags candidates to be deactivated, hidden, or
deleted; the bias is conservative, so a mesh is only reported when it is confidently hidden.

How it works
------------

The scene is rasterized into a voxel grid and visibility is flood-filled from the exterior. A mesh is
considered occluded when no exterior path reaches it. The check runs on GPU when ``useGpu`` is enabled
(the default; it falls back to CPU if CUDA is unavailable or the GPU check fails) and on CPU otherwise. Leave
it on: same meshes, much faster. Out of GPU memory, the CPU rerun is much slower; lower
``maximumGridResolution`` to stay on the GPU.

Tuning
------

- ``maximumGridResolution`` caps the number of cells along the longest axis. Higher values detect smaller
  openings but cost cubically more memory and time (500 suits a powerful GPU; use less for CPU).
- ``minimumGapSize`` is the smallest opening, in **stage units**, treated as a gap. Effective grid
  spacing is ``max(minimumGapSize, maxDim / maximumGridResolution)``. Smaller values produce a finer grid
  that finds smaller openings and flags fewer meshes as occluded. It acts as a tolerance for how sealed
  an enclosure must be (e.g. 3.5 means ignore any opening smaller than 3.5 stage units). Scale it with
  ``metersPerUnit``.
- ``clustered`` splits the stage into clusters of meshes with overlapping bounds and checks each cluster
  separately, improving both accuracy and performance. Clustering runs on the CPU and its cost grows faster
  than the mesh count, so on scenes with thousands of meshes pass one sub-assembly's meshes at a time in
  ``paths`` (mesh paths; a parent prim does not expand). Only meshes in ``paths`` occlude, so keep each mesh's
  enclosure in the same selection. Turning ``clustered`` off is fast but misses most occluded meshes.
- ``checkTransparency`` excludes meshes with opacity < 1.0 from occlusion testing.

Starting configurations
-----------------------

Standard analysis:

.. code-block:: json

    [{"operation": "findOccludedMeshes", "clustered": true, "checkTransparency": true}]

Conservative (finer grid, smaller gaps detected):

.. code-block:: json

    [{"operation": "findOccludedMeshes", "minimumGapSize": 0.01, "maximumGridResolution": 500}]


Arguments
---------

Meshes used for occlusion testing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Meshes that are tested for occlusion as well as considered as occluders

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Clustered
^^^^^^^^^

Split the stage into clusters of meshes with overlapping bounding boxes and check visibility per cluster, improving both accuracy and performance by reducing the number of meshes compared at the same time

- Name: ``clustered``
- Type: ``bool``
- Default Value: ``True``

Minimum gap size
^^^^^^^^^^^^^^^^

The minimum gap size corresponding to the spacing of the background grid. Gaps smaller than this value are considered closed for occlusion culling. The actual grid spacing is max(minimumGapSize, maxDim/maximumGridResolution). Very small values defer to maximumGridResolution for spacing, producing a finer grid that detects smaller gaps and results in fewer meshes being flagged as occluded. It is essentially a tolerance for how sealed an enclosure needs to be: e.g. a value of 3.5 means ignore any opening smaller than 3.5 scene units when deciding if something is hidden

- Name: ``minimumGapSize``
- Type: ``float``
- Default Value: ``0.01``
- Min Value: ``0.0``

Maximum grid resolution
^^^^^^^^^^^^^^^^^^^^^^^

The maximum number of cells along the longest axis of the grid used for visibility checking. This caps the grid resolution to prevent excessive memory and compute costs (the grid is 3D, so memory scales with the cube of resolution). A value of 500 is suitable for powerful GPUs, use smaller values for less powerful GPUs or CPUs

- Name: ``maximumGridResolution``
- Type: ``float``
- Default Value: ``500``
- Min Value: ``1.0``

Check Transparency
^^^^^^^^^^^^^^^^^^

Exclude meshes with opacity < 1.0 from occlusion testing

- Name: ``checkTransparency``
- Type: ``bool``
- Default Value: ``False``

Action
^^^^^^

What to do with occluded meshes

- Name: ``action``
- Type: ``int``
- Default Value: ``3``
- Enum Values:

  - ``1: Delete``
  - ``2: Deactivate``
  - ``3: Hide``


