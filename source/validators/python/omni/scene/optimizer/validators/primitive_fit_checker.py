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


# @register_requirements(cap.GeometryRequirements.VG_017)
@register_rule("Usd:Performance")
class PrimitiveFitChecker(BaseSceneOptimizerChecker):
    """
    Check mesh prims that could be replaced with a USD primitive prim, with an option to apply the fix from scene optimizer operation.
    """

    OPERATION_NAME: str = "fitPrimitives"

    @classmethod
    def _fit_primitives(cls, prim_name, ignore_nonconst_primvars, usdStage: Usd.Stage, prim: Usd.Prim) -> None:
        """
        Replace meshes with fit primitives using Scene Optimizer
        """

        # Configure optimize primvars with this specific prim path and primvar name.
        # TODO: It would be nice to be able to bulk apply these. If not, we can at
        #       least add a mode to SO to target a specific attribute path
        operations: List[analysis.OperationConfig] = [
            analysis.OperationConfig(
                cls.OPERATION_NAME,
                args={
                    "fitSphere": (prim_name == "sphere"),
                    "fitCylinder": (prim_name == "cylinder"),
                    "fitCone": (prim_name == "cone"),
                    "fitCube": (prim_name == "cube"),
                    "ignoreNonConstPrimvars": ignore_nonconst_primvars,
                },
            ),
        ]

        # Execute the optimization via SO.
        analysis.optimize(usdStage, operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        """
        Process the Scene Optimizer analysis of primimitve fit stats and issues.
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
                        message="Use the scene optimizer operation Fit Primitives with fit {} enabled.".format(name),
                        callable=partial(self._fit_primitives, name, False),
                    ),
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
                self._AddWarning(
                    message=text,
                    at=usdStage.GetPrimAtPath("/"),
                    suggestion=Suggestion(
                        message="If losing surface-varying features is acceptable, use the scene optimizer operation "
                        'Fit Primitives with both fit {} and "Ignore non-const primvars" enabled.'.format(name),
                        callable=partial(self._fit_primitives, name, True),
                    ),
                )
