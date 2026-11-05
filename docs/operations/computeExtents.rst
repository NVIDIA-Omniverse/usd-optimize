.. AUTO GENERATED FILE - DO NOT EDIT

===============
Compute Extents
===============

**Key**: ``computeExtents``

This will compute/recompute and author the ``extents`` property for meshes. If the ``meshPrimPaths`` option is empty, all prims in the stage will be computed.

Extents are the axis aligned bounding boxes of the meshes, these do not always exist in a USD file. The extents can be used to improve scene performance since they allow the application to know the exact bounds of an object. Running this operation on an imported stage can potentially help improve overall render and stage traversal performance.

Arguments
---------

Meshes To Process
^^^^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

