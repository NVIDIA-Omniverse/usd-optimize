# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.operation import Operation


class ArgsErrorPythonOperation(Operation):
    def __init__(self):
        super().__init__(
            "argsErrorPythonOperation",
            "Args Error Python Operation",
            "This is a Python Operation that raises an error in the _serialize_arguments function for testing purposes.",
        )

    @property
    def author(self):
        return "Scene Optimizer Unit Test"

    @property
    def version(self):
        return (1, 2, 3)

    def execute(self, args):
        return True

    def _serialize_arguments(self):
        raise RuntimeError("args error")


#####################################
# Register Scene Optimizer Plugin
#####################################


def sceneOptimizerPluginInit():
    return ArgsErrorPythonOperation()
