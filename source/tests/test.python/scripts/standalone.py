# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Implementation of omni.scene.optimizer.core.scripts.standalone.

Provides the public API using SceneOptimizerCore directly.
"""

import json as _json

from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore

_CONTEXT_KEYS = {"debug", "singleThreaded", "verbose", "generateReport", "captureStats"}
_BOOL_STRINGS = {"true": True, "false": False, "1": True, "0": False}


def _coerce_context_value(value):
    """Convert a JSON value to the type expected by ExecutionContext.

    Booleans and boolean-like strings ("true"/"false", "1"/"0") become ``bool``;
    other values are left as-is (``json.load`` already returns ``int`` / ``float``).
    """
    if isinstance(value, bool):
        return value
    if isinstance(value, str):
        mapped = _BOOL_STRINGS.get(value.lower())
        if mapped is not None:
            return mapped
    return value


def execute_commands_from_json(stage, filepath_or_json):
    """Execute a series of commands described in a JSON file or JSON string on the given Usd.Stage."""
    so_core = SceneOptimizerCore.getInstance()

    context = ExecutionContext()
    context.set_stage(stage)

    try:
        with open(filepath_or_json) as f:
            operations = _json.load(f)
    except (FileNotFoundError, OSError):
        try:
            operations = _json.loads(filepath_or_json)
        except ValueError:
            return False
    except ValueError:
        return False

    if not isinstance(operations, list):
        return False

    result = True
    for op_dict in operations:
        op_dict = dict(op_dict)
        if "operation" not in op_dict:
            result = False
            continue
        op_name = op_dict.pop("operation")

        if op_name == "executionContext":
            for key, value in op_dict.items():
                if key in _CONTEXT_KEYS:
                    setattr(context, key, _coerce_context_value(value))
            continue

        success, _error, _warning = so_core.executeOperation(op_name, context, op_dict)
        if not success:
            result = False

    # Do NOT call context.remove_stage() here; removing the stage from the
    # UsdUtils.StageCache can invalidate the caller's stage reference when
    # the stage was opened via an anonymous layer.
    return result


def get_output_paths(operation):
    """Return any output paths the operation may have set.

    Returns an empty list (not supported in standalone mode).
    """
    return []


def get_output_path_arrays(operation):
    """Return any output path arrays the operation may have set.

    Returns an empty list (not supported in standalone mode).
    """
    return []


def map_config(config):
    """Map a Scene Optimizer JSON configuration to update renamed operations/arguments."""
    so_core = SceneOptimizerCore.getInstance()
    return so_core.mapConfig(config)
