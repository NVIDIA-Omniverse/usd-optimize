.. AUTO GENERATED FILE - DO NOT EDIT

===========
Dice Meshes
===========

**Key**: ``diceMeshes``

This operation creates new vertices and faces as needed to cut meshes along a regular or
irregular grid. Each grid cell is self-contained: no part of a sub-mesh extends outside its cell. Dicing
a large or spatially sparse mesh into cells lets a renderer cull and load it in pieces.

.. Caution:: To create separate mesh prims after dicing, use the :doc:`Split Meshes<splitMeshes>`
   operation or enable ``splitDices`` to split the diced geometry.

Grid definition
---------------

``gridType`` selects ``0`` (*Regular*) or ``1`` (*Irregular*). For a regular grid, ``gridCellX/Y/Z`` set
the cell size along each axis (a value of ``0`` means "do not cut on that axis"), and ``gridOriginX/Y/Z``
shift the grid. For an irregular grid, ``cutHeightsX/Y/Z`` are space-separated lists of cut positions
along each axis. The up-vector arguments are advanced settings (``advancedSettings``) that rotate the
cutting frame away from the world axes.

Scale and units
---------------

``gridCell*``, ``gridOrigin*``, and the cut heights are in **stage units**, so scale them with the
stage's ``metersPerUnit``. A good starting cell size is roughly one tenth of the median mesh extent:
smaller cells cull better but raise prim/face count, larger cells do the opposite.

Starting configurations
-----------------------

Regular grid, uniform 100-unit cells, split into separate prims:

.. code-block:: json

    [{"operation": "diceMeshes", "gridType": 0, "gridCellX": 100.0, "gridCellY": 100.0, "gridCellZ": 100.0, "splitDices": true}]

Coarser cells (fewer, larger pieces):

.. code-block:: json

    [{"operation": "diceMeshes", "gridType": 0, "gridCellX": 500.0, "gridCellY": 500.0, "gridCellZ": 500.0}]


Arguments
---------

Meshes To Process
^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Split Dices
^^^^^^^^^^^

Split diced meshes into separate prims

- Name: ``splitDices``
- Type: ``bool``
- Default Value: ``False``

Grid Type
^^^^^^^^^

The type of grid

- Name: ``gridType``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Regular``
  - ``1: Irregular``


Cut Heights X
^^^^^^^^^^^^^

Cut heights in the X direction

- Name: ``cutHeightsX``
- Type: ``str``
- Default Value: ``""``

Cut Heights Y
^^^^^^^^^^^^^

Cut heights in the Y direction

- Name: ``cutHeightsY``
- Type: ``str``
- Default Value: ``""``

Cut Heights Z
^^^^^^^^^^^^^

Cut heights in the Z direction

- Name: ``cutHeightsZ``
- Type: ``str``
- Default Value: ``""``

Grid Cell X
^^^^^^^^^^^

Grid Cell Height X

- Name: ``gridCellX``
- Type: ``float``
- Default Value: ``0``
- Min Value: ``0.0``

Grid Cell Y
^^^^^^^^^^^

Grid Cell Height Y

- Name: ``gridCellY``
- Type: ``float``
- Default Value: ``0``
- Min Value: ``0.0``

Grid Cell Z
^^^^^^^^^^^

Grid Cell Height Z

- Name: ``gridCellZ``
- Type: ``float``
- Default Value: ``0``
- Min Value: ``0.0``

Grid Origin X
^^^^^^^^^^^^^

Grid Origin X

- Name: ``gridOriginX``
- Type: ``float``
- Default Value: ``0``

Grid Origin Y
^^^^^^^^^^^^^

Grid Origin Y

- Name: ``gridOriginY``
- Type: ``float``
- Default Value: ``0``

Grid Origin Z
^^^^^^^^^^^^^

Grid Origin Z

- Name: ``gridOriginZ``
- Type: ``float``
- Default Value: ``0``

Advanced Settings
^^^^^^^^^^^^^^^^^

Toggle advanced settings

- Name: ``advancedSettings``
- Type: ``bool``
- Default Value: ``False``

Up-vector A x
^^^^^^^^^^^^^

Up-vector X

- Name: ``upVectorAx``
- Type: ``float``
- Default Value: ``1``

Up-vector A y
^^^^^^^^^^^^^

Up-vector X

- Name: ``upVectorAy``
- Type: ``float``
- Default Value: ``0``

Up-vector A z
^^^^^^^^^^^^^

Up-vector X

- Name: ``upVectorAz``
- Type: ``float``
- Default Value: ``0``

Up-vector B x
^^^^^^^^^^^^^

Up-vector Y

- Name: ``upVectorBx``
- Type: ``float``
- Default Value: ``0``

Up-vector B y
^^^^^^^^^^^^^

Up-vector Y

- Name: ``upVectorBy``
- Type: ``float``
- Default Value: ``1``

Up-vector B z
^^^^^^^^^^^^^

Up-vector Y

- Name: ``upVectorBz``
- Type: ``float``
- Default Value: ``0``

Up-vector C x
^^^^^^^^^^^^^

Up-vector Z

- Name: ``upVectorCx``
- Type: ``float``
- Default Value: ``0``

Up-vector C y
^^^^^^^^^^^^^

Up-vector Z

- Name: ``upVectorCy``
- Type: ``float``
- Default Value: ``0``

Up-vector C z
^^^^^^^^^^^^^

Up-vector Z

- Name: ``upVectorCz``
- Type: ``float``
- Default Value: ``1``

