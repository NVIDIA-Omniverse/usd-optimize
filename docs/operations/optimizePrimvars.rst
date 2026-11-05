.. AUTO GENERATED FILE - DO NOT EDIT

=================
Optimize Primvars
=================

**Key**: ``optimizePrimvars``

Run operations to optimize primvars in the stage. This tool can convert flat primvars to indexed, or indexed primvars to flattened. It can also attempt to simplify primvars. For example if a primvar is authored as faceVarying (a value per face-vertex), but all the values are equal, this can be simplified to ``constant`` interpolation. Or if all the values for each face are equal, it could be reduced to ``uniform`` interpolation.

Flattening refers to removing indexing from a primvar and authoring all of the values in one array, whether they are unique or not. This may take more disk space. Indexing refers to recording only unique values in the primvar data, and having separate indices that refer to the unique values. This can take less space, particularly if there are not many unique values versus the length of the array.

Arguments
---------

Prim Paths
^^^^^^^^^^

A list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Primvar Names
^^^^^^^^^^^^^

Optional comma-separated list of primvars to consider

- Name: ``primvars``
- Type: ``[string]``
- Default Value: ``[]``

Mode
^^^^

What to do with any matching primvars

- Name: ``mode``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Ignore``
  - ``1: Index``
  - ``2: Index (Forced)``
  - ``3: Flatten``
  - ``4: Remove``


Simplify
^^^^^^^^

If possible, find a simpler representation of a primvar (e.g. convert uniform to constant if all values match)

- Name: ``simplify``
- Type: ``bool``
- Default Value: ``False``

Only Remove If Bound
^^^^^^^^^^^^^^^^^^^^

Only remove primvars if their prim has a material bound

- Name: ``removeIfBound``
- Type: ``bool``
- Default Value: ``False``

