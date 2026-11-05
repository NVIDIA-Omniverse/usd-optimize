.. AUTO GENERATED FILE - DO NOT EDIT

===================
Merge Static Meshes
===================

**Key**: ``merge``

The merge static meshes operation replaces multiple meshes that
share common properties with a single merged mesh. This reduces scene prim count
and can improve overall stage performance.

Spatial clustering can further subdivide merges to keep merged meshes a
reasonable size. The supported spatial modes are: None (no spatial grouping),
BoundingBox (group meshes within a distance threshold, capping cluster size),
VertexCount (cap the vertex count per merged mesh), and CoincidentBoundary (only
merge meshes that share a seam).

In CoincidentBoundary mode two meshes are merged only when they share at least
boundaryMinSharedVertices boundary vertices that coincide in world space within
boundaryTolerance, where a boundary vertex is one lying on a boundary edge (an
edge used by exactly one face). Connectivity is transitive, so a chain of
abutting meshes collapses into one, while meshes that share no seam are left
untouched. This targets bad CAD output where a single part is exported as
several abutting pieces, such as a washer split into two half-rings.

Meshes left unmerged
--------------------

Merge says how many prims it left meshes unmerged at. Generate a report, or
enable verbose logging, to see each one and why, for example
``Not merged (time-sampled transform): /World/Anim``. Meshes skipped together,
such as those below an invisible prim, are listed once under that prim.

Recommended pipelines
---------------------

Merge then ``computeExtents`` (to author bounds on the new prims); merge then ``shrinkwrap`` to turn a
group of parts into one watertight shell.

Starting configurations
-----------------------

Merge everything by material:

.. code-block:: json

    [{"operation": "merge", "considerMaterials": true}]

Spatial merge capped by vertex count:

.. code-block:: json

    [{"operation": "merge", "spatialMode": 2, "spatialVertexCount": 50000}]

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

Static Meshes to Merge
^^^^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider for merging

- Name: ``meshPrimPaths``
- Type: ``[string]``
- Default Value: ``[]``

Keep Materials Separate
^^^^^^^^^^^^^^^^^^^^^^^

Whether separate mesh prims will be created for each material that can be merged. When off, meshes with differing materials can be merged under a single mesh prim using GeomSubsets for each material.

- Name: ``considerMaterials``
- Type: ``bool``
- Default Value: ``False``

Compute Display Colors
^^^^^^^^^^^^^^^^^^^^^^

Set display color and opacity to values computed from the bound material

- Name: ``materialAlbedoAsVertexColors``
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

Allow Single Meshes
^^^^^^^^^^^^^^^^^^^

When enabled means a single mesh will still be run through the merge process

- Name: ``allowSingleMeshes``
- Type: ``bool``
- Default Value: ``False``

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
  - ``3: Coincident Boundary Vertices``


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

Boundary Tolerance
^^^^^^^^^^^^^^^^^^

Maximum distance, in world units, at which boundary-edge vertices are considered coincident when merging meshes that share a seam

- Name: ``boundaryTolerance``
- Type: ``float``
- Default Value: ``1e-05``
- Min Value: ``0.0``

Minimum Shared Vertices
^^^^^^^^^^^^^^^^^^^^^^^

Minimum number of coincident boundary vertices two meshes must share to be merged as a shared seam. Higher values require a longer shared boundary; the minimum of 2 avoids merging meshes that touch at a single point

- Name: ``boundaryMinSharedVertices``
- Type: ``int``
- Default Value: ``2``
- Min Value: ``1.0``

