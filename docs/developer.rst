Developer Guide
===============

This guide describes how to consume the published **Usd Optimize** package from
your own `packman <https://docs.omniverse.nvidia.com/kit/docs/repo_tools/latest/>`_
and `premake <https://premake.github.io/>`_ based build, so that you can link
against the library and call its C++ API.

To build Usd Optimize itself from source, or to consume a prebuilt binary drop
directly, see the repository ``README.md`` and the install guides under
``docs/install-prebuilt-linux.md`` and ``docs/install-prebuilt-windows.md``.
Platform-specific requirements of the source build are covered under
`Platform build notes`_ below.

Linking against the package
---------------------------

1. Update your ``deps/target-deps.packman.xml`` to add a ``usd_optimize``
   dependency with ``linkPath="../_build/target-deps/usd_optimize"``.

2. Add a new file ``deps/usd-optimize-deps.packman.xml`` with the following
   contents:

   .. code-block:: xml

      <project toolsVersion="5.0">
        <import path="../_build/target-deps/usd_optimize/dev/deps/all-deps.packman.xml">
          <filter include="autouv-core" />
          <filter include="omnimesh_ops_usd" />
        </import>

        <dependency name="autouv-core" linkPath="../_build/target-deps/omni_autouv_core" tags="non-redist"/>
        <dependency name="omnimesh_ops_usd" linkPath="../_build/target-deps/omnimesh_ops_usd" tags="non-redist"/>
      </project>

3. Update your ``repo.toml`` to pull the new file. For example:

   .. code-block:: toml

      [repo_build]
      fetch.packman_target_files_to_pull = [
          "${root}/deps/target-deps.packman.xml",
          "${root}/deps/usd-optimize-deps.packman.xml",
      ]

4. Access the ``use_usd_optimize()`` function in your premake by adding the
   following sections.

   .. code-block:: lua

      ...
      usd_optimize_build = require(path.replaceextension(os.matchfiles("_build/target-deps/usd_optimize/*/dev/tools/premake/usd-optimize-public.lua")[1], ""))
      ...
      project "foo_bar"
          usd_optimize_build.use_usd_optimize()
      ...

Calling the API
---------------

Once your project links against Usd Optimize, include the public header and
drive operations through the core singleton:

.. code-block:: cpp

   #include <usd_optimize/core/Core.h>

The public C++ interface is documented in the :doc:`../api/api` reference.
The equivalent Python entry points are described in :doc:`python`, and the
operation catalog (with JSON configuration examples that apply equally to the
C++ and Python paths) is in :doc:`operations`.

Extending Usd Optimize
----------------------

New optimizations are added as plugins that subclass ``usd_optimize::Operation``
and register themselves with the core library. The full plugin authoring guide
lives in ``PLUGINS.md`` in the repository root.

Platform build notes
--------------------

These apply when building Usd Optimize from source with ``./repo.sh build`` /
``repo.bat build``.

**(Windows) Host toolchain discovery.** The build links a **host-installed**
compiler (``msbuild.link_host_toolchain`` in ``repo.toml``), not packman
``msvc``. If discovery fails, set ``msbuild.vs_path`` / ``msbuild.vs_version``
(and ``msbuild.winsdk_path`` for the SDK) in ``repo.toml`` — see
`repo_build toolchains
<https://docs.omniverse.nvidia.com/kit/docs/repo_build/latest/docs/toolchains.html>`__.
A Visual Studio install without the C++ toolset is not enough: if
``Microsoft.VCToolsVersion.default.txt`` is missing under
``VC/Auxiliary/Build``, add the **Desktop development with C++** workload via
the Visual Studio Installer.

**(Windows, non-English locale) Set** ``PYTHONUTF8=1``. Repo tooling reads
UTF-8 config files, but Python on Windows decodes text using the system ANSI
code page (CP949, CP932, GBK), which surfaces as a ``UnicodeDecodeError``
during ``repo.bat build``. Enable UTF-8 mode:

.. code-block:: bat

   set PYTHONUTF8=1        :: current session
   setx PYTHONUTF8 1       :: persist for new shells

.. code-block:: powershell

   $env:PYTHONUTF8 = "1"   # current session

English/US Windows defaults to CP1252, which overlaps UTF-8 for ASCII and so
usually hides this; ``PYTHONUTF8=1`` is a harmless, safe default. See
`PEP 540 <https://peps.python.org/pep-0540/>`__.

**(Linux) Extra requirements for** ``./repo.sh py_package``. It needs
``patchelf`` (``sudo apt-get install patchelf``) for ``auditwheel repair``, and
outbound HTTPS/PyPI access — ``tools/pyproject/pybuild.sh`` pip-installs
Poetry, auditwheel, and related tools into
``_build/host-deps/py_package_venv/``. If that bootstrap fails partway, remove
the venv and rerun: ``rm -rf _build/host-deps/py_package_venv``.

**(Linux x86_64) C++11 ABI** is enabled via
``premake.linux_x86_64_cxx_abi`` in ``repo.toml``.
