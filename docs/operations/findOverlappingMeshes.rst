.. AUTO GENERATED FILE - DO NOT EDIT

=======================
Find Overlapping Meshes
=======================

**Key**: ``findOverlappingMeshes``

This operation shows which meshes are overlapping in the scene.  The intersections are represented in the viewport as a graph connecting centroids of overlapping pairs.  The user may select any mesh in the viewport to see a detailed visualization of its intersections with other meshes. This is updated as the mesh is moved or scaled, allowing the user to quickly fix overlapping meshes in the scene.  This also works with multiple selected meshes or primitives with mesh descendants in the scene hierarchy.

Detection runs on the GPU when ``useGpu`` is set (the default) and CUDA is available.  Leave it on: it is faster than ``useParallelCpu``, by far on large assets, but can miss meshes that only touch a neighbour.  Without CUDA it falls back to the CPU, so set ``useParallelCpu``.  If the GPU code fails, the operation logs an error and reports no overlaps instead of falling back to the CPU.  Assets with tens of millions of faces can take a very long time, on the GPU or with ``useParallelCpu``.

Arguments
---------

Meshes to test
^^^^^^^^^^^^^^

Optional list of prim paths to consider

- Name: ``paths``
- Type: ``[string]``
- Default Value: ``[]``

Report islands
^^^^^^^^^^^^^^

If set, overlapping meshes will be grouped into islands.  Otherwise they will be grouped into overlap pairs.

- Name: ``reportIslands``
- Type: ``bool``
- Default Value: ``False``

Always list overlaps
^^^^^^^^^^^^^^^^^^^^

If set, individual overlaps will be reported even when 'paths' is empty (processing the full stage).  Otherwise overlaps will only be reported when 'paths' is not empty.

- Name: ``fullStageReport``
- Type: ``bool``
- Default Value: ``False``

Use GPU
^^^^^^^

If set, mesh overlap detection is performed on GPU.

- Name: ``useGpu``
- Type: ``bool``
- Default Value: ``True``

Use Parallel CPU
^^^^^^^^^^^^^^^^

If set and useGpu is False, the CPU implementation uses multiple threads (TBB) for point-clash and face-intersection passes.  Ignored when useGpu is True.

- Name: ``useParallelCpu``
- Type: ``bool``
- Default Value: ``False``

