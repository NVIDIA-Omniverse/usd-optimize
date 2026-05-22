# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from functools import partial

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# VG_004: no sparse mesh requirement
# VG_005: no large mesh requirement
# @register_requirements(cap.GeometryRequirements.VG_004, cap.GeometryRequirements.VG_005)
@register_rule("Usd:Performance")
class SparseMeshChecker(BaseSceneOptimizerChecker):
    """
    Finds mesh prims that are considered sparse, this can be due to density of the geometry volume in relation to the
    extent volume, or prims with many sparse disjoint meshes.
    These prims will be either identified as needing to be diced, split, or clustered together with other similar
    sparse meshes within the scene.
    """

    OPERATION_NAME: str = "sparseMeshes"

    @classmethod
    def _optimize_stage(cls, usdStage: Usd.Stage, _: Usd.Prim, operation_configs: list) -> None:
        """
        Run scene optimizer using the results of the sparse mesh analysis
        """
        analysis.optimize(usdStage, operation_configs)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):

        # get the large and disjoint sparse meshes
        large_sparse_meshes = analysis_data.get("largeSparseMeshes", {})
        disjoint_sparse_meshes = analysis_data.get("disjointSparseMeshes", {})

        # if any fixes were found, add a suggestion to the stage level
        if large_sparse_meshes or disjoint_sparse_meshes:
            self._AddWarning(
                message="Stage contains sparse meshes",
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Fix automatically using Scene Optimizer",
                    callable=partial(self._optimize_stage, operation_configs=self.suggested_operations),
                ),
            )

        # warn if there are any large single sparse meshes
        for prim_path, density in large_sparse_meshes.items():
            self._AddWarning(
                message=f"Large sparse mesh that can be diced detected with density of {density:.2f}%",
                at=usdStage.GetPrimAtPath(prim_path),
            )

        # warn if there are any disjoint sparse meshes
        for prim_path, density in disjoint_sparse_meshes.items():
            self._AddWarning(
                message=f"Disjoint sparse mesh that can be split and clustered detected with density of {density:.2f}%",
                at=usdStage.GetPrimAtPath(prim_path),
            )
