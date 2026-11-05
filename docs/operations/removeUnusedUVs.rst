.. AUTO GENERATED FILE - DO NOT EDIT

=================
Remove Unused UVs
=================

**Key**: ``removeUnusedUVs``

This operation attempts to find prims that have primvars describing UVs (texture coordinates) that appear to be unused, and then remove or block them.

As there is no way to 100% guarantee that a material does not use UVs, the operation makes assumptions. For any UV primvar that is identified the bound material is checked. If the material has a `UsdUVTexture` shader no primvars will be removed from the prim. If the material contains a `UsdPrimvarReader_float2` shader then the `varname` of it is checked, and no matching primvars will be removed as they are being used. Finally, if any of the child shaders contains an `Asset` or `AssetArray` typed shader input then the assumption is made that it is possibly a texture, and therefore UVs will not be removed from the prim.

Arguments
---------

Prim Paths
^^^^^^^^^^

A list of prim path expressions to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Mode
^^^^

What to do with unused attributes

- Name: ``mode``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Remove``
  - ``1: Block``


UV Attributes
^^^^^^^^^^^^^

A list of custom UV attribute names to check

- Name: ``attributes``
- Type: ``[string]``
- Default Value: ``[]``

