.. AUTO GENERATED FILE - DO NOT EDIT

============
Prune Leaves
============

**Key**: ``pruneLeaves``

Prune unnecessary leaf grouping prims (``Scope``, ``Xform``) from a stage.

Arguments
---------

Prim Paths to Search
^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Method
^^^^^^

How to prune any leaf prims that are found

- Name: ``pruneMode``
- Type: ``int``
- Default Value: ``1``
- Enum Values:

  - ``1: Delete``
  - ``2: Deactivate``
  - ``3: Hide``


Filter Inactive Prims
^^^^^^^^^^^^^^^^^^^^^

Do not consider inactive prims empty

- Name: ``filterInactive``
- Type: ``bool``
- Default Value: ``False``

Preserve Unloaded Payloads
^^^^^^^^^^^^^^^^^^^^^^^^^^

Do not prune leaf prims that carry an unloaded payload (they may contribute content once loaded). Disable to prune them anyway.

- Name: ``preserveUnloadedPayloads``
- Type: ``bool``
- Default Value: ``True``

Preserve Relationship Targets
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Do not prune leaf prims that a relationship targets, or that contain one (pruning them would leave the relationship dangling). Disable to prune them anyway.

- Name: ``preserveRelationshipTargets``
- Type: ``bool``
- Default Value: ``True``

