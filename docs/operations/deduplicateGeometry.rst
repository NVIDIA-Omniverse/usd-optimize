.. AUTO GENERATED FILE - DO NOT EDIT

====================
Deduplicate Geometry
====================

**Key**: ``deduplicateGeometry``

This replaces multiple duplicate geometric prims in a scene with a single prim plus
references/instances to it. Since a referenced prim uses less memory than a full duplicated prim, this
can reduce both system memory and VRAM. It is only effective when there are prims that are identical but
not already instanced, so it may have no effect on a scene that is already well instanced.

A fuzzy comparison mode is also available: its similarity measure is independent of tessellation and
based on relative shape deviation, with CPU and GPU implementations. The operation deduplicates
point-based geometry (meshes, basis curves, etc.); in fuzzy mode only meshes are supported.

This is **mesh-level** deduplication: it matches individual gprims, not whole sub-trees. To collapse
duplicate assemblies (entire prim hierarchies), run :doc:`Deduplicate Hierarchies<deduplicateHierarchies>`
first, then this operation to catch any remaining loose duplicate meshes.

Matching controls
-----------------

``tolerance`` (default ``0.001``, stage units) is the position tolerance for considering two meshes
equal; use ``0`` to require exact matches. ``fuzzy`` enables shape-based matching; ``allowScaling`` lets
uniformly scaled copies match; ``considerDeepTransforms`` (default ``true``) lets two meshes match when
their stored points differ by a linear transform, instead of requiring the point arrays to agree
directly. ``minimumDuplicates`` (default ``2``) sets how many copies must exist before a prototype is
created. ``ignoreAttributes`` excludes named attributes from the comparison.

``considerDeepTransforms`` is meant to find more duplicates, but some data sets report more with it
disabled; if a scene with obvious repeats yields none, try both and compare the counts.

``ignoreAttributes`` applies only to the Reference, Instanceable Reference and Point Instancer
methods; for Copy Values and Set Attribute it is a no-op. Prefer a specific attribute over a
namespace, since one ending in ``:`` also excludes everything beneath it.

Recommended pipelines
---------------------

Frequently used in memory-reduction stacks alongside ``optimizeMaterials`` and ``pruneLeaves``, and after
``fitPrimitives`` so primitive-replaced meshes can also be deduplicated.

Starting configurations
-----------------------

Exact instancing (default method):

.. code-block:: json

    [{"operation": "deduplicateGeometry", "duplicateMethod": 2, "tolerance": 0.001}]

Fuzzy (tessellation-independent) matching:

.. code-block:: json

    [{"operation": "deduplicateGeometry", "duplicateMethod": 2, "fuzzy": true, "allowScaling": true}]


Arguments
---------

Geometry to De-duplicate
^^^^^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

    - Name: ``meshPrimPaths``
    - Type: ``[string]``
    - Default Value: ``[]``

Tolerance
^^^^^^^^^

Acceptable point position change during deduplication. The value is a stage unit in worldspace

    - Name: ``tolerance``
    - Type: ``float``
    - Default Value: ``0.001``

Method
^^^^^^

Method used to conform meshes that are duplicates

    - Name: ``duplicateMethod``
    - Type: ``int``
    - Default Value: ``2``
    - Enum Values:
        - ``0: Copy Values``
        - ``1: Reference``
        - ``2: Instanceable Reference``
        - ``3: Set Attribute``
        - ``4: Point Instancer``

Point Instancer Location
^^^^^^^^^^^^^^^^^^^^^^^^

Where to author the PointInstancer for each duplicate set

    - Name: ``pointInstancerLocation``
    - Type: ``int``
    - Default Value: ``0``
    - Enum Values:
        - ``0: Common Root``
        - ``1: Custom Path``

Parent Path
^^^^^^^^^^^

Prim path to author the PointInstancer under. Created as an Xform if it does not exist.

    - Name: ``pointInstancerParentPath``
    - Type: ``str``
    - Default Value: ``""``

Minimum Duplicates
^^^^^^^^^^^^^^^^^^

Minimum number of duplicates a set must contain for a PointInstancer to be created. Sets with fewer duplicates are left untouched.

    - Name: ``minimumDuplicates``
    - Type: ``int``
    - Default Value: ``2``
    - Min Value: ``2.0``

Ignore Attributes
^^^^^^^^^^^^^^^^^

Optional list of attributes to ignore. This list can be explicit attributes, or if ending with a ':' can ignore namespaces.

    - Name: ``ignoreAttributes``
    - Type: ``[string]``
    - Default Value: ``[]``

Fuzzy mode
^^^^^^^^^^

When enabled, uses shape comparison to find duplicates that differ in tessellation or have baked-in point offsets

    - Name: ``fuzzy``
    - Type: ``bool``
    - Default Value: ``False``

Allow Scaling
^^^^^^^^^^^^^

When enabled, fuzzy comparison will factor out uniform scaling

    - Name: ``allowScaling``
    - Type: ``bool``
    - Default Value: ``False``

Consider Deep Transforms
^^^^^^^^^^^^^^^^^^^^^^^^

Look for duplicates where the points values have been uniformly transformed

    - Name: ``considerDeepTransforms``
    - Type: ``bool``
    - Default Value: ``True``

