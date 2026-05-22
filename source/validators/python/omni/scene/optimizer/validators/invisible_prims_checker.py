# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from functools import partial

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker

# Constants
REMOVE_METHOD_DEACTIVATE = 2


# @register_requirements(cap.GeometryRequirements.VG_034)
@register_rule("Usd:Performance")
class InvisiblePrimsChecker(BaseSceneOptimizerChecker):
    """
    Uses Scene Optimizer to find invisible prims in the stage that can be deactivated instead.
    """

    OPERATION_NAME: str = "removePrims"

    OPERATION_ARGS = {
        "removeInvisible": True,
        "invisibleRemoveMethod": REMOVE_METHOD_DEACTIVATE,
        "removeOrphanedOvers": False,
    }

    @classmethod
    def _optimize_stage(cls, usdStage: Usd.Stage, _: Usd.Prim, operation_configs: list) -> None:
        """
        Run scene optimizer using the results of the remove prims analysis
        """
        analysis.optimize(usdStage, operation_configs)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """Check a stage for invisible prims"""
        invisible_prims = analysis_data.get("invisiblePrims", [])

        # create a suggested fix if there are invisible prims
        if invisible_prims:
            self._AddWarning(
                message="Stage contains invisible prims",
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Deactivate invisible prims using Scene Optimizer",
                    callable=partial(self._optimize_stage, operation_configs=self.suggested_operations),
                ),
            )

        # create issues for the invisible prims
        for prim_path in invisible_prims:
            self._AddWarning(
                message="Invisible prim found",
                at=usdStage.GetPrimAtPath(prim_path),
            )
