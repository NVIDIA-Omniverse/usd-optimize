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


# @register_requirements(cap.GeometryRequirements.VG_019, override=True)
@register_rule("Omni:Geometry")
class ZeroAreaFacesChecker(BaseSceneOptimizerChecker):
    """
    Check mesh prims for any zero area faces, returns all prims that have zero area faces as single warning with an option to fix using the scene optimizer operation.
    """

    OPERATION_NAME: str = "meshCleanup"

    @classmethod
    def _mesh_remove_zero_area_faces(cls, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Cleanup meshes by removing zero area faces using Scene Optimizer
        """

        # TODO: Configure settings to remove zero area faces

        # Configure mesh cleanup
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME,
                args={
                    "mergeVertices": False,
                    "tolerance": 0.0,
                    "contractDegenerateEdges": True,
                    "removeDegenerateFaces": True,
                    "makeManifold": False,
                    "removeIsolatedVertices": False,
                    "mergeBoundaries": False,
                    "mergeNeighbors": False,
                    "removeDuplicateFaces": False,
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
        meshesWithDegenerateFaces = analysis_data["meshesWithDegenerateFaces"]

        if meshesWithDegenerateFaces > 0:
            suffix = "es" if meshesWithDegenerateFaces > 1 else ""
            message: str = f"Found {meshesWithDegenerateFaces} zero area faces mesh{suffix} to fix"
            self._AddWarning(
                # requirement=cap.GeometryRequirements.VG_019,
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Fix zero area faces meshes using Scene Optimizer",
                    callable=partial(self._mesh_remove_zero_area_faces),
                ),
            )
