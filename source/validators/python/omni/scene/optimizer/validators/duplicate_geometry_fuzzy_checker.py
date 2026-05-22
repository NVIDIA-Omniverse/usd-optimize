# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

# import omni.capabilities as cap
from omni.asset_validator import register_rule

from .base_duplicate_geometry_checker import DUPLICATE_METHOD_INSTANCEABLEREFERENCE, BaseDuplicateGeometryChecker


@register_rule("Usd:Performance")
class FuzzyDuplicateGeometryChecker(BaseDuplicateGeometryChecker):
    """
    Find geometric prims that are duplicates within a tolerance.
    """

    # Allow overriding paths
    PATHS = []

    # Default arguments for the command
    OPERATION_ARGS = {
        # Default Args
        "meshPrimPaths": [],
        "considerDeepTransforms": False,
        "tolerance": 0.001,
        "duplicateMethod": DUPLICATE_METHOD_INSTANCEABLEREFERENCE,
        "useGpu": False,
        # Fuzzy Args
        "fuzzy": True,
        "allowScaling": True,
        "fuzzyOnly": True,
    }

    def _GetArgs(self):
        """Custom GetArgs function

        Allows configuring when testing the operation
        """

        args = FuzzyDuplicateGeometryChecker.OPERATION_ARGS.copy()
        if FuzzyDuplicateGeometryChecker.PATHS:
            args["meshPrimPaths"] = FuzzyDuplicateGeometryChecker.PATHS

        return args
