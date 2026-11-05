# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from functools import partial
from typing import ClassVar, List, Mapping

from pxr import Usd
from usd_optimize.core import analysis
from usd_validation_nvidia import Suggestion, capabilities, register_requirements

from .base_usd_optimize_checker import BaseUsdOptimizeChecker, Parameter, ParameterFromOpArg

# Usd Optimize Constants
MODE_HIDE = 3


@register_requirements(capabilities.GeometryRequirements.VG_003)
class OccludedMeshesChecker(BaseUsdOptimizeChecker):
    """
    Uses Usd Optimize to analyze a scene checking for occluded meshes.
    """

    OPERATION_NAME: str = "findOccludedMeshes"
    PARAMETERS: ClassVar[Mapping[str, Parameter]] = {
        "USE_GPU": ParameterFromOpArg("useGpu", default=True),
        "CHECK_TRANSPARENCY": ParameterFromOpArg("checkTransparency", default=True),
        "CLUSTERED": ParameterFromOpArg("clustered"),
        "MINIMUM_GAP_SIZE": ParameterFromOpArg("minimumGapSize"),
        "MAXIMUM_GRID_RESOLUTION": ParameterFromOpArg("maximumGridResolution"),
    }

    @classmethod
    def _remove_occluded_meshes(
        cls,
        usdStage: Usd.Stage,
        _: Usd.Prim,
        use_gpu: bool = True,
        check_transparency: bool = True,
        clustered: bool = True,
        minimum_gap_size: float = 0.01,
        maximum_grid_resolution: float = 500.0,
    ) -> None:
        """Remove occluded meshes using Usd Optimize.

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

        # Execute the optimization via Usd Optimize.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """Check a stage for occluded meshes"""

        occluded_mesh_paths = analysis_data["occludedMeshes"]
        occluded_mesh_count = len(occluded_mesh_paths)

        if occluded_mesh_count > 0:
            suffix = "" if occluded_mesh_count == 1 else "es"
            message: str = f"Found {occluded_mesh_count} occluded mesh{suffix}"

            args = self._effective_args()
            self._AddWarning(
                message=message,
                at=usdStage.GetPrimAtPath("/"),
                suggestion=Suggestion(
                    message="Remove occluded meshes using Usd Optimize",
                    callable=partial(
                        self._remove_occluded_meshes,
                        use_gpu=args["useGpu"],
                        check_transparency=args["checkTransparency"],
                        clustered=args["clustered"],
                        minimum_gap_size=args["minimumGapSize"],
                        maximum_grid_resolution=args["maximumGridResolution"],
                    ),
                ),
            )

            # In verbose mode, list each occluded mesh individually.
            self._AddVerbosePrimWarnings(usdStage, occluded_mesh_paths, "Occluded mesh found")
