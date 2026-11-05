.. AUTO GENERATED FILE - DO NOT EDIT

==============
Merge Vertices
==============

**Key**: ``mergeVertices``

This operation merges vertices that are closer to one another than a given tolerance, followed by removing any degenerate faces and optionally making the resulting mesh be manifold and/or removing any isolated vertices.

Arguments
---------

Meshes To Process
^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Tolerance
^^^^^^^^^

The tolerance (distance) apart for vertices to be considered equal, a negative value skips the merge vertices step

- Name: ``tolerance``
- Type: ``float``
- Default Value: ``0``

Merge Boundaries
^^^^^^^^^^^^^^^^

Merge boundaries when merging vertices

- Name: ``mergeBoundaries``
- Type: ``bool``
- Default Value: ``True``

Remove isolated vertices
^^^^^^^^^^^^^^^^^^^^^^^^

Removes isolated vertices (done after merging vertices)

- Name: ``removeIsolatedVertices``
- Type: ``bool``
- Default Value: ``True``

Make Manifold
^^^^^^^^^^^^^

Ensure the final result is a manifold mesh

- Name: ``makeManifold``
- Type: ``bool``
- Default Value: ``True``

