.. AUTO GENERATED FILE - DO NOT EDIT

========================
Find Coincident Geometry
========================

**Key**: ``findCoincidingGeometry``

Identify geometry that share the same location based on a tolerance metric.

Arguments
---------

Prims To Consider
^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``primPaths``
- Type: ``[string]``
- Default Value: ``[]``

Tolerance
^^^^^^^^^

Tolerance value when comparing points values in world space

- Name: ``tolerance``
- Type: ``float``
- Default Value: ``0.001``

Offset %
^^^^^^^^

An offset to allow prims to be considered coincident. Describes a percentage relative to the prim bounds

- Name: ``offset``
- Type: ``float``
- Default Value: ``0``
- Min Value: ``0.0``
- Max Value: ``150.0``

Fuzzy
^^^^^

Find geometry that is the same shape but may have different vertex positions/connectivity

- Name: ``fuzzy``
- Type: ``bool``
- Default Value: ``False``

