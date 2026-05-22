# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from functools import partial

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.GeometryRequirements.VG_030)
@register_rule("Usd:Performance")
class ZeroExtentChecker(BaseSceneOptimizerChecker):
    """
    Uses Scene Optimizer to analyze a scene checking for geometry that has zero sized extents.
    """

    OPERATION_NAME: str = "removeSmallGeometry"

    OPERATION_ARGS = {
        "threshold": 0.0,
    }

    @classmethod
    def _optimize_stage(cls, usdStage: Usd.Stage, _: Usd.Prim, operation_configs: list) -> None:
        """
        Run scene optimizer using the results of the remove small geometry analysis
        """
        analysis.optimize(usdStage, operation_configs)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """Check a stage for zero extent geometry"""
        # Get the list of zero extent geometry prims
        zero_extent_paths = analysis_data.get("smallGeometry", [])

        if zero_extent_paths:
            self._AddWarning(
                message="Stage contains geometry with zero sized extents",
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Remove zero extent geometry using Scene Optimizer",
                    callable=partial(self._optimize_stage, operation_configs=self.suggested_operations),
                ),
            )

        # create issues for the zero extent geometry prims
        for prim_path in zero_extent_paths:
            self._AddWarning(
                message="Zero extent geometry found",
                at=usdStage.GetPrimAtPath(prim_path),
            )
