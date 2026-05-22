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


# @register_requirements(cap.GeometryRequirements.VG_032)
@register_rule("Omni:Geometry")
class DuplicateFaceChecker(BaseSceneOptimizerChecker):
    """
    Check mesh prims for duplicate faces, returns all prims as a single warning with an option to fix via scene optimizer operation.
    """

    OPERATION_NAME: str = "meshCleanup"

    @classmethod
    def _mesh_fix_duplicate_faces(cls, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Cleanup meshes by fixing duplicate faces geometry using Scene Optimizer
        """

        # Configure mesh cleanup
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME,
                args={
                    "mergeVertices": False,
                    "tolerance": 0.0,
                    "contractDegenerateEdges": False,
                    "removeDegenerateFaces": False,
                    "makeManifold": False,
                    "removeIsolatedVertices": False,
                    "mergeBoundaries": False,
                    "mergeNeighbors": False,
                    "removeDuplicateFaces": True,
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
        meshesWithDuplicateFaces = analysis_data["meshesWithDuplicateFaces"]

        if meshesWithDuplicateFaces > 0:
            suffix = "es" if meshesWithDuplicateFaces > 1 else ""
            message: str = f"Found {meshesWithDuplicateFaces} mesh{suffix} with duplicate faces to fix"
            self._AddWarning(
                # requirement=cap.GeometryRequirements.VG_032,
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Fix duplicate faces using Scene Optimizer",
                    callable=partial(self._mesh_fix_duplicate_faces),
                ),
            )
