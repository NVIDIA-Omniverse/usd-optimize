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


# @register_requirements(cap.GeometryRequirements.VG_016, override=True)
@register_rule("Omni:Geometry")
class ColocatedVerticesChecker(BaseSceneOptimizerChecker):
    """
    Check mesh prims for colocated vertices, returns all prims with colocated vertices as a single warning with an option to fix via scene optimizer operation.
    """

    OPERATION_NAME: str = "meshCleanup"

    @classmethod
    def _mesh_merge_vertices(cls, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Cleanup meshes by merging vertices using Scene Optimizer
        """

        # Configure mesh cleanup
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME,
                args={
                    "mergeVertices": True,
                    "tolerance": 0.0,
                    "mergeBoundaries": True,
                    "mergeNeighbors": True,
                    "contractDegenerateEdges": False,
                    "removeDegenerateFaces": False,
                    "makeManifold": False,
                    "removeIsolatedVertices": False,
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
        meshesWithMergeableVertices = analysis_data["meshesWithMergeableVertices"]

        if meshesWithMergeableVertices > 0:
            suffix = "es" if meshesWithMergeableVertices > 1 else ""
            message: str = f"Found {meshesWithMergeableVertices} mesh{suffix} with mergeable vertices to fix"
            self._AddWarning(
                # requirement=cap.GeometryRequirements.VG_016,
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Fix meshes with coincident vertices using Scene Optimizer",
                    callable=partial(self._mesh_merge_vertices),
                ),
            )
