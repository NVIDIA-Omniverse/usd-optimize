==========
Operations
==========

The following is the catalog of optimization operations provided by Usd Optimize.
Each operation is identified by a string **key** (shown on each operation's page) and
declares a set of typed arguments that are supplied as a JSON object / Python
dictionary.

For almost every operation, a ``paths`` (or ``meshPrimPaths`` / ``primPaths``)
argument selects which prims to process. **If left empty, the whole stage is
processed.** Path expressions are supported. Prim paths with non-ASCII
characters, such as ``/World/Défaut``, work only when every entry is a plain
prim path: a pattern such as ``/World/Dé*``, or a non-ASCII path listed with
any pattern, selects nothing.

.. Caution:: Operations work against the current set of selected variants. To
   optimize multiple variants, apply optimizations to each variant
   individually — for example by externalizing the contents of each variant to
   separate USD assets, optimizing each, and referencing the results back into
   the desired variant compositions.

.. Note:: **Native CLI vs. Python host.** Most operations are built-in (C++)
   plugins available everywhere Usd Optimize runs. Four operations —
   ``pythonScript``, ``deleteHiddenPrims``, ``removeUntypedPrims``, and
   ``moveMaterials`` — are implemented as Python plugins. These require a
   Python-hosted runtime and are
   available through the ``usd-optimize`` Python wheel / bindings, but **not**
   from the standalone ``bin/usdOptimize`` CLI, which does not initialize a
   Python interpreter. Invoking one of them from the native CLI reports
   ``invalid operation specified``. See :doc:`cli` and :doc:`python`.

.. GENERATED_DOCS_BEGIN - do not edit manually - see tools/repoman/docs_gen.py
.. toctree::
    :hidden:
    :maxdepth: 1
    :caption: Operations

    Auto UV Unwrap <operations/generateAtlasUVs>
    Box Clip <operations/boxClip>
    Compute Extents <operations/computeExtents>
    Compute Pivot <operations/pivot>
    Conform Meshes <operations/conformMeshes>
    Count Vertices <operations/countVertices>
    Decimate Meshes <operations/decimateMeshes>
    Deduplicate Geometry <operations/deduplicateGeometry>
    Deduplicate Hierarchies <operations/deduplicateHierarchies>
    Delete Hidden Prims <operations/deleteHiddenPrims>
    Delete Prims <operations/deletePrims>
    Dice Meshes <operations/diceMeshes>
    Edit Stage Metrics <operations/editStageMetrics>
    Find Coincident Geometry <operations/findCoincidingGeometry>
    Find Flat Hierarchies <operations/findFlatHierarchies>
    Find Occluded Meshes <operations/findOccludedMeshes>
    Find Overlapping Meshes <operations/findOverlappingMeshes>
    Fit Primitives <operations/fitPrimitives>
    Flatten Hierarchy <operations/flattenHierarchy>
    Generate Normals <operations/generateNormals>
    Generate Projection UVs <operations/generateProjectionUVs>
    Generate Scene <operations/generateScene>
    Manifold Meshes <operations/manifoldMeshes>
    Merge Physics Body Meshes <operations/mergePhysicsBodyMeshes>
    Merge Static Meshes <operations/merge>
    Merge Vertices <operations/mergeVertices>
    Mesh Cleanup <operations/meshCleanup>
    Move Materials <operations/moveMaterials>
    Optimize Materials <operations/optimizeMaterials>
    Optimize Primvars <operations/optimizePrimvars>
    Optimize Skeleton Roots <operations/optimizeSkelRoots>
    Optimize Time Samples <operations/optimizeTimeSamples>
    Organize Prototypes <operations/organizePrototypes>
    Primitives to Meshes <operations/primitivesToMeshes>
    Prune Leaves <operations/pruneLeaves>
    Python Script <operations/pythonScript>
    RTX Mesh Count <operations/rtxMeshCount>
    Remesh Meshes <operations/remeshMeshes>
    Remove Attributes <operations/removeAttributes>
    Remove Prims <operations/removePrims>
    Remove Small Geometry <operations/removeSmallGeometry>
    Remove Untyped Prims <operations/removeUntypedPrims>
    Remove Unused UVs <operations/removeUnusedUVs>
    Shrinkwrap <operations/shrinkwrap>
    Sparse Meshes <operations/sparseMeshes>
    Split Meshes <operations/splitMeshes>
    Stats <operations/printStats>
    Subdivide Meshes <operations/subdivideMeshes>
    Triangulate Meshes <operations/triangulateMeshes>
    Utility Function <operations/utilityFunction>


.. table::
    :widths: 30 70

    ======================================================================== ===========================================================================================================================================================================================================================
    Operation                                                                Description
    ======================================================================== ===========================================================================================================================================================================================================================
    :doc:`Auto UV Unwrap<operations/generateAtlasUVs>`                       Generate *texture coordinates* for meshes using unwrap methods with low distortion.
    :doc:`Box Clip<operations/boxClip>`                                      Clips mesh prims to the provided world-space axis-aligned bounding-box.
    :doc:`Compute Extents<operations/computeExtents>`                        Compute and author the ``extent`` property for meshes.
    :doc:`Compute Pivot<operations/pivot>`                                   Center the pivot of each mesh.
    :doc:`Conform Meshes<operations/conformMeshes>`                          Make meshes compatible with each other so that merge can combine them.
    :doc:`Count Vertices<operations/countVertices>`                          Create a report of prims with excessive vertex counts.
    :doc:`Decimate Meshes<operations/decimateMeshes>`                        Reduce tessellation density for mesh prims.
    :doc:`Deduplicate Geometry<operations/deduplicateGeometry>`              Convert identical meshes into instances.
    :doc:`Deduplicate Hierarchies<operations/deduplicateHierarchies>`        Find duplicate prim hierarchies and replace duplicates with instances.
    :doc:`Delete Hidden Prims [Python only]<operations/deleteHiddenPrims>`   Deletes all prims that are constantly hidden.
    :doc:`Delete Prims<operations/deletePrims>`                              Deletes prims from a stage.
    :doc:`Dice Meshes<operations/diceMeshes>`                                Dice meshes to a given regular grid or an irregular one.
    :doc:`Edit Stage Metrics<operations/editStageMetrics>`                   Set the ``metersPerUnit`` and/or ``upAxis`` of a stage.
    :doc:`Find Coincident Geometry<operations/findCoincidingGeometry>`       Identify geometry that share the same location based on a tolerance metric.
    :doc:`Find Flat Hierarchies<operations/findFlatHierarchies>`             Finds prims that have more than a specified number of children.
    :doc:`Find Occluded Meshes<operations/findOccludedMeshes>`               Finds meshes that are globally occluded meaning they are occluded from any camera that does not cross meshes in the scene.
    :doc:`Find Overlapping Meshes<operations/findOverlappingMeshes>`         Finds meshes that overlap with each other and displays the overlapping regions for selected meshes.
    :doc:`Fit Primitives<operations/fitPrimitives>`                          Replace meshes in a stage with transformed primitive geometries (sphere, cylinder, cone, or cube) if they can be fit within tolerance.
    :doc:`Flatten Hierarchy<operations/flattenHierarchy>`                    Finds and removes redundant Xforms to reduce prim count.
    :doc:`Generate Normals<operations/generateNormals>`                      Generate normals for meshes.
    :doc:`Generate Projection UVs<operations/generateProjectionUVs>`         Generate *texture coordinates* for meshes using various projection methods.
    :doc:`Generate Scene<operations/generateScene>`                          Generates a USD stage to use for benchmarking and testing using reference prims from the incoming stage.
    :doc:`Manifold Meshes<operations/manifoldMeshes>`                        Makes mesh Manifold.
    :doc:`Merge Physics Body Meshes<operations/mergePhysicsBodyMeshes>`      Prepare and merge visual and collision meshes within each rigid body while preserving articulation. Analysis discovers candidate geometry and output scopes.
    :doc:`Merge Static Meshes<operations/merge>`                             Merge individual meshes.
    :doc:`Merge Vertices<operations/mergeVertices>`                          This operation merges vertices that are closer to one another than a given tolerance, followed by removing any degenerate faces and optionally making the resulting mesh be manifold and/or removing any isolated vertices.
    :doc:`Mesh Cleanup<operations/meshCleanup>`                              Applies various cleanups to meshes.
    :doc:`Move Materials [Python only]<operations/moveMaterials>`            Moves all materials under a prim location such as "/World/Looks" and can also set the root prim of the path (e.g. "/World") as the default prim
    :doc:`Optimize Materials<operations/optimizeMaterials>`                  Run operations to optimize materials in a stage.
    :doc:`Optimize Primvars<operations/optimizePrimvars>`                    Flatten or index primvars, or check whether they can be simplified, for example reducing from faceVarying to uniform..
    :doc:`Optimize Skeleton Roots<operations/optimizeSkelRoots>`             Merge all meshes for meshes attached to a skeleton. This can greatly improve character playback speed by optimizing scenes for GPU skinning computation.
    :doc:`Optimize Time Samples<operations/optimizeTimeSamples>`             Remove redundant time-samples from attributes in a stage.
    :doc:`Organize Prototypes<operations/organizePrototypes>`                Reparent internal scene-graph instance prototypes under a user-specified namespace.
    :doc:`Primitives to Meshes<operations/primitivesToMeshes>`               Replace gprim types sphere, cylinder, capsule, cone, or cube with a mesh approximation.
    :doc:`Prune Leaves<operations/pruneLeaves>`                              Prune unnecessary leaf grouping prims (``Scope``, ``Xform``) from a stage.
    :doc:`Python Script [Python only]<operations/pythonScript>`              Execute a user defined python script with access to the current USD stage.
    :doc:`RTX Mesh Count<operations/rtxMeshCount>`                           Analysis operation for counting the number of RTX Meshes in the stage and how many are unique.
    :doc:`Remesh Meshes<operations/remeshMeshes>`                            Remesh existing mesh prims to user defined tolerance.
    :doc:`Remove Attributes<operations/removeAttributes>`                    Remove attributes from prims
    :doc:`Remove Prims<operations/removePrims>`                              Finds various prims that can be removed from that stage.
    :doc:`Remove Small Geometry<operations/removeSmallGeometry>`             Identifies and removes small and/or degenerate geometry from a USD stage.
    :doc:`Remove Untyped Prims [Python only]<operations/removeUntypedPrims>` Removes untyped prims that are not under /Render.
    :doc:`Remove Unused UVs<operations/removeUnusedUVs>`                     Remove unused UV primvars.
    :doc:`Shrinkwrap<operations/shrinkwrap>`                                 Convert meshes to a level set volume and extract a watertight mesh. Useful for closing holes, simplifying topology, and creating LODs.
    :doc:`Sparse Meshes<operations/sparseMeshes>`                            Hidden operation used for analyzing the sparse meshes of a scene and suggesting optimizations
    :doc:`Split Meshes<operations/splitMeshes>`                              Split disjoint meshes into multiple mesh prims.
    :doc:`Stats<operations/printStats>`                                      Collect and display statistics about the contents of a USD stage
    :doc:`Subdivide Meshes<operations/subdivideMeshes>`                      Apply Catmull-Clark or Loop subdivision iterations to meshes.
    :doc:`Triangulate Meshes<operations/triangulateMeshes>`                  Converts polygonal meshes to triangle-only meshes.
    :doc:`Utility Function<operations/utilityFunction>`                      Helper functions to pre-process components for Usd Optimize operations.
    ======================================================================== ===========================================================================================================================================================================================================================


.. GENERATED_DOCS_END