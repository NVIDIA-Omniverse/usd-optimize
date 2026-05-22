Developer Guide
===============

1. Update your `deps/target-deps.packman.xml` to add an `scene_optimizer_core` dependency with `linkPath="../_build/target-deps/omni_scene_optimizer"`

2. Add a new file `deps/scene-optimizer-deps.packman.xml` with the following contents:

.. code-block:: xml

   <project toolsVersion="5.0">
     <import path="../_build/target-deps/omni_scene_optimizer/dev/deps/all-deps.packman.xml">
       <filter include="autouv-core" />
       <filter include="omnimesh_ops_usd" />
     </import>

     <dependency name="autouv-core" linkPath="../_build/target-deps/omni_autouv_core" tags="non-redist"/>
     <dependency name="omnimesh_ops_usd" linkPath="../_build/target-deps/omnimesh_ops_usd" tags="non-redist"/>
   </project>

3. Update your `repo.toml` to pull the new file. For example:

.. code-block:: toml

   [repo_build]
   fetch.packman_target_files_to_pull = [
       "${root}/deps/target-deps.packman.xml",
       "${root}/deps/scene-optimizer-deps.packman.xml",
   ]

4. Access the `use_scene_optimizer()` function in your premake by adding the following sections.

.. code-block:: lua

   ...
   scene_optimizer_build = require(path.replaceextension(os.matchfiles("_build/target-deps/omni_scene_optimizer/*/dev/tools/premake/scene-optimizer-public.lua")[1], ""))
   ...
   project "foo_bar"
       scene_optimizer_build.use_scene_optimizer()
   ...
