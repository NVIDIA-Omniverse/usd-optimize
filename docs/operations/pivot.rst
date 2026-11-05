.. AUTO GENERATED FILE - DO NOT EDIT

=============
Compute Pivot
=============

**Key**: ``pivot``

Compute Pivot will place the pivot at the center of the target mesh, think of this as creating a center pivot in other DCC tools.
This makes it easier to interact with objects in the scene because the transform manipulator is centered on the object.

Some tools generate scenes where the transform is at the origin, meaning it is far from the actual vertices, making it hard to move a mesh precisely.

Arguments
---------

Prims To Process
^^^^^^^^^^^^^^^^

Optional list of prim paths or expressions to consider

- Name: ``meshPrimPaths``
- Type: ``[string]``
- Default Value: ``[]``

Overwrite Authored Pivots
^^^^^^^^^^^^^^^^^^^^^^^^^

If enabled, overwrite existing authored pivot attributes.

- Name: ``overwrite``
- Type: ``bool``
- Default Value: ``False``

Apply To
^^^^^^^^

What type of prims to apply a pivot to

- Name: ``applyTo``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Meshes``
  - ``1: Meshes and Xforms``


Method
^^^^^^

Method of determining the new pivot

- Name: ``method``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Weighted``
  - ``1: Bounding Box Center``


