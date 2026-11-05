.. AUTO GENERATED FILE - DO NOT EDIT

================
Subdivide Meshes
================

**Key**: ``subdivideMeshes``

This operation performs either Catmull-Clark or Loop subdivision on meshes in a stage, replacing the topology with the subdivided result.
Mesh subsets are forwarded and floating-point data defined on corners and vertices will be interpolated.

Creases and corners may be described using the crease and corner index attributes of UsdGeomMesh.
See creaseIndices, creaseLengths, creaseSharpnesses, cornerIndices, and cornerSharpnesses.
The resulting mesh will have updated crease and corner attributes, reflecting edge and vertex forwarding from the original mesh to the result.  Also, the forwarded sharpnesses will be decremented by 1 with each subdivision iteration. Non-positive sharpnesses result in the removal of creases or corners.  This way the operation may be repeatedly applied to a mesh, leading to consistent sharpnesses of crease and corners regardless of how many iterations are performed with each execution of the operation.

Arguments
---------

Meshes to subdivide
^^^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

GPU face count threshold
^^^^^^^^^^^^^^^^^^^^^^^^

When a mesh will more than this number of faces after subdivision, use GPU algorithm

- Name: ``gpuFaceCountThreshold``
- Type: ``int``
- Default Value: ``4000``
- Min Value: ``0.0``

Maximum face count
^^^^^^^^^^^^^^^^^^

If the subdivided mesh would have more than this number of faces, it will not be generated

- Name: ``faceCountLimit``
- Type: ``int``
- Default Value: ``2000000``
- Min Value: ``4.0``

Subdivision Method
^^^^^^^^^^^^^^^^^^

Which subdivision method to use

- Name: ``method``
- Type: ``int``
- Default Value: ``0``
- Enum Values:

  - ``0: Catmull-Clark``
  - ``1: Loop``


Subdivision Iteration Count
^^^^^^^^^^^^^^^^^^^^^^^^^^^

The number of times to subdivide

- Name: ``iterationCount``
- Type: ``int``
- Default Value: ``1``
- Min Value: ``1.0``
- Max Value: ``10.0``

