# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from typing import List

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.GeometryRequirements.VG_015)
@register_rule("Usd:Performance")
class RedundantTimeSamplesChecker(BaseSceneOptimizerChecker):
    """
    Uses Scene Optimizer to analyze a scene checking for redundant time samples.
    """

    OPERATION_NAME: str = "optimizeTimeSamples"

    @classmethod
    def _remove_redundant_timesamples(cls, usdStage: Usd.Stage, attr: Usd.Attribute):
        """Use Scene Optimizer to fix the specified attribute"""

        # Configure operation
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(cls.OPERATION_NAME, args={"attributePaths": [str(attr.GetPath())]}),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """Check a stage for redundant time samples"""

        for attr_path, values in analysis_data.items():
            redundant: int = values[0]
            total: int = values[1]

            suffix = "" if redundant == 1 else "s"
            message: str = f"Attribute {attr_path} has {redundant}/{total} redundant time sample{suffix}"
            self._AddWarning(
                message=message,
                at=usdStage.GetAttributeAtPath(attr_path),
                suggestion=Suggestion(
                    message="Remove redundant time samples using Scene Optimizer",
                    callable=self._remove_redundant_timesamples,
                ),
            )
