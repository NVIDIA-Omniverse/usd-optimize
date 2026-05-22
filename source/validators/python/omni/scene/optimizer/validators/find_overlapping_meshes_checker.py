# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
from omni.asset_validator import register_rule
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.GeometryRequirements.VG_017)
@register_rule("Usd:Performance")
class FindOverlappingMeshesChecker(BaseSceneOptimizerChecker):
    """
    Check stage for overlapping meshes.
    """

    OPERATION_NAME: str = "findOverlappingMeshes"
    OPERATION_ARGS = {"fullStageReport": True}

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Scene Optimizer mesh overlap detection analysis.

        Args:
            usdStage: The USD stage being validated.
            analysis_data: Dictionary containing overlap analysis results from the
                findOverlappingMeshes operation.
        """

        # Retrieve analysis data
        suppressed_overlaps = analysis_data.get("suppressedOverlaps", 0)
        overlapping_meshes = analysis_data.get("overlappingMeshes", [])

        # If the analysis is suppressed, add a warning and return
        if suppressed_overlaps:
            self._AddWarning(
                message=(
                    f"Found {suppressed_overlaps} overlapping meshes in the stage."
                    "\nSelect the meshes in the viewer to visualize and eliminate overlaps.\n"
                )
            )
            return

        if len(overlapping_meshes) == 0:
            return

        all_prims = [usdStage.GetPrimAtPath(path) for path in overlapping_meshes]

        self._AddWarning(
            message=f"Found {len(overlapping_meshes)} overlapping meshes in the stage.",
            at=all_prims,
        )
