.. AUTO GENERATED FILE - DO NOT EDIT

=======================
Deduplicate Hierarchies
=======================

**Key**: ``deduplicateHierarchies``

Find duplicate prim hierarchies and replace duplicates with instanceable internal
references to a prototype. Identical hierarchies that contain variants produce one prototype per variant.
The operation recurses into each prototype so nested duplicates are consolidated into nested instanceable
references.

This is **assembly-level** deduplication: it matches whole sub-trees, not individual meshes. Matching is
structural (the shape of the sub-tree plus authored values), so it safely collapses repeated assemblies
such as bolts, fasteners, or fixtures that appear many times in a CAD-imported scene. To also catch loose
duplicate meshes that are not part of a repeated hierarchy, follow this with
:doc:`Deduplicate Geometry<deduplicateGeometry>`.

Matching controls
-----------------

``tolerance`` (default ``0.001``, stage units) is the value tolerance for treating two hierarchies as
equal. Use ``0`` to require exact matches; this is appropriate for metrology, simulation, or articulated
assets where small authored differences are meaningful. ``ignoreShaderOutputs`` (default ``true``)
ignores shader output differences when comparing. ``maxDepth`` (default ``0`` = unlimited) caps how deep
the structural comparison descends.

Recommended pipelines
---------------------

Pair with ``deduplicateGeometry`` (hierarchies first, then geometry). This is the basis of the
``hierarchy-dedup`` preset.

Starting configurations
-----------------------

Structural dedup, then geometry dedup:

.. code-block:: json

    [
        {"operation": "deduplicateHierarchies"},
        {"operation": "deduplicateGeometry", "duplicateMethod": 2, "tolerance": 0.001}
    ]

Exact matching (no value tolerance):

.. code-block:: json

    [{"operation": "deduplicateHierarchies", "tolerance": 0.0}]


Arguments
---------

Prim Paths
^^^^^^^^^^

Optional subtree roots. Empty = walk children of the default prim.

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Tolerance
^^^^^^^^^

Acceptable difference for floating-point properties when comparing subtrees: scalar float/double/half, vectors, matrices (including descendant xformOp:transform), quaternions, and arrays of any of these (points, normals, UVs, etc.). The value is in stage units. Integer/topology indices, strings, tokens and bools always require an exact match regardless of tolerance. Set to 0 for bitwise-exact comparison.

- Name: ``tolerance``
- Type: ``float``
- Default Value: ``0.001``

Ignore Shader Outputs
^^^^^^^^^^^^^^^^^^^^^

Skip shader output attributes (outputs:surface, outputs:displacement, etc.) during value comparison. These often differ between material instances even when the geometry is identical. Enabled by default.

- Name: ``ignoreShaderOutputs``
- Type: ``bool``
- Default Value: ``True``

Max Depth
^^^^^^^^^

Maximum number of breadth-first levels to descend, counting from the children of the default prim (or of `paths`) as level 1. 0 (the default) means unbounded. Because the operation recurses into each prototype to build a nested-instance library, deep hierarchies can reach many levels; cap this to bound runtime or to avoid consolidating very deeply nested instances.

- Name: ``maxDepth``
- Type: ``int``
- Default Value: ``0``

