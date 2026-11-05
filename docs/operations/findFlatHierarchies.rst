.. AUTO GENERATED FILE - DO NOT EDIT

=====================
Find Flat Hierarchies
=====================

**Key**: ``findFlatHierarchies``

Finds prims that have more than a specified number of children.

Arguments
---------

Paths
^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Max Children
^^^^^^^^^^^^

The maximum number of children a prim can have until it is considered a flat hierarchy.

- Name: ``maxChildren``
- Type: ``int``
- Default Value: ``500``

Consider All Children
^^^^^^^^^^^^^^^^^^^^^

Whether to consider all children or only active, loaded, defined, non-abstract children.

- Name: ``considerAllChildren``
- Type: ``bool``
- Default Value: ``True``

