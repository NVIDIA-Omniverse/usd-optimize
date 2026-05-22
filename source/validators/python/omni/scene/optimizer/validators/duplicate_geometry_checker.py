# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import omni.capabilities as cap
from omni.asset_validator import register_rule

from .base_duplicate_geometry_checker import DUPLICATE_METHOD_INSTANCEABLEREFERENCE, BaseDuplicateGeometryChecker


# @register_requirements(cap.GeometryRequirements.VG_022)
@register_rule("Usd:Performance")
class DuplicateGeometryChecker(BaseDuplicateGeometryChecker):
    """
    Find geometric prims that are duplicates.
    """

    # Default arguments for the command
    OPERATION_ARGS = {
        # Default Args
        "meshPrimPaths": [],
        "considerDeepTransforms": True,
        "tolerance": 0.05,
        "duplicateMethod": DUPLICATE_METHOD_INSTANCEABLEREFERENCE,
        "useGpu": False,
        # Fuzzy Args
        "fuzzy": False,
        "allowScaling": False,
    }
