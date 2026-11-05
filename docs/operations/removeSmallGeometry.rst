.. AUTO GENERATED FILE - DO NOT EDIT

=====================
Remove Small Geometry
=====================

**Key**: ``removeSmallGeometry``

This operation will find and remove small or degenerate geometry in the scene. Degenerate geometry are prims with an extent size of 0.0, whereas small geometry is at or below ``threshold``.

Arguments
---------

Paths
^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Remove Method
^^^^^^^^^^^^^

Method that will be used to remove small geometry.

- Name: ``removeMethod``
- Type: ``int``
- Default Value: ``1``
- Enum Values:

  - ``1: Delete``
  - ``2: Deactivate``
  - ``3: Hide``


Detection Method
^^^^^^^^^^^^^^^^

Method that will be used for detecting small geometry. World Space: small geometry is determined by checking the maximum size side of extent bounds against an absolute world space value. Percentage: small geometry is determined by checking the maximum size side of the extent bounds against a percentage threshold of the scene's median extent size.

- Name: ``detectionMethod``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: World Space``
  - ``1: Percentage``


Threshold
^^^^^^^^^

Threshold that represents the size at which extents are considered small (how this is compared depends on the detection method). Note: regardless of the detection method, a threshold of 0.0 will mean only degenerate geometry is removed.

- Name: ``threshold``
- Type: ``float``
- Default Value: ``0``

