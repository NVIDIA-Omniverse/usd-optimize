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
MODE_HIDE = 3


# @register_requirements(cap.GeometryRequirements.VG_003)
@register_rule("Usd:Performance")
class OccludedMeshesChecker(BaseSceneOptimizerChecker):
    """
    Uses Scene Optimizer to analyze a scene checking for occluded meshes.

    Parameters:
        USE_GPU: Enable GPU-accelerated occlusion detection. Default: False.
        CHECK_TRANSPARENCY: Consider material transparency when detecting occlusion. Default: True.
        CLUSTERED: Split the stage into clusters of meshes with overlapping bounding boxes and check visibility per cluster, improving both accuracy and performance by reducing the number of meshes compared at the same time. Default: True.
        MINIMUM_GAP_SIZE: The minimum gap size for the background grid spacing. Gaps smaller than this are considered closed. Default: 0.01.
        MAXIMUM_GRID_RESOLUTION: The maximum number of cells along the longest axis of the visibility grid. Default: 500.0.
    """

    OPERATION_NAME: str = "findOccludedMeshes"

    def _get_param(self, name, default):
        """Get a parameter value, returning default if it doesn't exist in the requirements spec."""
        try:
            return self.parameters[name].assigned_value
        except (KeyError, AttributeError):
            return default

    def _GetArgs(self):
        """Get arguments for the Scene Optimizer operation.

        Overrides base class to include parameters from requirement spec.
        """
        return {
            "useGpu": self._get_param("USE_GPU", False),
            "checkTransparency": self._get_param("CHECK_TRANSPARENCY", True),
            "clustered": self._get_param("CLUSTERED", True),
            "minimumGapSize": self._get_param("MINIMUM_GAP_SIZE", 0.01),
            "maximumGridResolution": self._get_param("MAXIMUM_GRID_RESOLUTION", 500.0),
        }

    @classmethod
    def _remove_occluded_meshes(
        cls,
        usdStage: Usd.Stage,
        _: Usd.Prim,
        use_gpu: bool = False,
        check_transparency: bool = True,
        clustered: bool = True,
        minimum_gap_size: float = 0.01,
        maximum_grid_resolution: float = 500.0,
    ) -> None:
        """Remove occluded meshes using Scene Optimizer.

        Args:
            usdStage: The USD stage to operate on
            _: Unused prim argument (required by Suggestion callable signature)
            use_gpu: Whether to use GPU acceleration
            check_transparency: Whether to consider material transparency
            clustered: Whether to cluster meshes before visibility checking
            minimum_gap_size: The minimum gap size for the background grid
            maximum_grid_resolution: The maximum grid resolution for visibility checking
        """
        # Configure operation
        # Need to pass all meshes involved in occluded mesh detection,
        # not just occluded mesh, so easiest just to work on full scene
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME,
                args={
                    "action": MODE_HIDE,
                    "useGpu": use_gpu,
                    "checkTransparency": check_transparency,
                    "clustered": clustered,
                    "minimumGapSize": minimum_gap_size,
                    "maximumGridResolution": maximum_grid_resolution,
                },
            ),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """Check a stage for occluded meshes"""

        occluded_mesh_paths = analysis_data["occludedMeshes"]
        occluded_mesh_count = len(occluded_mesh_paths)

        if occluded_mesh_count > 0:
            suffix = "" if occluded_mesh_count == 1 else "es"
            message: str = f"Found {occluded_mesh_count} occluded mesh{suffix}"

            # Get parameter values from requirement spec
            use_gpu = self._get_param("USE_GPU", False)
            check_transparency = self._get_param("CHECK_TRANSPARENCY", True)
            clustered = self._get_param("CLUSTERED", True)
            minimum_gap_size = self._get_param("MINIMUM_GAP_SIZE", 0.01)
            maximum_grid_resolution = self._get_param("MAXIMUM_GRID_RESOLUTION", 500.0)

            self._AddWarning(
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Remove occluded meshes using Scene Optimizer",
                    callable=partial(
                        self._remove_occluded_meshes,
                        use_gpu=use_gpu,
                        check_transparency=check_transparency,
                        clustered=clustered,
                        minimum_gap_size=minimum_gap_size,
                        maximum_grid_resolution=maximum_grid_resolution,
                    ),
                ),
            )
