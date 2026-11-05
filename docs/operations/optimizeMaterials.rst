.. AUTO GENERATED FILE - DO NOT EDIT

==================
Optimize Materials
==================

**Key**: ``optimizeMaterials``

Run operations to optimize materials in a stage. Run this optimization to replace duplicates with references to unique materials. This can reduce memory usage and also improve performance.

Arguments
---------

Materials to Optimize
^^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``materialPrimPaths``
- Type: ``[string]``
- Default Value: ``[]``

Method
^^^^^^

The material optimization to perform

- Name: ``optimizeMaterialsMode``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Deduplicate``
  - ``1: Convert to color``
  - ``2: Remove unbound``
  - ``3: Deduplicate with primvars``


Materials Path
^^^^^^^^^^^^^^

Path to create new Materials at

- Name: ``materialsPath``
- Type: ``str``
- Default Value: ``"/World/Looks"``

