.. AUTO GENERATED FILE - DO NOT EDIT

========
Box Clip
========

**Key**: ``boxClip``

Clips meshes to a user-defined world-space axis-aligned box, like a 3D cookie cutter. It
keeps, cuts, or discards geometry depending on whether it falls inside, outside, or straddles the box.

Defining the box
----------------

``clipBoxDef`` selects how the box is specified: ``1`` (*Prim*, the default) uses the world-space
bounding box of the prim at ``clipBoxPrimPath``; ``0`` (*Corners of Box*) uses the explicit
``minX/minY/minZ`` and ``maxX/maxY/maxZ`` corners. The corner values are in **stage units** and must be
authored at the scene's scale (track ``metersPerUnit``). The default corner box is degenerate (all
zeros), so *Corners of Box* mode does nothing until real extents are supplied, and each min must be less
than its corresponding max.

Recommended pipelines
---------------------

After cutting with mode ``1``, run a ``meshCleanup`` pass to tidy the new cut edges.

Starting configurations
-----------------------

Trim to a prim's bounds, cutting straddling geometry:

.. code-block:: json

    [{"operation": "boxClip", "clipBoxDef": 1, "clipBoxPrimPath": "/World/ClipVolume", "clipMode": 1}]

Trim to explicit corners:

.. code-block:: json

    [{"operation": "boxClip", "clipBoxDef": 0, "minX": -100.0, "minY": -100.0, "minZ": -100.0, "maxX": 100.0, "maxY": 100.0, "maxZ": 100.0, "clipMode": 1}]


Arguments
---------

Meshes To Process
^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Clip Box Definition
^^^^^^^^^^^^^^^^^^^

How the clip box is defined

- Name: ``clipBoxDef``
- Type: ``int``
- Default Value: ``1``
- Enum Values:

  - ``0: Corners of Box``
  - ``1: Prim``


min x
^^^^^

Min x value

- Name: ``minX``
- Type: ``float``
- Default Value: ``0``

min y
^^^^^

Min y value

- Name: ``minY``
- Type: ``float``
- Default Value: ``0``

min z
^^^^^

Min z value

- Name: ``minZ``
- Type: ``float``
- Default Value: ``0``

max x
^^^^^

Max x value

- Name: ``maxX``
- Type: ``float``
- Default Value: ``0``

max y
^^^^^

Max y value

- Name: ``maxY``
- Type: ``float``
- Default Value: ``0``

max z
^^^^^

Max z value

- Name: ``maxZ``
- Type: ``float``
- Default Value: ``0``

Clip Box Prim
^^^^^^^^^^^^^

Prim whose extents defines the clip box

- Name: ``clipBoxPrimPath``
- Type: ``str``
- Default Value: ``""``

Ignore Clip Box Side
^^^^^^^^^^^^^^^^^^^^

Optionally ignore one side of the clip box (extending it to infinity)

- Name: ``ignoreClipBoxSide``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: None``
  - ``1: -X``
  - ``2: +X``
  - ``3: -Y``
  - ``4: +Y``
  - ``5: -Z``
  - ``6: +Z``


Clip Mode
^^^^^^^^^

How geometry is clipped relative to the box

- Name: ``clipMode``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Keep if fully inside clip box + keep if partially inside``
  - ``1: Keep if fully inside clip box + cut if partially inside``
  - ``2: Keep if fully inside clip box + discard if partially inside``
  - ``3: Keep if fully outside clip box + keep if partially outside``
  - ``4: Keep if fully outside clip box + discard if partially outside``


