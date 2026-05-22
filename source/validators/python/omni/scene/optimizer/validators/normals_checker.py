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


# @register_requirements(cap.GeometryRequirements.VG_028, override=True)
@register_rule("Usd:Performance")
class NormalsChecker(BaseSceneOptimizerChecker):
    """
    Checks mesh prims for normals aligned to face orientation.

    Returns all prims with normals not aligned with face winding order as a single warning, with an option
    to fix using scene optimizer operations.

    """

    OPERATION_NAME: str = "generateNormals"

    @classmethod
    def _mesh_generate_normals(cls, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Generate normals using Scene Optimizer
        """

        # TODO: Configure settings to fix invalid normals
        #
        # NOTE: We can generate new normals, but it's not guaranteed to always give the result the user desired.

        # Configure generateNormals
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME,
                args={
                    "binding": 3,  # auto
                    "existingNormals": 0,  # fix
                    "sharpnessAngle": 60.0,
                    "weightmode": 0,  # weight by angle
                },
            ),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Scene Optimizer analysis of mesh cleanups
        """

        # Retrieve problem count
        totalNonUnitLengthStrict = analysis_data["totalNonUnitLengthStrict"]

        if totalNonUnitLengthStrict > 0:
            suffix = "es" if totalNonUnitLengthStrict > 1 else ""
            message: str = (
                f"Found {totalNonUnitLengthStrict} mesh{suffix} with normals that are not of length 1 within a tolerance of 1e-4"
            )
            self._AddWarning(
                # requirement=cap.GeometryRequirements.VG_028,
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Fix normals using Scene Optimizer",
                    callable=partial(self._mesh_generate_normals),
                ),
            )
