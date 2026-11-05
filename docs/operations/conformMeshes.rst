.. AUTO GENERATED FILE - DO NOT EDIT

==============
Conform Meshes
==============

**Key**: ``conformMeshes``

The conform meshes operation runs before ``merge``. Within each merge boundary it makes the
meshes compatible with each other, so that meshes ``merge`` would otherwise keep apart, or merge while losing data,
are merged.

Meshes are grouped as ``merge`` groups them, using the same ``mergePoint`` and ``considerMaterials`` arguments, but
ignoring the differences listed below. A mesh is only changed to match the meshes in its own group. Differences that
keep groups apart, for example ``subdivisionScheme``, ``orientation``, ``purpose``, applied schemas, a UV set ``st1`` or
``st2`` on only some meshes, normals on only some meshes when they are not generated, or the material when
``considerMaterials`` is on, are logged and left alone. The other ``merge`` grouping arguments are assumed to be at
their defaults.

It makes these changes, and logs each one:

- Normals or UVs that some meshes of a group have and others lack are generated for the others, with
  ``generateNormals`` and ``generateProjectionUVs`` at their default settings. Nothing is generated when no mesh of the
  group has them, as the meshes merge as they are. The projected UVs fill the gap so the meshes merge, they are not
  authored to suit a textured material. Normals are only generated when the meshes' ``subdivisionScheme`` is
  ``none``, as USD says normals should not be authored on a subdivision surface, which computes its own. An unauthored
  ``subdivisionScheme`` is ``catmullClark``. A mesh has normals if ``normals`` or ``primvars:normals`` is authored on
  it. Generated normals are written to ``primvars:normals``, and ``merge`` combines them with the ``normals`` attribute
  of other meshes, each face keeping its own normals.
- A UV set named ``st`` on some meshes and ``st0`` on others is renamed to the name most of the meshes use (``st`` if
  the counts are equal). Generated UVs take that name and are not counted. A mesh whose bound material reads its UV
  set by name, through a ``UsdPrimvarReader_float2``, keeps that name, and when all such meshes use the same name the
  other meshes are renamed to it. A material that picks its UVs another way, for example by texture coordinate index,
  is not checked.
- A primvar that some meshes have and others lack is added to the others as a constant value, taken from
  ``primvarDefaults`` or else zero for numbers and empty for strings and tokens. A missing ``displayColor`` is filled
  with 0.18 grey and a missing ``displayOpacity`` with 1. A default that does not fit the primvar's type, including a
  number outside the type's range, ``inf`` or ``nan``, is logged and not used.
- Where ``doubleSided`` differs, it is set to true on every mesh in the group. A single-sided mesh therefore shows
  its back faces after the merge.

Meshes that still lack normals or UVs that others in the group have, because they were not generated, are only renamed,
filled and set double-sided to match each other, as ``merge`` keeps them apart from the rest.

It also logs, and leaves alone, the differences within a group it cannot reconcile: primvars with the same name but a
different value type, primvars of a type ``merge`` cannot combine or with an element size above 1, a mesh that has both
``st`` and ``st0``, a UV set that cannot be renamed because its material reads it by name, its values or indices have
time samples, or it is authored in a layer other than the edit target, UVs that some meshes lack when ``generateUVs`` is
off, normals that some meshes lack when ``generateNormals`` is off or the meshes are subdivision surfaces, and normals or
UVs that could not be generated for a mesh.
``merge`` still keeps such meshes apart, or drops the primvar.

Recommended pipelines
---------------------

``conformMeshes`` then ``merge``.

Starting configurations
-----------------------

Make every mesh under the stage mergeable:

.. code-block:: json

    [{"operation": "conformMeshes"}, {"operation": "merge"}]


Arguments
---------

Meshes to Conform
^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``meshPrimPaths``
- Type: ``[string]``
- Default Value: ``[]``

Generate Normals
^^^^^^^^^^^^^^^^

Generate normals for meshes that lack them when other meshes in the same merge boundary have them. Normals are not generated for subdivision surfaces, only for subdivisionScheme none

- Name: ``generateNormals``
- Type: ``bool``
- Default Value: ``True``

Generate UVs
^^^^^^^^^^^^

Generate UVs for meshes that lack them when other meshes in the same merge boundary have them

- Name: ``generateUVs``
- Type: ``bool``
- Default Value: ``True``

Primvar Defaults
^^^^^^^^^^^^^^^^

Values to give a primvar on the meshes that lack it, as name=value, for example tint=0.5,0.5,0.5. A primvar with no default gets zero, or empty for strings and tokens

- Name: ``primvarDefaults``
- Type: ``[string]``
- Default Value: ``[]``

Merge Boundary
^^^^^^^^^^^^^^

The boundary of where to merge meshes, for example, only merge meshes within a model

- Name: ``mergePoint``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Stage``
  - ``7: Root Prim``
  - ``8: Parent Prim``
  - ``1: Parent Xform``
  - ``9: Original Prim``
  - ``2: Kind: Assembly``
  - ``3: Kind: Group``
  - ``4: Kind: Component``
  - ``5: Kind: Model``
  - ``6: Kind: Subcomponent``


Keep Materials Separate
^^^^^^^^^^^^^^^^^^^^^^^

Whether separate mesh prims will be created for each material that can be merged. When off, meshes with differing materials can be merged under a single mesh prim using GeomSubsets for each material.

- Name: ``considerMaterials``
- Type: ``bool``
- Default Value: ``False``

