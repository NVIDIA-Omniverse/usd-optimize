.. AUTO GENERATED FILE - DO NOT EDIT

=====================
Optimize Time Samples
=====================

**Key**: ``optimizeTimeSamples``

Remove redundant time-samples from attributes in a stage.

Arguments
---------

Prim Paths
^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Remove Interpolated
^^^^^^^^^^^^^^^^^^^

Remove intermediate samples that can be linearly interpolated

- Name: ``removeInterpolated``
- Type: ``bool``
- Default Value: ``False``

Epsilon (Double)
^^^^^^^^^^^^^^^^

Threshold for which to consider double numbers equal

- Name: ``epsilonD``
- Type: ``float``
- Default Value: ``1e-12``

Epsilon (Float)
^^^^^^^^^^^^^^^

Threshold for which to consider floating point numbers equal

- Name: ``epsilonF``
- Type: ``float``
- Default Value: ``1e-06``

