# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import unittest

from omni.scene.optimizer.core import analysis
from pxr import Sdf, Usd

from .test_utils import _get_test_data_file_path


class TestCoreAnalysis(unittest.TestCase):

    def __open_stage(self, name):
        file_path = _get_test_data_file_path(name)
        layer = Sdf.Layer.FindOrOpen(file_path)
        if layer:
            layer.Reload()
        self.assertIsNotNone(layer, f"Failed to open layer: {file_path}")
        stage = Usd.Stage.Open(layer)
        self.assertIsNotNone(stage)
        return stage

    def test_analyze(self):

        # build a list of operations to run analysis on
        operations = [
            analysis.OperationConfig("sparseMeshes"),  # sparse meshes is a valid analysis operation
            analysis.OperationConfig(
                "pythonScript"
            ),  # python script does not support analysis, but is a valid operation
            analysis.OperationConfig("doesNotExist"),  # invalid operation
        ]

        # open the stage
        stage = self.__open_stage("factory.usda")

        # call analysis
        analysis_result = analysis.analyze(stage, operations)

        # verify the sparse meshes result
        self.assertTrue("sparseMeshes" in analysis_result)
        sparse_meshes_result = analysis_result["sparseMeshes"]
        self.assertTrue(len(sparse_meshes_result) == 3)
        self.assertTrue(sparse_meshes_result[0])
        sparse_meshes_data = sparse_meshes_result[2]
        self.assertTrue("analysis" in sparse_meshes_data)  # should have an analysis section
        sparse_meshes_analysis = sparse_meshes_data["analysis"]
        # check there is a disjointSparseMeshes section
        self.assertTrue("disjointSparseMeshes" in sparse_meshes_analysis)
        # check there is a suggestedOperations section
        self.assertTrue("suggestedOperations" in sparse_meshes_analysis)

        # verify the python script result
        self.assertTrue("pythonScript" in analysis_result)
        python_script_result = analysis_result["pythonScript"]
        self.assertTrue(len(python_script_result) == 3)
        self.assertFalse(python_script_result[0])
        # should have an error since python script does not support analysis
        self.assertTrue(python_script_result[1])

        # verify the doesNotExist result
        self.assertTrue("doesNotExist" in analysis_result)
        does_not_exist_result = analysis_result["doesNotExist"]
        self.assertTrue(len(does_not_exist_result) == 3)
        self.assertFalse(does_not_exist_result[0])
        # should have an error since the operation does not exist
        self.assertTrue(does_not_exist_result[1])

    def test_optimize(self):
        # build a list of operations to run scene optimizer on
        operations = [
            analysis.OperationConfig("sparseMeshes"),
            analysis.OperationConfig("doesNotExist"),
        ]

        # open the stage
        stage = self.__open_stage("factory.usda")

        # call analysis
        analysis_result = analysis.analyze(stage, operations)

        # create operations from the analysis result
        operations = analysis.create_operations_from_analysis_result(analysis_result)

        # verify one split meshes operation was created
        self.assertTrue(len(operations) == 1)
        self.assertTrue(operations[0].name == "splitMeshes")

        # call optimize
        optimize_result = analysis.optimize(stage, operations)

        # verify the splitMeshes operation has completed successfully
        self.assertTrue("splitMeshes" in optimize_result)
        split_meshes_result = optimize_result["splitMeshes"]
        self.assertTrue(len(split_meshes_result) == 3)
        self.assertTrue(split_meshes_result[0])
