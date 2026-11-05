.. AUTO GENERATED FILE - DO NOT EDIT

==============
Generate Scene
==============

**Key**: ``generateScene``

Generates a USD stage to use for benchmarking and testing using reference prims from the incoming stage.

Arguments
---------

Seed
^^^^

The seed which will be used when generating random numbers

- Name: ``seed``
- Type: ``int``
- Default Value: ``123456789``

Reference Mesh Paths
^^^^^^^^^^^^^^^^^^^^

Prim paths of meshes to use as reference meshes for generating random geometry. If no paths are provided random meshes will not be generated. If a path is provided that is not a mesh its children will be searched.

- Name: ``referenceMeshPaths``
- Type: ``[string]``
- Default Value: ``[]``

Generate Mesh Path
^^^^^^^^^^^^^^^^^^

The path and name to use for newly generated meshes

- Name: ``generatedMeshPath``
- Type: ``str``
- Default Value: ``""``

Mesh Count
^^^^^^^^^^

The number of disjoint meshes that will be generated

- Name: ``meshCount``
- Type: ``int``
- Default Value: ``32``

Uniform Layout
^^^^^^^^^^^^^^

Whether the generated meshes will follow a uniform layout, if false generated meshes will be randomly spaced

- Name: ``uniformLayout``
- Type: ``bool``
- Default Value: ``False``

2D Layout
^^^^^^^^^

Whether the generated meshes will be placed in a 2D layout, if false meshes will be arranged in a 3D layout

- Name: ``2DLayout``
- Type: ``bool``
- Default Value: ``False``

Layout Spacing
^^^^^^^^^^^^^^

The distance between each generated mesh, this may be uniform or a hint for random layouts.

- Name: ``layoutSpacing``
- Type: ``float``
- Default Value: ``200``

Unique Mesh Percentage
^^^^^^^^^^^^^^^^^^^^^^

The percentage of randomly generated meshes that will have unique geometry.

- Name: ``uniqueMeshPercentage``
- Type: ``float``
- Default Value: ``0.5``
- Min Value: ``0.0``
- Max Value: ``1.0``

Scale Unique Meshes
^^^^^^^^^^^^^^^^^^^

Whether meshes with unique geometry will have a scale factor applied to their points.

- Name: ``scaleUniqueMeshes``
- Type: ``bool``
- Default Value: ``True``

Clustered Percent
^^^^^^^^^^^^^^^^^

The percentage of randomly generated meshes that will be clustered together.

- Name: ``clusteredPercent``
- Type: ``float``
- Default Value: ``0.75``
- Min Value: ``0.0``
- Max Value: ``1.0``

Number of Clusters
^^^^^^^^^^^^^^^^^^

The target number of clusters to group random geometry together in. Note: a higher number of clusters may be generated if the cluster max vertex counts are exceeded.

- Name: ``numClusters``
- Type: ``int``
- Default Value: ``16``

Material Paths
^^^^^^^^^^^^^^

Prim paths of materials to randomly assign to generated geometry. If no paths are provided no materials will be assigned. If a path is provided that is not a material its children will be searched.

- Name: ``materialPaths``
- Type: ``[string]``
- Default Value: ``[]``

