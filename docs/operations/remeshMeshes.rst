.. AUTO GENERATED FILE - DO NOT EDIT

=============
Remesh Meshes
=============

**Key**: ``remeshMeshes``

This operation will remesh input mesh prims to a defined tolerance to create a new mesh topology. Input mesh and normals will guide the maximum error and size of the triangles to match input volume.

Arguments
---------

Meshes to Remesh
^^^^^^^^^^^^^^^^

Optional list of prim paths/expressions to remesh

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Gradation
^^^^^^^^^

The gradation for the remesh, affecting how many triangles are generated. [Note: this parameter will likely be replaced by something else]

- Name: ``gradation``
- Type: ``float``
- Default Value: ``0``
- Min Value: ``0.0``
- Max Value: ``0.5``

Maximum Error
^^^^^^^^^^^^^

Maximum error for the remesh.

- Name: ``maxError``
- Type: ``float``
- Default Value: ``0.1``
- Min Value: ``0.0``

