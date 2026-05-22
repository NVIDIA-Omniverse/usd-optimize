# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from typing import Dict, Tuple

from omni.asset_validator import BaseRuleChecker
from omni.scene.optimizer.core import SceneOptimizerCore, analysis
from pxr import Usd


class BaseSceneOptimizerChecker(BaseRuleChecker):
    """Base checker for Scene Optimizer analysis

    Handles executing a Scene Optimizer operation with analysis mode enabled
    and validating the result.

    The analysis payload can then be passed to a derived Checker to process
    issues specific to that operation.
    """

    OPERATION_NAME: str = None
    OPERATION_ARGS: Dict = {}

    def _GetArgs(self):
        """Get arguments to use in execution"""
        return self.OPERATION_ARGS

    def _AnalyzeStage(self, usdStage: Usd.Stage, operation_name: str, args: Dict = None) -> Tuple:
        """
        Runs Scene Optimizer analysis on the given USD stage with the specified operation, reports a failure if found,
        and the returns the analysis result.
        """
        # Implementation error
        if not operation_name:
            self._AddFailedCheck(message="Invalid rule, no operation configured")
            return None

        # For debug purposes, determine the arguments that will be used to execute
        # the operation. Start with the custom args being added to this operation,
        # then add any default operation args that aren't present. This gives us
        # the total config that will be used.
        configured_args = (args or {}).copy()

        arguments = SceneOptimizerCore.getInstance().getOperationArguments(operation_name)

        for arg in arguments:
            if "group" not in arg and arg["name"] not in configured_args:
                configured_args[arg["name"]] = arg["defaultValue"]

        print(f"AV/SO Config for {operation_name}: {configured_args}")

        analysis_result: Dict = analysis.analyze(usdStage, [analysis.OperationConfig(operation_name, args=args)])
        if not analysis_result:
            self._AddFailedCheck(message="Failed to run Scene Optimizer analysis with unknown error.")
            return None

        # Extract sets of duplicates from the result
        operation_result: tuple = analysis_result.get(operation_name)
        if not operation_result:
            self._AddFailedCheck(message="Failed to run Scene Optimizer analysis with unknown error.")
            return None

        # result should be a 3-tuple
        if len(operation_result) != 3:
            self._AddFailedCheck(message="Scene Optimizer analysis returned invalid result.")
            return None

        # did analysis run successfully?
        if operation_result[0] is False:
            self._AddFailedCheck(message=f"Analysis encountered error: {operation_result[1]}")
            return None

        # resolve the suggested operations from the analysis result
        suggested_operations = analysis.create_operations_from_analysis_result(analysis_result)

        return (operation_result[2].get("analysis"), suggested_operations)

    def _CheckStage(self, usdStage: Usd.Stage, analysis: dict):
        """Derived checkers should implement this function.

        Subclasses that need the suggested operations from analysis can access
        them via ``self.suggested_operations``.
        """
        pass

    def CheckStage(self, usdStage: Usd.Stage):
        """Base setup/execution of analysis mode for a scene optimizer operation"""
        analysis_result = None
        try:
            result = self._AnalyzeStage(usdStage, self.OPERATION_NAME, args=self._GetArgs())
            if result is not None:
                analysis_result, self.suggested_operations = result
        except Exception as ex:
            print("Failed to analyze stage:", ex)

        if analysis_result:
            # Defer to the derived class to process the result
            self._CheckStage(usdStage, analysis_result)
