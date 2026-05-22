# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

# import omni.capabilities as cap
from omni.asset_validator import Suggestion, register_rule
from pxr import Usd

from .base_scene_optimizer_checker import BaseSceneOptimizerChecker


# @register_requirements(cap.HierarchyRequirements.HI_011)
@register_rule("Usd:Performance")
class FlatHierarchiesChecker(BaseSceneOptimizerChecker):
    """
    Finds prims that have 500 or more children.
    """

    MAX_CHILDREN: int = 500
    CONSIDER_ALL_CHILDREN: bool = True

    def _GetArgs(self):
        """Custom GetArgs function

        Allows configuring the thresholds when testing the operation
        """
        return {"maxChildren": self.MAX_CHILDREN, "considerAllChildren": self.CONSIDER_ALL_CHILDREN}

    OPERATION_NAME: str = "findFlatHierarchies"

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        # verify that the analysis data contains the flat hierarchies
        flat_hierarchies = analysis_data.get("flatHierarchies")
        if flat_hierarchies is None:
            self._AddFailedCheck(message="Analysis data does not contain flat hierarchies")
            return

        # create any issues
        for prim_path, num_children in flat_hierarchies.items():
            self._AddWarning(
                message=f"Found {num_children} children under prim '{prim_path}'",
                at=usdStage.GetPrimAtPath(prim_path),
            )
