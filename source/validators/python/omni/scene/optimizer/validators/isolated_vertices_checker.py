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


# @register_requirements(cap.GeometryRequirements.VG_018, override=True)
@register_rule("Omni:Geometry")
class IsolatedVerticesChecker(BaseSceneOptimizerChecker):
    """
    Check mesh prims for isolated vertices, returns all prims as a single warning with an option to fix via scene optimizer operation.
    """

    OPERATION_NAME: str = "meshCleanup"

    @classmethod
    def _mesh_fix_isolated_vertices(cls, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Cleanup meshes by removing isolated vertices using Scene Optimizer
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
                    "removeIsolatedVertices": True,
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
        meshesWithIsolatedVertices = analysis_data["meshesWithIsolatedVertices"]

        if meshesWithIsolatedVertices > 0:
            suffix = "es" if meshesWithIsolatedVertices > 1 else ""
            message: str = f"Found {meshesWithIsolatedVertices} mesh{suffix} with isolated vertices to fix"
            self._AddWarning(
                # requirement=cap.GeometryRequirements.VG_018,
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Fix isolated vertices using Scene Optimizer",
                    callable=partial(self._mesh_fix_isolated_vertices),
                ),
            )
