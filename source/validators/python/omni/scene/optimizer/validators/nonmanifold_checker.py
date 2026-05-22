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


# @register_requirements(cap.GeometryRequirements.VG_007, override=True)
@register_rule("Omni:Geometry")
class NonManifoldChecker(BaseSceneOptimizerChecker):
    """
    Check mesh prims for non-manifold geometry, returns all non-manifold prims as a single warning with an option to fix via scene optimizer operation.
    """

    OPERATION_NAME: str = "meshCleanup"

    @classmethod
    def _mesh_fix_nonmanifold(cls, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Cleanup meshes by fixing nonmanifold geometry using Scene Optimizer
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
                    "makeManifold": True,
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
        meshesThatAreNonManifolds = analysis_data["meshesThatAreNonManifolds"]

        if meshesThatAreNonManifolds > 0:
            suffix = "es" if meshesThatAreNonManifolds > 1 else ""
            message: str = f"Found {meshesThatAreNonManifolds} nonManifold mesh{suffix} to fix"
            self._AddWarning(
                # requirement=cap.GeometryRequirements.VG_007,
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Fix nonmanifold meshes using Scene Optimizer",
                    callable=partial(self._mesh_fix_nonmanifold),
                ),
            )
