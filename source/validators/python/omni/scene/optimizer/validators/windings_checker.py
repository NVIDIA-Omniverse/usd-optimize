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


# @register_requirements(cap.GeometryRequirements.VG_029, override=True)
@register_rule("Usd:Performance")
class WindingsChecker(BaseSceneOptimizerChecker):
    """
    Finds and fixes meshes with inconsistent windings.
    """

    OPERATION_NAME: str = "meshCleanup"

    @classmethod
    def _mesh_coorient(cls, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Reverse the winding order of the mesh in question
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
                    "removeDuplicateFaces": False,
                    "coorientFaces": True,
                    "paths": [str(prim.GetPath())],
                },
            ),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):

        # verify that the analysis data contains the meshes with inconsistent windings
        inconsistent_meshes = analysis_data.get("meshesWithInconsistentWindings")
        if inconsistent_meshes is None:
            self._AddFailedCheck(message="Analysis data does not contain any meshes with inconsistent windings")
            return

        # sort for consistency
        inconsistent_meshes.sort()

        # create the issues
        for prim_path in inconsistent_meshes:
            self._AddError(
                message="Mesh has inconsistent windings",
                at=usdStage.GetPrimAtPath(prim_path),
                suggestion=Suggestion(
                    message="Fix windings using Scene Optimizer",
                    callable=partial(self._mesh_coorient),
                ),
            )
