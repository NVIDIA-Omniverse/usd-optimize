.. AUTO GENERATED FILE - DO NOT EDIT

====================
Primitives to Meshes
====================

**Key**: ``primitivesToMeshes``

This operation replaces gprim types sphere, cylinder, capsule, cone, and cube with a mesh approximation. This allows the geometry to be used with operations that require mesh types.

Capsule height describes the cylindrical body, excluding the two hemispherical caps. Capsule meshes approximate this height and preserve the radius and axis without stretching the caps. Use capsuleRadialDivisions for circumferential detail, capsuleLengthDivisions for body subdivisions, and capsuleCapDivisions for rounded-end detail. Increasing these values increases mesh size. Capsules are always closed.

Capsule prototypes have radius one. The height/radius ratio is first converted to a single-precision float. All eight exponent bits and the highest 20 fraction bits, without the sign bit, are encoded as seven uppercase hexadecimal digits after ``_hr``. The lowest three fraction bits are zeroed for geometry, so ratios with the same code share a prototype without ratio-collision suffixes, independently of conversion order. Capsules with the same quantized ratio, axis, and subdivision settings share a prototype; a uniform scale restores each capsule's radius, while its body height approximates the original according to the quantized ratio. Capsules with invalid dimensions (nonpositive radius, negative height, nonfinite dimensions, or a ratio outside the finite float range) are skipped.

Arguments
---------

Primitives to convert
^^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Convert sphere primitives
^^^^^^^^^^^^^^^^^^^^^^^^^

Whether or not to generate meshes from sphere prims.

- Name: ``convertSpheres``
- Type: ``bool``
- Default Value: ``True``

Longitude divisions
^^^^^^^^^^^^^^^^^^^

The number of longitudinal divisions in which to divide spheres.  Must be at least 3.

- Name: ``sphereLongitudeDivisions``
- Type: ``int``
- Default Value: ``32``
- Min Value: ``3.0``
- Max Value: ``1000.0``

Latitude divisions
^^^^^^^^^^^^^^^^^^

The number of latitudinal divisions in which to divide spheres.  Must be at least 2.

- Name: ``sphereLatitudeDivisions``
- Type: ``int``
- Default Value: ``16``
- Min Value: ``2.0``
- Max Value: ``1000.0``

Convert cylinder primitives
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Whether or not to generate meshes from cylinder prims.

- Name: ``convertCylinders``
- Type: ``bool``
- Default Value: ``True``

Wall divisions
^^^^^^^^^^^^^^

The number of divisions to make around the cylinder wall.  Must be at least 3.

- Name: ``cylinderWallDivisions``
- Type: ``int``
- Default Value: ``32``
- Min Value: ``3.0``
- Max Value: ``1000.0``

Length divisions
^^^^^^^^^^^^^^^^

The number of end-to-end divisions to make along the cylinder.  Must be positive.

- Name: ``cylinderLatitudeDivisions``
- Type: ``int``
- Default Value: ``1``
- Min Value: ``1.0``
- Max Value: ``1000.0``

Generate endcaps
^^^^^^^^^^^^^^^^

Whether or not to add endcaps to generated cylinder meshes.

- Name: ``cylinderEndcaps``
- Type: ``bool``
- Default Value: ``True``

Convert capsule primitives
^^^^^^^^^^^^^^^^^^^^^^^^^^

Whether or not to generate meshes from capsule prims.

- Name: ``convertCapsules``
- Type: ``bool``
- Default Value: ``True``

Radial divisions
^^^^^^^^^^^^^^^^

The number of divisions around the capsule. Must be at least 3.

- Name: ``capsuleRadialDivisions``
- Type: ``int``
- Default Value: ``32``
- Min Value: ``3.0``
- Max Value: ``1000.0``

Length divisions
^^^^^^^^^^^^^^^^

The number of lengthwise divisions along the cylindrical body. Must be positive.

- Name: ``capsuleLengthDivisions``
- Type: ``int``
- Default Value: ``1``
- Min Value: ``1.0``
- Max Value: ``1000.0``

Cap latitude divisions
^^^^^^^^^^^^^^^^^^^^^^

The number of latitude divisions per hemispherical endcap. Must be positive.

- Name: ``capsuleCapDivisions``
- Type: ``int``
- Default Value: ``8``
- Min Value: ``1.0``
- Max Value: ``1000.0``

Convert cone primitives
^^^^^^^^^^^^^^^^^^^^^^^

Whether or not to generate meshes from cone prims.

- Name: ``convertCones``
- Type: ``bool``
- Default Value: ``True``

Side divisions
^^^^^^^^^^^^^^

The number of divisions to make around the side of the cone.  Must be at least 3.

- Name: ``coneSideDivisions``
- Type: ``int``
- Default Value: ``64``
- Min Value: ``3.0``
- Max Value: ``1000.0``

Length divisions
^^^^^^^^^^^^^^^^

The number of divisions to make along the length of the cone.  Must be positive.

- Name: ``coneLengthDivisions``
- Type: ``int``
- Default Value: ``3``
- Min Value: ``1.0``
- Max Value: ``1000.0``

Generate bases
^^^^^^^^^^^^^^

Whether or not to add a base to generated cone meshes.

- Name: ``coneBases``
- Type: ``bool``
- Default Value: ``True``

Convert cube primitives
^^^^^^^^^^^^^^^^^^^^^^^

Whether or not to generate meshes from cube prims.

- Name: ``convertCubes``
- Type: ``bool``
- Default Value: ``True``

