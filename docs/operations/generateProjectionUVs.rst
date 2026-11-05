.. AUTO GENERATED FILE - DO NOT EDIT

=======================
Generate Projection UVs
=======================

**Key**: ``generateProjectionUVs``

Generate *texture coordinates* for meshes using various projection methods.

Arguments
---------

Meshes to generate UVs for
^^^^^^^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Projection Type
^^^^^^^^^^^^^^^

Projection method for generating UVs

- Name: ``projectionType``
- Type: ``int``
- Default Value: ``4``
- Enum Values:

  - ``0: Planar``
  - ``1: Spherical``
  - ``2: Cylindrical``
  - ``3: Triplanar``
  - ``4: Cube``


Use World Space Scales
^^^^^^^^^^^^^^^^^^^^^^

Scale to world space dimensions before projection

- Name: ``useWorldSpaceScales``
- Type: ``bool``
- Default Value: ``True``

Scale Factor
^^^^^^^^^^^^

Uniform scale factor to apply to change UV coordinates texel density

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

