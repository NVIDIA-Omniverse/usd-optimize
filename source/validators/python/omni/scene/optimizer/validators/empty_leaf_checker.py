# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
from typing import List

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.HierarchyRequirements.HI_012)
@register_rule("Usd:Performance")
class EmptyLeafChecker(BaseSceneOptimizerChecker):
    """
    Check a stage for redundant leaf primitives (Scopes, Xforms).
    """

    OPERATION_NAME: str = "pruneLeaves"
    OPERATION_ARGS: dict = {"filterInactive": True}

    @classmethod
    def _remove_leaves(cls, usdStage: Usd.Stage, _: Usd.Stage) -> None:
        """
        Prune leaf prims via Scene Optimizer
        """

        # Configure operation
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(cls.OPERATION_NAME, args=cls.OPERATION_ARGS),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Scene Optimizer analysis of empty leaves and log warnings/suggestions.
        """

        leaves: int = len(analysis_data)
        suffix: str = "" if leaves == 1 else "s"
        message: str = f"Stage contains {leaves} empty leaf primitive{suffix}"

        self._AddWarning(
            message=message,
            at=usdStage,
            suggestion=Suggestion(
                message="Remove empty leaf primitives with Scene Optimizer", callable=self._remove_leaves, at=None
            ),
        )
