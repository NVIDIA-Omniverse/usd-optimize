# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#
import omni.scene.optimizer.core
from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from pxr import Usd, UsdUtils


class OperationConfig:

    def __init__(self, name: str, args: dict = None):
        """
        Simple class used to represent an operation configuration for either analysis or optimization.

        :param name: The name of the operation to perform.
        :param args: The optional arguments to use when executing the operation.
        """
        self.__name = name
        self.__args = dict(args) if args is not None else {}

    @property
    def name(self):
        """
        The name of the operation to perform.
        """
        return self.__name

    @property
    def args(self):
        """
        The arguments to use when executing the operation.
        """
        return self.__args


# given a list of Operation Configs, resolves which operations are valid to execute.
# If supports_analysis is True, this will also check if the operation supports analysis.
# Operations that are not valid will be added to the result dict with an error message.
def __resolve_valid_operations(operations: list, supports_analysis: bool, result: dict):
    # get the names of valid operations
    operation_names = SceneOptimizerCore.getInstance().getOperations()

    # check which operations are valid and support analysis
    valid_operations = []
    for operation in operations:
        # check if the operation is valid
        if operation.name not in operation_names:
            result[operation.name] = (False, "Operation not found", None)
            continue
        # if we need to, check if the operation supports analysis
        if supports_analysis and not SceneOptimizerCore.getInstance().getOperationSupportsAnalysis(operation.name):
            result[operation.name] = (False, "Operation does not support analysis", None)
            continue
        # found a valid operation
        valid_operations.append(operation)

    return valid_operations


def analyze(stage: Usd.Stage, operations: list, context: ExecutionContext = None):
    """
    Runs the analysis for the given operations on the stage.

    :param stage: The USD stage to analyze.
    :param operations: A list of Config objects representing the operations to analyze.
    :param context: An optional ExecutionContext to use for the analysis.

    :return: A dictionary with the results of the analysis for each operation, keyed by each operation name.
    """
    result = {}
    valid_operations = __resolve_valid_operations(operations, True, result)

    # create a new Scene Optimizer ExecutionContext in analysis mode
    if context is None:
        context = ExecutionContext()
    context.usdStageId = UsdUtils.StageCache().Get().Insert(stage).ToLongInt()
    context.analysisMode = 1

    # run each operation
    for operation in valid_operations:
        operation_result = SceneOptimizerCore.getInstance().executeOperation(operation.name, context, operation.args)
        analysis_data = {}
        if operation_result and operation_result[0] and operation_result[2] and "analysis" in operation_result[2]:
            analysis_data = operation_result[2]["analysis"]
        result[operation.name] = (operation_result[0], operation_result[1], {"analysis": analysis_data})
    return result


def create_operations_from_analysis_result(analysis_result: dict):
    """
    Creates a list of OperationConfig objects from the suggested operations in the analysis result.
    """
    if analysis_result is None:
        return []
    ret = []
    for _, operation_result in analysis_result.items():
        # invalid result?
        if not operation_result or len(operation_result) < 3:
            continue
        # operation failed?
        if not operation_result[0]:
            continue
        # get analysis data
        analysis_data = operation_result[2].get("analysis")
        if analysis_data is None:
            continue
        # get suggested operations
        if not isinstance(analysis_data, dict):
            continue
        suggested_operations = analysis_data.get("suggestedOperations")
        if suggested_operations is None:
            continue
        for config in suggested_operations:
            op_name = config.get("name")
            if op_name is None:
                continue
            op_args = config.get("args", {})
            ret.append(OperationConfig(op_name, op_args))
    return ret


def optimize(stage: Usd.Stage, operations: list, context: ExecutionContext = None):
    """
    Runs Scene Optimizer for the given operations on the stage, and uses the analysis result to configure the operations
    if available.

    :param stage: The USD stage to optimize.
    :param operations: A list of Config objects representing the operations to perform.
    :param context: An optional ExecutionContext to use for the optimization.

    :return: A dictionary with the results of the optimization for each operation, keyed by each operation name.
    """
    result = {}
    valid_operations = __resolve_valid_operations(operations, False, result)

    # create a new Scene Optimizer ExecutionContext if needed
    if context is None:
        context = ExecutionContext()
    context.usdStageId = UsdUtils.StageCache().Get().Insert(stage).ToLongInt()
    context.generateReport = 1

    # run each operation
    for operation in valid_operations:
        try:
            operation_result = SceneOptimizerCore.getInstance().executeOperation(
                operation.name, context, operation.args
            )
        except Exception as e:
            operation_result = (False, f"Operation failed: {str(e)}", None)

        result[operation.name] = operation_result

    return result
