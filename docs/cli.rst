Command Line Interface Tool
===========================

Usd Optimize provides a command line interface (CLI) tool under
``bin/usdOptimize`` that allows you to run optimization operations on USD files
without writing any code.

.. Note:: **On Windows, run** ``bin\usdOptimize.bat``, not ``usdOptimize.exe``.
   The ``.bat`` sets up the library path; the bare ``.exe`` exits
   ``-1073741515`` (``STATUS_DLL_NOT_FOUND``). Name it explicitly, since
   ``PATHEXT`` resolves ``.exe`` first.

CLI usage is as follows:

.. Note:: The native CLI runs the built-in (C++) operations only, which is why
   the operation list below is a subset of the full :doc:`operations` catalog.
   The Python-plugin operations (``pythonScript``, ``deleteHiddenPrims``,
   ``removeUntypedPrims``, ``moveMaterials``) are unavailable here because the
   CLI binary does not initialize a Python interpreter; use the ``usd-optimize``
   Python wheel / bindings to run them.

.. GENERATED_DOCS_BEGIN - do not edit manually - see tools/repoman/docs_gen.py

.. code-block:: text

    Usage: usdOptimize [OPTIONS] input-stage

    Required Args:
      -i [ --input ] arg    The input stage to read

    Optional Args:
      -h [ --help ]                       Print this help
      -h [ --help ] operation             Print help specific to an operation

      -a [ --argument ] argument=value    Specify an argument for an operation, along with its value. Any arguments apply
                                          to the most recent operation that was specified. Array values can be specified by
                                          using a comma separated list.
      -an [ --analysis ]                  Run in analysis mode (unsupported operations will be skipped)
      -c [ --config ] arg                 JSON commands file
      -fl [ --flatten ]                   If enabled, flattens the stage before outputting, otherwise only exports the root
                                          layer.
      -j [ --json ] filename              Write any operation configuration to the specified JSON file
      -o [ --operation ] operation        Add an operation. Multiple operations can be specified, and will execute in the
                                          order they are provided. Adding arguments will apply to the most recently
                                          specified operation
      -r [ --report ]                     If enabled, generate a report describing what was done, warnings, etc
      -s [ --stats ]                      Capture stage stats before/after operations
      -rp [ --relativePaths ]             After exporting a stage, check for any asset paths that can be made relative
      -st [ --singleThreaded ]            Disable multi-threading in operations that support this option
      -v [ --verbose ]                    Enables verbose mode (extra stats, more logging, etc)
      -w [ --write ] filename             The output stage to write

    Available Operations:
    boxClip                computeExtents           conformMeshes             countVertices
    decimateMeshes         deduplicateGeometry      deduplicateHierarchies    deletePrims
    diceMeshes             editStageMetrics         findCoincidingGeometry    findFlatHierarchies
    findOccludedMeshes     findOverlappingMeshes    fitPrimitives             flattenHierarchy
    generateAtlasUVs       generateNormals          generateProjectionUVs     generateScene
    manifoldMeshes         merge                    mergePhysicsBodyMeshes    mergeVertices
    meshCleanup            optimizeMaterials        optimizePrimvars          optimizeSkelRoots
    optimizeTimeSamples    organizePrototypes       pivot                     primitivesToMeshes
    printStats             pruneLeaves              remeshMeshes              removeAttributes
    removePrims            removeSmallGeometry      removeUnusedUVs           rtxMeshCount
    shrinkwrap             sparseMeshes             splitMeshes               subdivideMeshes
    triangulateMeshes      utilityFunction

.. GENERATED_DOCS_END