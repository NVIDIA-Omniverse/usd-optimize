.. AUTO GENERATED FILE - DO NOT EDIT

=====
Stats
=====

**Key**: ``printStats``

Collect and display statistics about the contents of a USD stage

Arguments
---------

Count Primvars/Values
^^^^^^^^^^^^^^^^^^^^^

Count the number of primvars and their values

- Name: ``countPrimvars``
- Type: ``bool``
- Default Value: ``False``

Split Collocated Points
^^^^^^^^^^^^^^^^^^^^^^^

Should points that are collocated be considered part of a disjoint mesh

- Name: ``splitCollocatedPoints``
- Type: ``bool``
- Default Value: ``False``

Use Default Time
^^^^^^^^^^^^^^^^

Collect stats at UsdTimeCode::Default. Disable to set Time.

- Name: ``useDefaultTime``
- Type: ``bool``
- Default Value: ``True``

Time
^^^^

Time code to collect stats at when Use Default Time is disabled.

- Name: ``time``
- Type: ``float``
- Default Value: ``0``

