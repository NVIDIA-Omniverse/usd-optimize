================
GPU Acceleration
================

Several operations have a CUDA path next to their CPU one. Without CUDA (no NVIDIA GPU or driver, or
``CUDA_VISIBLE_DEVICES=-1``) they run on the CPU, so one configuration works on any machine. Both paths give
the same results unless noted. How much the GPU gains depends on the hardware and on mesh size.

Which Operations Use the GPU
----------------------------

Each entry gives the switch and its default.

- :doc:`operations/findOccludedMeshes`: ``useGpu`` (hidden, on). Leave it on: same meshes, much faster.
- :doc:`operations/findOverlappingMeshes`: ``useGpu`` (on). Leave it on: faster than ``useParallelCpu``, by far on
  large assets, but it can miss meshes that only touch. Without CUDA, set ``useParallelCpu``.
- :doc:`operations/deduplicateGeometry`, fuzzy mode: ``useGpu`` (hidden, off). Leave it off: no faster, and
  it can group meshes that differ by more than ``tolerance``.
- :doc:`operations/fitPrimitives`: ``gpuFaceCountThreshold`` (``0``, off). Set it to ``100000``: faster
  above it, same fits.
- :doc:`operations/subdivideMeshes`: ``gpuFaceCountThreshold`` (4,000 output faces). Keep it: much faster above it.
- :doc:`operations/decimateMeshes`: ``gpuVertexCountThreshold`` (500K vertices). Keep it: faster above it.
- :doc:`operations/remeshMeshes`: ``gpuVertexCountThreshold`` (hidden, 500K vertices). Keep it: faster above it,
  slower below.
- :doc:`operations/triangulateMeshes` (``gpuVertexCountThreshold``, hidden, 1M vertices) and
  :doc:`operations/generateNormals` (``gpuThreshold``, 500K normals): little or no gain; keep the defaults.

Thresholds are per mesh, and keep small meshes on the CPU on purpose: there the CPU is as fast, and sending
every mesh to the GPU (a threshold of ``0``, or ``1`` for ``fitPrimitives``) has caused intermittent aborts.
Hidden arguments are set like visible ones, e.g. ``-a useGpu=0`` on the CLI.

Validators
----------

The :doc:`performance-validators` run the same operations:

- Occluded meshes: the GPU when available, else the CPU; ``--parameter USE_GPU=false`` forces the CPU.
- Primitive fit: pass ``--parameter GPU_FACE_COUNT_THRESHOLD=100000`` on a GPU host.
- Overlapping meshes: the GPU when available, else the much slower single-threaded CPU path.
- Fuzzy duplicate geometry: always the CPU comparison.

The occluded rule on assets with thousands of meshes, and the overlapping rule on tens of millions of faces, can
take a very long time. Skip them there with ``-D UsdOptimizeOccludedMeshesChecker`` and
``-D UsdOptimizeFindOverlappingMeshesChecker``.

Caveats
-------

- **First use compiles code.** The GPU code of ``findOccludedMeshes``, ``findOverlappingMeshes`` and fuzzy
  ``deduplicateGeometry`` is built for Turing, so newer GPUs compile it on first use (a one-time delay;
  the driver caches the result up to ``CUDA_CACHE_MAXSIZE``), and Volta or older cannot run it. Where it cannot run,
  ``findOccludedMeshes`` reruns on the CPU, but the other two log an error and report nothing found; turn
  ``useGpu`` off for them, or run the validators with ``CUDA_VISIBLE_DEVICES=-1``.
- **Out of GPU memory,** ``findOccludedMeshes`` reruns on the CPU, much more slowly; lower
  ``maximumGridResolution`` to stay on the GPU.
- **WSL2** runs GPU work much slower than native Windows or Linux.
- **Forked processes (Linux):** a worker forked from a process that already used the GPU gets an unusable CUDA
  context (``CUDA error: initialization error``). ``nvidia_usd_validate --process`` on its own is not affected.
