# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.GeometryRequirements.VG_008)
@register_rule("Usd:Performance")
class CoincidingGeometryChecker(BaseSceneOptimizerChecker):
    """
    Finds cases where two or more prims have coinciding geometry that exists within the same world space in the scene.
    """

    OPERATION_NAME: str = "findCoincidingGeometry"

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):

        # verify that the analysis data contains the coinciding meshes
        coinciding_meshes = analysis_data.get("coincidingGeometry")
        if coinciding_meshes is None:
            self._AddFailedCheck(message="Analysis data does not contain coinciding meshes")
            return

        issue_paths: list = []
        issue_data: dict = {}

        # iterate over coinciding meshes and group them by the primary path, so we can sort them next - this to
        # guarantee consistent order across runs
        for prim_paths in coinciding_meshes:
            # sort the paths first
            prim_paths.sort()
            issue_paths.append(prim_paths[0])
            issue_data[prim_paths[0]] = prim_paths

        # now sort the issues by the primary path
        issue_paths.sort()

        # create the issues
        for prim_path in issue_paths:
            prim_paths = issue_data[prim_path]
            self._AddWarning(
                message=f"{len(prim_paths)} coinciding geometries found at paths: [{', '.join(prim_paths)}]",
                at=usdStage.GetPrimAtPath(prim_path),
            )
