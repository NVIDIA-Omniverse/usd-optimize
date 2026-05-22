# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

# import omni.capabilities as cap
from omni.asset_validator import register_rule
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.GeometryRequirements.VG_021)
@register_rule("Usd:Performance")
class HighVertexCountChecker(BaseSceneOptimizerChecker):
    """
    Check a stage for meshes with high or extreme vertex counts.
    """

    OPERATION_NAME: str = "countVertices"
    OPERATION_ARGS: dict = {}

    LEVEL_HIGH: int = 100000
    LEVEL_VERY_HIGH: int = 500000
    LEVEL_EXTREME: int = 1000000

    def _GetArgs(self):
        """Custom GetArgs function

        Allows configuring the thresholds when testing the operation
        """
        return {"high": self.LEVEL_HIGH, "veryHigh": self.LEVEL_VERY_HIGH, "extreme": self.LEVEL_EXTREME}

    def _GenerateWarning(self, prim: Usd.Prim, count: int, level: str):
        """Add a warning based on the prim/count"""

        self._AddWarning(message=f"Mesh has {level} vertex count ({count})", at=prim)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Scene Optimizer analysis of empty leaves and log warnings/suggestions.
        """

        for path, count in analysis_data["high"].items():
            self._GenerateWarning(usdStage.GetPrimAtPath(path), count, "high")

        for path, count in analysis_data["veryHigh"].items():
            self._GenerateWarning(usdStage.GetPrimAtPath(path), count, "very high")

        for path, count in analysis_data["extreme"].items():
            self._GenerateWarning(usdStage.GetPrimAtPath(path), count, "extreme")
