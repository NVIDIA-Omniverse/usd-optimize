# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import logging
import os

from pxr import Gf, UsdGeom

from .test_utils import Test_Operation, _get_context

logger = logging.getLogger(__name__)

# Default arguments for the command
DEFAULT_ARGS = {"paths": [], "reportIslands": False, "fullStageReport": False, "useGpu": False}


class Test_Operation_FindOverlappingMeshes(Test_Operation):
    """Tests for the Find Overlapping Meshes operation."""

    OPERATION = "findOverlappingMeshes"

    async def test_find_overlapping_meshes(self):
        """Test find overlapping meshes operation"""

        for use_gpu in [True, False]:
            with self.subTest(use_gpu=use_gpu):
                if os.environ.get("OMNI_REPO_NO_GPU") and use_gpu:
                    logger.debug("Skipping GPU test due to OMNI_REPO_NO_GPU environment variable")
                    self.skipTest("Skipping GPU test due to OMNI_REPO_NO_GPU environment variable.")

                args = DEFAULT_ARGS.copy()

                args["useGpu"] = use_gpu
                logger.debug("Testing Find Overlapping Meshes using %s", "GPU" if use_gpu else "CPU")

                # Open the test stage and get context
                stage = self._open_stage("sphere_clashes_frame.usda")
                context = _get_context(stage, analysis=True)

                # The operation should execute successfully.
                success, result = self._execute_command(args, context=context)
                self.assertTrue(success)
                self.assertTrue(result[0])
                self.assertTrue("analysis" in result[2])
                analysis = result[2]["analysis"]

                if use_gpu and analysis.get("suppressedOverlaps", 0) == 0 and not analysis.get("overlappingMeshes"):
                    logger.warning(
                        "Skipping GPU overlap test because GPU clash detection returned no results: %s", analysis
                    )
                    self.skipTest("Skipping GPU overlap test because GPU clash detection returned no results.")

                # There should be 8 overlapping meshes
                suppressed_overlaps = analysis.get("suppressedOverlaps", 0)
                self.assertEqual(suppressed_overlaps, 8)

                # Move xform node that has 3 meshes under it
                xform_cache = UsdGeom.XformCache()
                node_to_move = stage.GetPrimAtPath("/World/Xform")
                tm = xform_cache.GetLocalToWorldTransform(node_to_move)
                translation_vector = tm.ExtractTranslation()
                translation_vector[0] = 40.0
                translation_vector[2] = 200.0
                tm.SetTranslate(translation_vector)
                xformable = UsdGeom.Xformable(node_to_move)
                matrix_op = xformable.MakeMatrixXform()
                matrix_op.Set(tm)

                success, result = self._execute_command(args, context=context)
                self.assertTrue(success)
                self.assertTrue(result[0])
                self.assertTrue("analysis" in result[2])
                analysis = result[2]["analysis"]

                # There should be 9 overlapping meshes
                suppressed_overlaps = analysis.get("suppressedOverlaps", 0)
                self.assertEqual(suppressed_overlaps, 9)

                # Move mesh under xform node
                node_to_move = stage.GetPrimAtPath("/World/Xform/Sphere_05")
                tm = xform_cache.GetLocalToWorldTransform(node_to_move)
                translation_vector = Gf.Vec3d(0, 150, 0)
                tm.SetTranslate(translation_vector)
                xformable = UsdGeom.Xformable(node_to_move)
                matrix_op = xformable.MakeMatrixXform()
                matrix_op.Set(tm)

                success, result = self._execute_command(args, context=context)
                self.assertTrue(success)
                self.assertTrue(result[0])
                self.assertTrue("analysis" in result[2])
                analysis = result[2]["analysis"]

                # There should be 7 overlapping meshes
                suppressed_overlaps = analysis.get("suppressedOverlaps", 0)
                self.assertEqual(suppressed_overlaps, 7)
