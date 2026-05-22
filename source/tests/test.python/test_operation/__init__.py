# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.operation import Operation
from pxr import Usd

EXECUTED = False


class TestOperation(Operation):

    def __init__(self):
        super().__init__("testOperation", "Test Operation", "A test operation for testing Python bindings.")

    @property
    def author(self):
        return "Scene Optimizer (Internal)"

    @property
    def version(self):
        return (1, 0, 0)

    def execute(self, args):
        global EXECUTED
        EXECUTED = True

        stage = self.get_usd_stage()
        stage.DefinePrim(f"/{args['primName']}", "Xform")

        return True


def sceneOptimizerPluginInit():
    return TestOperation()
