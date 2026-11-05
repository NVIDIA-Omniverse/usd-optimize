.. AUTO GENERATED FILE - DO NOT EDIT

==============
Auto UV Unwrap
==============

**Key**: ``generateAtlasUVs``

This operation generates texture (UV) coordinates for mesh prims with lower distortion
than projection-based methods, writing them as the face-varying ``st`` primvar. It segments each mesh
into patches, flattens them to 2D, and packs the patches into a single atlas.

Tuning
------

``distortionThreshold`` (default ``3``) is the primary dial: it bounds how much stretch a patch may have
before it is cut. Lower values produce more, smaller patches (more seams, less distortion); higher values
produce fewer patches (fewer seams, more distortion). The value is clamped to a minimum of ``1.05``.

``enableAtlasPacking`` (default ``true``) packs all patches into one atlas; disable it to leave patches
unpacked. ``overwriteExisting`` (default ``true``) regenerates UVs even where an ``st`` primvar already
exists; set it ``false`` to preserve authored UVs.

World-space scaling
-------------------

When ``useWorldSpaceScales`` is ``true`` (default), texel density is derived from world-space size using
``scaleFactor`` and ``scaleUnits`` so UV scale is consistent across meshes of different sizes. These
scale inputs are in stage units and should track ``metersPerUnit``.

Notes
-----

UVs are written as face-varying ``st``. Meshes with time-varying topology, instance proxies, or
unauthored topology are skipped. An empty ``paths`` processes all meshes.

Starting configurations
-----------------------

Default unwrap:

.. code-block:: json

    [{"operation": "generateAtlasUVs", "distortionThreshold": 3.0}]

Low-distortion (more seams):

.. code-block:: json

    [{"operation": "generateAtlasUVs", "distortionThreshold": 1.5}]


Arguments
---------

Meshes to generate UVs for
^^^^^^^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Distortion Threshold
^^^^^^^^^^^^^^^^^^^^

Lower values reduce distortion but increase number of UV islands. Should be > 1.

- Name: ``distortionThreshold``
- Type: ``float``
- Default Value: ``3``

Enable Atlas Packing
^^^^^^^^^^^^^^^^^^^^

Enable atlas packing for AutoUV unwrap

- Name: ``enableAtlasPacking``
- Type: ``bool``
- Default Value: ``True``

Use World Space Scales
^^^^^^^^^^^^^^^^^^^^^^

Scales UV islands to world space dimensions of the source mesh

- Name: ``useWorldSpaceScales``
- Type: ``bool``
- Default Value: ``True``

Scale Factor
^^^^^^^^^^^^

Uniform scale factor to apply to UV islands to change texel density

- Name: ``scaleFactor``
- Type: ``float``
- Default Value: ``0.01``

Scale Units
^^^^^^^^^^^

Real world unit in which the scale factor is described

- Name: ``scaleUnits``
- Type: ``float``
- Default Value: ``0``

Overwrite Existing
^^^^^^^^^^^^^^^^^^

Overwrite existing UVs on the meshes selected for processing

- Name: ``overwriteExisting``
- Type: ``bool``
- Default Value: ``True``

