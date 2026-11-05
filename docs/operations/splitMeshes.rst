.. AUTO GENERATED FILE - DO NOT EDIT

============
Split Meshes
============

**Key**: ``splitMeshes``

This operation finds meshes that contain multiple disjoint pieces (parts that share no
vertices) and replaces them with separate mesh prims, one per connected piece. It is the inverse of
:doc:`Merge Static Meshes<merge>` and is useful for debugging, isolating spatial outliers, and letting a
renderer cull pieces independently.

Starting configurations
-----------------------

Split disjoint pieces into separate prims:

.. code-block:: json

    [{"operation": "splitMeshes", "splitOn": 0}]

Split and re-cluster spatially by vertex count:

.. code-block:: json

    [{"operation": "splitMeshes", "spatialMode": 2, "spatialVertexCount": 50000}]

Merge boundaries
----------------

``mergePoint`` selects the boundary that meshes are grouped under. Only meshes sharing a boundary are
merged with each other, and the result is written under that boundary. Callers driving the operation
from JSON, the CLI or the API pass the integer value.

.. table::
   :widths: 10 25 65

   ===== ================== ================================================================================
   Value UI label           Merge boundary
   ===== ================== ================================================================================
   0     Stage              The pseudo-root, so the whole stage is a single boundary (default)
   1     Parent Xform       The first parent typed ``Xform``
   2     Kind: Assembly     The first parent of kind ``assembly``
   3     Kind: Group        The first parent of kind ``group``
   4     Kind: Component    The first parent of kind ``component``, falling back to ``group``
   5     Kind: Model        The first parent of kind ``model``
   6     Kind: Subcomponent The first parent of kind ``subcomponent``, then ``component``, then ``group``
   7     Root Prim          Each root prim, that is each direct child of the pseudo-root
   8     Parent Prim        The first parent, whatever its type
   9     Original Prim      Each mesh itself, with results written beside the original rather than under it
   ===== ================== ================================================================================

Kinds are matched through the kind registry, so a mode also matches any kind derived from the one it
names. The values above are elective boundaries layered on top of the structural boundaries that
always apply: the pseudo-root, prims that are not ``def``, prims composed into an instanceable prim,
prims that reset the xform stack, and prims with a time sampled transform or visibility.


Arguments
---------

Meshes to split
^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Split On
^^^^^^^^

The method by which to detect disjoint meshes

- Name: ``splitOn``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Vertices``
  - ``1: Geom Subsets``


Split Collocated Points
^^^^^^^^^^^^^^^^^^^^^^^

Should points that are collocated be considered part of a disjoint mesh

- Name: ``splitCollocatedPoints``
- Type: ``bool``
- Default Value: ``False``

Original Mesh Handling
^^^^^^^^^^^^^^^^^^^^^^

What to do with any meshes in the original scene that were split or merged

- Name: ``originalGeomOption``
- Type: ``int``
- Default Value: ``1``
- Enum Values:

  - ``0: Ignore``
  - ``1: Delete``
  - ``2: Deactivate``
  - ``3: Hide``


Spatial Clustering Mode
^^^^^^^^^^^^^^^^^^^^^^^

Enable spatial clustering of meshes by choosing a clustering method

- Name: ``spatialMode``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: None``
  - ``1: Bounding Box``
  - ``2: Vertex Count``


Keep Materials Separate
^^^^^^^^^^^^^^^^^^^^^^^

Whether separate mesh prims will be created for each material that can be merged. When off, meshes with differing materials can be merged under a single mesh prim using GeomSubsets for each material.

- Name: ``considerMaterials``
- Type: ``bool``
- Default Value: ``False``

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


Output Name
^^^^^^^^^^^

The output name to use for newly created merged meshes

- Name: ``rootPath``
- Type: ``str``
- Default Value: ``""``

Strict Attribute Mode
^^^^^^^^^^^^^^^^^^^^^

When enabled all additional attributes on prims must match for them to be merged

- Name: ``considerAllAttributes``
- Type: ``bool``
- Default Value: ``False``

Spatial Threshold
^^^^^^^^^^^^^^^^^

Maximum distance at which to consider meshes neighbors

- Name: ``spatialThreshold``
- Type: ``float``
- Default Value: ``10``

Spatial Max Size
^^^^^^^^^^^^^^^^

Maximum size that clustered meshes can be grouped in

- Name: ``spatialMaxSize``
- Type: ``float``
- Default Value: ``0``

Spatial Vertex Count
^^^^^^^^^^^^^^^^^^^^

Maximum number of vertices that to cluster together

- Name: ``spatialVertexCount``
- Type: ``int``
- Default Value: ``10000``

