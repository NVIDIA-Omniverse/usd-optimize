# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from unittest import TestCase

from pxr import Plug, Sdr


class Test_StagedPlugins(TestCase):
    """Assert every registered USD plugin can actually load from the staged tree.

    Plugins are registered from ``plugInfo.json`` without their binaries being
    touched, so one staged without its runtime dependencies stays invisible
    until something first demands it -- no load-time error, no warning.

    That is how the MaterialX staging gap survived: ``usd_usdMtlx`` was copied
    into ``extraLibs/`` from the USD package's ``lib/`` while the MaterialX
    runtime it links against lives in the package's ``bin/``, and nothing in Usd
    Optimize resolves shader nodes, so no test ever triggered the load. It only
    surfaced when an external harness ran ``usdchecker`` against a package.

    Forcing every plugin to load turns that whole class of staging gap into an
    immediate failure, and it does so in the configuration that matters: the
    suite runs from the ``usd_optimize_tests`` archive, which ships
    ``extraLibs/`` and not the USD package's ``bin/``.
    """

    def test_all_registered_plugins_load(self):
        """Every registered plugin resolves its runtime dependencies."""
        plugins = Plug.Registry().GetAllPlugins()
        self.assertGreater(len(plugins), 0, msg="no USD plugins registered; the staged tree looks wrong")

        failures = []
        for plugin in plugins:
            try:
                plugin.Load()
            except Exception as exc:
                # Collect every failure rather than stopping at the first, so a
                # staging mistake reports its full blast radius in one run.
                failures.append(f"{plugin.name} ({plugin.path}): {exc}")

        self.assertEqual(failures, [], msg="plugins failed to load:\n" + "\n".join(failures))

    def test_usdshaders_is_staged(self):
        """UsdPreviewSurface resolves in Sdr, so the Sdr validators check shaders instead of failing them all."""
        # USD keeps usdShaders in plugin/usd, which copying its lib/ misses.
        self.assertIsNotNone(Plug.Registry().GetPluginWithName("usdShaders"), msg="usdShaders is not staged")
        self.assertTrue(Sdr.Registry().GetShaderNodeByIdentifier("UsdPreviewSurface"))
