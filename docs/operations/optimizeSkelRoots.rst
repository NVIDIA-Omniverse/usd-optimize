.. AUTO GENERATED FILE - DO NOT EDIT

=======================
Optimize Skeleton Roots
=======================

**Key**: ``optimizeSkelRoots``

This operation will merge all meshes for meshes attached to a skeleton. This can greatly improve character playback speed by optimizing scenes for GPU skinning computation.

This is a good option to try if you have rigged characters that use ``UsdSkel``. Only meshes with matching attributes, primvars and applied schemas are merged together, so a skeleton can keep several meshes. Meshes with blend shapes or material GeomSubsets are not merged. Similar to merge static meshes, this will not significantly reduce memory usage.

