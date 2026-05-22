# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from functools import partial

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from omni.scene.optimizer.core import analysis
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.GeometryRequirements.VG_012)
@register_rule("Usd:Performance")
class SmallMeshChecker(BaseSceneOptimizerChecker):
    """
    Uses Scene Optimizer to analyze a scene checking for meshes with extents
    below a configurable size threshold.

    Parameters:
        SIZE_THRESHOLD: The minimum extent size a mesh can have before it is
            considered small. Default: 0.001.
    """

    OPERATION_NAME: str = "removeSmallGeometry"
    DEFAULT_SIZE_THRESHOLD: float = 0.001

    def _get_param(self, name, default):
        """Get a parameter value, returning default if it doesn't exist in the requirements spec."""
        try:
            return self.parameters[name].assigned_value
        except (KeyError, AttributeError):
            return default

    def _get_threshold(self):
        """Get the size threshold, from user parameter or DEFAULT_SIZE_THRESHOLD fallback."""
        return self._get_param("SIZE_THRESHOLD", self.DEFAULT_SIZE_THRESHOLD)

    def _GetArgs(self):
        """Get arguments for the Scene Optimizer operation.

        Overrides base class to include the size threshold parameter.
        """
        return {
            "removeMethod": 1,
            "detectionMethod": 0,
            "threshold": self._get_threshold(),
        }

    @classmethod
    def _optimize_stage(cls, usdStage: Usd.Stage, _: Usd.Prim, operation_configs: list) -> None:
        """Remove small meshes using the analysis-derived operation configs.

        Args:
            usdStage: The USD stage to operate on
            _: Unused prim argument (required by Suggestion callable signature)
            operation_configs: Operation configs from analysis result
        """
        analysis.optimize(usdStage, operation_configs)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """Check a stage for small meshes below the size threshold."""
        small_mesh_paths = analysis_data.get("smallGeometry", [])

        if small_mesh_paths:
            threshold = self._get_threshold()
            suffix = "" if len(small_mesh_paths) == 1 else "es"
            self._AddWarning(
                message=f"Stage contains {len(small_mesh_paths)} mesh{suffix} below size threshold {threshold}",
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Remove small meshes using Scene Optimizer",
                    callable=partial(self._optimize_stage, operation_configs=self.suggested_operations),
                ),
            )

        for prim_path in small_mesh_paths:
            self._AddWarning(
                message="Small mesh found",
                at=usdStage.GetPrimAtPath(prim_path),
            )
