.. AUTO GENERATED FILE - DO NOT EDIT

================
Generate Normals
================

**Key**: ``generateNormals``

Generate normals for meshes.

Arguments
---------

Meshes To Process
^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Add Normals to
^^^^^^^^^^^^^^

Type of normals to generate

- Name: ``binding``
- Type: ``int``
- Default Value: ``3``
- Enum Values:

  - ``3: Auto``
  - ``0: Corners``
  - ``2: Vertices``
  - ``1: Faces``


Existing Normals
^^^^^^^^^^^^^^^^

What to do with existing normals

- Name: ``existingNormals``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Fix``
  - ``1: Replace``


Sharpness Angle
^^^^^^^^^^^^^^^

The absolute value of the dihedral angle at an edge above which the edge is considered sharp. The dihedral angle is measured in the range ]-180, 180] degrees with 0 being flat.

- Name: ``sharpnessAngle``
- Type: ``float``
- Default Value: ``60``
- Min Value: ``-180.0``
- Max Value: ``180.0``

Weighting
^^^^^^^^^

Weight each contribution to the final normal by angle or face area

- Name: ``weightmode``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Angle``
  - ``1: Area``


GPU Threshold
^^^^^^^^^^^^^

Use GPU algorithm if number of normals to generate is greater than this value

- Name: ``gpuThreshold``
- Type: ``int``
- Default Value: ``500000``
- Min Value: ``0.0``

