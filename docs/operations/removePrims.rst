.. AUTO GENERATED FILE - DO NOT EDIT

============
Remove Prims
============

**Key**: ``removePrims``

Configurable operation that can find different types of prims that can be removed from the stage and provides options for how to remove them.

Arguments
---------

Paths
^^^^^

Optional list of prim paths to consider.

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Remove Invisible
^^^^^^^^^^^^^^^^

Whether to remove prims which have computed visibility as invisible.

- Name: ``removeInvisible``
- Type: ``bool``
- Default Value: ``True``

Invisible Remove Method
^^^^^^^^^^^^^^^^^^^^^^^

Method that will be used to remove invisible prims.

- Name: ``invisibleRemoveMethod``
- Type: ``int``
- Default Value: ``2``
- Enum Values:

  - ``1: Delete``
  - ``2: Deactivate``


Remove Orphaned Overs
^^^^^^^^^^^^^^^^^^^^^

Whether to remove orphaned overs i.e. overs that do not have any non-concrete arcs, relationships, or connections.

- Name: ``removeOrphanedOvers``
- Type: ``bool``
- Default Value: ``True``

Orphaned Remove Method
^^^^^^^^^^^^^^^^^^^^^^

Method that will be used to remove orphaned overs.

- Name: ``orphanedOverRemoveMethod``
- Type: ``int``
- Default Value: ``1``
- Enum Values:

  - ``1: Delete``
  - ``2: Deactivate``
  - ``3: Hide``


