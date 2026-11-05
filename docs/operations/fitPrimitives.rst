.. AUTO GENERATED FILE - DO NOT EDIT

==============
Fit Primitives
==============

**Key**: ``fitPrimitives``

This operation attempts to fit transformed primitive shapes (sphere, cylinder, cone, or
cube) to selected meshes. If a mesh can be fit within tolerance it is replaced with the best-fitting
transformed primitive. ``fitSphere``, ``fitCylinder``, ``fitCone``, and ``fitCube`` (all default
``true``) select which primitive types are attempted, in any combination.

Tolerances and units
--------------------

Fitting is governed by two **relative** tolerances, so they are independent of ``metersPerUnit`` and
scene scale:

- ``vertexTolerance`` (default ``0.01``) is the allowed RMS distance between mesh and primitive surface,
  relative to the mesh size. Lower it to accept only close fits, raise it to fit more loosely.
- ``volumeTolerance`` (default ``0.01``) is the allowed relative difference in enclosed volume.

A mesh is replaced only when it passes both. ``gpuFaceCountThreshold`` selects the GPU path for meshes with
at least that many faces (``0``, the default, keeps everything on CPU). On a GPU host use ``100000``: above
it the GPU is faster with the same fits, and below it the CPU is as fast.

Footguns
--------

``ignoreNonConstPrimvars`` and ``ignoreSubsets`` (both default ``true``) are permissive: when ``true`` a
mesh with varying primvars or geom subsets can still be replaced (losing that data). Set them ``false``
to refuse fitting meshes that carry such data. ``generateMeshes`` (default ``false``) emits a tessellated
mesh approximation instead of a true gprim; leave it ``false`` to get actual ``Sphere``/``Cylinder``/etc.
prims, which are lighter.

Recommended pipelines
---------------------

Run before ``deduplicateGeometry`` so the resulting primitives can be instanced, and after a cleanup pass
on noisy meshes. A common chain is ``meshCleanup`` -> ``fitPrimitives`` -> ``deduplicateGeometry``.

Starting configurations
-----------------------

Default fit (all primitive types):

.. code-block:: json

    [{"operation": "fitPrimitives", "vertexTolerance": 0.01, "volumeTolerance": 0.01}]

Strict fit, true gprims only, refuse meshes with subsets/varying primvars:

.. code-block:: json

    [{"operation": "fitPrimitives", "vertexTolerance": 0.005, "ignoreSubsets": false, "ignoreNonConstPrimvars": false}]


Arguments
---------

Meshes to fit
^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

GPU face count threshold
^^^^^^^^^^^^^^^^^^^^^^^^

For meshes with at least this many faces, use GPU algorithm.  A value of zero forces CPU.

- Name: ``gpuFaceCountThreshold``
- Type: ``int``
- Default Value: ``0``
- Min Value: ``0.0``

Show fitting parameters
^^^^^^^^^^^^^^^^^^^^^^^

Tolerances and filters used to determine quality of fit.

- Name: ``showFittingParameters``
- Type: ``bool``
- Default Value: ``True``

Vertex error tolerance
^^^^^^^^^^^^^^^^^^^^^^

Relative tolerance of RMS distance from fit vertices to primitive surface.

- Name: ``vertexTolerance``
- Type: ``float``
- Default Value: ``0.01``
- Min Value: ``0.0``

Volume error tolerance
^^^^^^^^^^^^^^^^^^^^^^

Relative tolerance of volume between faces and the fitting primitive.

- Name: ``volumeTolerance``
- Type: ``float``
- Default Value: ``0.01``
- Min Value: ``0.0``

Ignore non-const primvars
^^^^^^^^^^^^^^^^^^^^^^^^^

If set, a mesh with non-constant primvars is allowed to be fit.  If replaced by a primitive, any non-constant primvars will be lost.

- Name: ``ignoreNonConstPrimvars``
- Type: ``bool``
- Default Value: ``True``

Ignore subsets
^^^^^^^^^^^^^^

