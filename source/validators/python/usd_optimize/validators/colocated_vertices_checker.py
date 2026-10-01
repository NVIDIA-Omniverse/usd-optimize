# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from functools import partial
from typing import ClassVar, List, Mapping

from pxr import Usd
from usd_optimize.core import analysis
from usd_validation_nvidia import Suggestion, capabilities, register_requirements

from .base_usd_optimize_checker import BaseUsdOptimizeChecker, Parameter, ParameterFromOpArg


@register_requirements(capabilities.GeometryRequirements.VG_016, override=True)
class ColocatedVerticesChecker(BaseUsdOptimizeChecker):
    """
    Check mesh prims for colocated vertices, returns all prims with colocated vertices as a single warning with an option to fix via a Usd Optimize operation.
    """

    OPERATION_NAME: str = "meshCleanup"

    # Analysis must request only the coincident-vertex fixes: the gated checkClean reports a defect
    # only if its fix is enabled, so both merge modes are needed to see either defect. Mirrors
    # _mesh_merge_vertices. Leaving this unset falls through to the operation's C++ ctor defaults,
    # which additionally enable DegenerateEdges -- and pairing that with CoincidentNeighborVertices
    # corrupts the heap inside omo::checkClean.
    OPERATION_ARGS = {
        "mergeVertices": True,
        "tolerance": 0.0,
        "contractDegenerateEdges": False,
        "removeDegenerateFaces": False,
        "makeManifold": False,
        "removeIsolatedVertices": False,
        "mergeBoundaries": True,
        "mergeNeighbors": True,
        "removeDuplicateFaces": False,
    }
    PARAMETERS: ClassVar[Mapping[str, Parameter]] = {"TOLERANCE": ParameterFromOpArg("tolerance", default=0.0)}

    def _mesh_merge_vertices(self, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Cleanup meshes by merging vertices using Usd Optimize
        """

        # Configure mesh cleanup, honoring the tuned TOLERANCE parameter so the
        # fix merges vertices with the same tolerance used during analysis.
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                self.OPERATION_NAME,
                args={
                    "mergeVertices": True,
                    "tolerance": self._effective_args()["tolerance"],
                    "mergeBoundaries": True,
                    "mergeNeighbors": True,
                    "contractDegenerateEdges": False,
                    # Welding collapses faces to zero area; removing them is what keeps the output
                    # manifold. contractDegenerateEdges stays off -- it corrupts omo::checkClean.
                    "removeDegenerateFaces": True,
                    "makeManifold": False,
                    "removeIsolatedVertices": False,
                    "removeDuplicateFaces": False,
                },
            ),
        ]

        # Execute the optimization via Usd Optimize.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Usd Optimize analysis of mesh cleanups
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
                    message="Fix meshes with coincident vertices using Usd Optimize",
                    callable=partial(self._mesh_merge_vertices),
                ),
            )

            # In verbose mode, list each mesh with mergeable vertices individually.
            self._AddVerbosePrimWarnings(
                usdStage,
                analysis_data.get("meshesWithMergeableVerticesPaths", []),
                "Mesh with mergeable vertices found",
            )
