# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from typing import List

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker

# Constants
MODE_BLOCK = 1


# @register_requirements(cap.GeometryRequirements.VG_033)
@register_rule("Usd:Performance")
class UnusedUVsChecker(BaseSceneOptimizerChecker):
    """
    Check a stage for unused texture coordinate primvars.
    """

    OPERATION_NAME: str = "removeUnusedUVs"
    OPERATION_ARGS: dict = {}

    @classmethod
    def _remove_uvs(cls, usdStage: Usd.Stage, attribute: Usd.Attribute) -> None:
        """
        Remove an unused UV attribute
        """

        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME, args={"paths": [str(attribute.GetPrimPath())], "mode": MODE_BLOCK}
            ),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Scene Optimizer analysis
        """

        for attribute_path in sorted(analysis_data):
            attribute: Usd.Attribute = usdStage.GetAttributeAtPath(attribute_path)
            self._AddWarning(
                message=f"Unused UV attribute",
                at=attribute,
                suggestion=Suggestion(
                    message="Remove the attribute",
                    callable=self._remove_uvs,
                    at=[attribute],
                ),
            )