If set, a mesh with subsets is allowed to be fit.  If replaced by a primitive, any subsets will be lost.

- Name: ``ignoreSubsets``
- Type: ``bool``
- Default Value: ``True``

Allow negative volume
^^^^^^^^^^^^^^^^^^^^^

If set, a mesh with negative volume (inward-pointing normals) is allowed to be fit.

- Name: ``allowNegativeVolume``
- Type: ``bool``
- Default Value: ``True``

Allow missing endcaps
^^^^^^^^^^^^^^^^^^^^^

If set, a cylinder, cone, or box mesh without endcaps is allowed to be fit.

- Name: ``allowMissingEndcaps``
- Type: ``bool``
- Default Value: ``True``

Fit sphere
^^^^^^^^^^

Attempt to fit a transformed sphere to selected meshes

- Name: ``fitSphere``
- Type: ``bool``
- Default Value: ``True``

Fit cylinder
^^^^^^^^^^^^

Attempt to fit a transformed cylinder to selected meshes

- Name: ``fitCylinder``
- Type: ``bool``
- Default Value: ``True``

Fit cone
^^^^^^^^

Attempt to fit a transformed cone to selected meshes

- Name: ``fitCone``
- Type: ``bool``
- Default Value: ``True``

Fit cube
^^^^^^^^

Attempt to fit a transformed cube to selected meshes

- Name: ``fitCube``
- Type: ``bool``
- Default Value: ``True``

Generate meshes
^^^^^^^^^^^^^^^

If set, a mesh will be generated instead of a primitive shape.

- Name: ``generateMeshes``
- Type: ``bool``
- Default Value: ``False``

Sphere longitude divisions
^^^^^^^^^^^^^^^^^^^^^^^^^^

The number of longitudinal divisions in which to divide spheres.  Must be at least 3.

- Name: ``sphereLongitudeDivisions``
- Type: ``int``
- Default Value: ``32``
- Min Value: ``3.0``
- Max Value: ``1000.0``

Sphere latitude divisions
^^^^^^^^^^^^^^^^^^^^^^^^^

The number of latitudinal divisions in which to divide spheres.  Must be at least 2.

- Name: ``sphereLatitudeDivisions``
- Type: ``int``
- Default Value: ``16``
- Min Value: ``2.0``
- Max Value: ``1000.0``

Cylinder wall divisions
^^^^^^^^^^^^^^^^^^^^^^^

The number of divisions to make around the cylinder wall.  Must be at least 3.

- Name: ``cylinderWallDivisions``
- Type: ``int``
- Default Value: ``32``
- Min Value: ``3.0``
- Max Value: ``1000.0``

Cylinder length divisions
^^^^^^^^^^^^^^^^^^^^^^^^^

The number of end-to-end divisions to make along the cylinder.  Must be positive.

- Name: ``cylinderLatitudeDivisions``
- Type: ``int``
- Default Value: ``1``
- Min Value: ``1.0``
- Max Value: ``1000.0``

Generate cylinder endcaps
^^^^^^^^^^^^^^^^^^^^^^^^^

Whether or not to add endcaps to generated cylinder meshes.

- Name: ``cylinderEndcaps``
- Type: ``bool``
- Default Value: ``True``

Cone side divisions
^^^^^^^^^^^^^^^^^^^

The number of divisions to make around the side of the cone.  Must be at least 3.

- Name: ``coneSideDivisions``
- Type: ``int``
- Default Value: ``64``
- Min Value: ``3.0``
- Max Value: ``1000.0``

Cone length divisions
^^^^^^^^^^^^^^^^^^^^^

The number of divisions to make along the length of the cone.  Must be positive.

- Name: ``coneLengthDivisions``
- Type: ``int``
- Default Value: ``3``
- Min Value: ``1.0``
- Max Value: ``1000.0``

Generate cone bases
^^^^^^^^^^^^^^^^^^^

Whether or not to add a base to generated cone meshes.

- Name: ``coneBases``
- Type: ``bool``
- Default Value: ``True``

