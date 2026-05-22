# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from functools import partial
from typing import List

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker

# Scene Optimizer Constants
MODE_DEDUPLICATE = 0


# @register_requirements(cap.MaterialsRequirements.VM_D_001)
@register_rule("Usd:Performance")
class DuplicateMaterialsChecker(BaseSceneOptimizerChecker):
    """
    Uses Scene Optimizer to analyse a scene checking for duplicate materials.
    Scene Optimizer can then fix by deduplicating them.
    """

    OPERATION_NAME: str = "optimizeMaterials"

    @classmethod
    def _deduplicate_materials(cls, usdStage: Usd.Stage, prim: Usd.Prim, duplicates: list) -> None:

        # Configure Optimize Materials operation
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME,
                args={"materialPrimPaths": duplicates, "mode": MODE_DEDUPLICATE, "analysisCheckPrimvars": False},
            ),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """Check a stage for duplicate materials"""

        # Duplicates will always exist in the result, if we got this far.
        duplicate_groups: List[List[str]] = analysis_data["duplicates"]

        # Duplicate groups is a list of lists - each list is a set of duplicate
        # materials.
        for duplicates in duplicate_groups:

            # Consider the first material the "main" one, and everything else to be
            # a duplicate of that.
            material_paths: List[str] = sorted(duplicates)
            material_path: str = material_paths[0]
            material_prim: Usd.Prim = usdStage.GetPrimAtPath(material_path)
            count: int = len(duplicates) - 1

            message: str = ""
            if count == 1:
                message = f"There is 1 duplicate of {material_path}"
            else:
                message = f"There are {count} duplicates of {material_path}"

            self._AddWarning(
                message=message,
                at=material_prim,
                suggestion=Suggestion(
                    message="Deduplicate materials using Scene Optimizer",
                    callable=partial(self._deduplicate_materials, duplicates=material_paths),
                ),
            )
