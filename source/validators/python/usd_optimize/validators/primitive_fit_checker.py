# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from functools import partial
from typing import ClassVar, List, Mapping

from pxr import Usd
from usd_optimize.core import analysis
from usd_validation_nvidia import Suggestion, capabilities, register_requirements

from .base_usd_optimize_checker import BaseUsdOptimizeChecker, Parameter, ParameterFromOpArg


@register_requirements(capabilities.GeometryRequirements.VG_017)
class PrimitiveFitChecker(BaseUsdOptimizeChecker):
    """
    Check mesh prims that could be replaced with a USD primitive prim, with an option to apply the fix from a Usd Optimize operation.
    """

    OPERATION_NAME: str = "fitPrimitives"
    PARAMETERS: ClassVar[Mapping[str, Parameter]] = {
        "GPU_FACE_COUNT_THRESHOLD": ParameterFromOpArg("gpuFaceCountThreshold"),
        "VERTEX_TOLERANCE": ParameterFromOpArg("vertexTolerance"),
        "VOLUME_TOLERANCE": ParameterFromOpArg("volumeTolerance"),
        "IGNORE_SUBSETS": ParameterFromOpArg("ignoreSubsets"),
        "ALLOW_NEGATIVE_VOLUME": ParameterFromOpArg("allowNegativeVolume"),
        "ALLOW_MISSING_ENDCAPS": ParameterFromOpArg("allowMissingEndcaps"),
    }

    def _fit_primitives(self, prim_name, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Replace meshes with fit primitives using Usd Optimize
        """

        # Start from the effective args so the tuned tolerance/threshold parameters
        # (vertexTolerance, volumeTolerance, gpuFaceCountThreshold, ignoreSubsets,
        # allowNegativeVolume, allowMissingEndcaps) drive the fix the same way they
        # drive analysis, then set the per-suggestion fit target and primvar handling.
        # Configure optimize primvars with this specific prim path and primvar name.
        # TODO: It would be nice to be able to bulk apply these. If not, we can at
        #       least add a mode to Usd Optimize to target a specific attribute path
        args = dict(self._effective_args())
        args.update(
            {
                "fitSphere": (prim_name == "sphere"),
                "fitCylinder": (prim_name == "cylinder"),
                "fitCone": (prim_name == "cone"),
                "fitCube": (prim_name == "cube"),
                "ignoreNonConstPrimvars": False,  # a trade-off; never taken unattended
            }
        )
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(self.OPERATION_NAME, args=args),
        ]

        # Execute the optimization via Usd Optimize.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Usd Optimize analysis of primimitve fit stats and issues.
        """

        # Give stage stats for context
        non_composed_mesh_count = analysis_data["totalMeshCount"] - analysis_data["composedCount"]
        if non_composed_mesh_count > 0:
            totalFaceCount = analysis_data["totalFaceCount"]
            totalVertexCount = analysis_data["totalVertexCount"]
            self._AddInfo(
                message="Stage contains {} non-composed meshes with a total of {} faces and {} vertices.".format(
                    non_composed_mesh_count, totalFaceCount, totalVertexCount
                )
            )

        # List possible primitive replacements
        primitives = analysis_data["primitives"]
        for name, data in primitives.items():
            mesh_count = data["meshCount"]
            face_count = data["faceCount"]
            vertex_count = data["vertexCount"]
            nonconst_primvar_mesh_count = data["nonconstPrimvarMeshCount"]
            nonconst_primvar_face_count = data["nonconstPrimvarFaceCount"]
            nonconst_primvar_vertex_count = data["nonconstPrimvarVertexCount"]
            if mesh_count > 0:
                s = "es" if mesh_count > 1 else ""
                text = "Found {} mesh{} w/o non-const primvars that can be replaced by a ".format(mesh_count, s)
                text += "{} GPrim, eliminating {} faces and {} vertices.".format(name, face_count, vertex_count)
                self._AddWarning(
                    message=text,
                    at=usdStage.GetPrimAtPath("/"),
                    suggestion=Suggestion(
                        message="Use the Usd Optimize operation Fit Primitives with fit {} enabled.".format(name),
                        callable=partial(self._fit_primitives, name),
                    ),
                )

                # In verbose mode, list each fittable mesh individually.
                self._AddVerbosePrimWarnings(
                    usdStage,
                    data.get("meshPaths", []),
                    "Mesh can be replaced by a {} GPrim".format(name),
                )
            if nonconst_primvar_mesh_count != 0:
                s = "es" if nonconst_primvar_mesh_count > 1 else ""
                w = "with" if mesh_count == 0 else "WITH"
                text = "Found {} mesh{} {} non-const primvars that can be replaced by a ".format(
                    nonconst_primvar_mesh_count, s, w
                )
                text += "{} GPrim, losing texture mapping ".format(name)
                text += "but eliminating {} faces and {} vertices.".format(
                    nonconst_primvar_face_count, nonconst_primvar_vertex_count
                )
                # No suggestion: IssueFixer applies them unattended, so attaching one here
                # would take the primvar loss on the user's behalf.
                self._AddWarning(
                    message=text + " Run fitPrimitives with "
                    '"Ignore non-const primvars" if losing them is acceptable.',
                    at=usdStage.GetPrimAtPath("/"),
                )

                # In verbose mode, list each fittable mesh (with non-const primvars) individually.
                self._AddVerbosePrimWarnings(
                    usdStage,
                    data.get("nonconstPrimvarMeshPaths", []),
                    "Mesh with non-const primvars can be replaced by a {} GPrim".format(name),
                )
