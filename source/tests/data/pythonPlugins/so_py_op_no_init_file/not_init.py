# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.operation import Operation


class NoInitFilePythonOperation(Operation):
    def __init__(self):
        super().__init__(
            "noInitFilePythonOperation",
            "No Init File Python Operation",
            "This is a Python Operation that doesn't use a __init__.py file for testing purposes.",
        )

    @property
    def author(self):
        return "Scene Optimizer Unit Test"

    @property
    def version(self):
        return (1, 2, 3)

    def execute(self, args):
        return True


#####################################
# Register Scene Optimizer Plugin
#####################################


def sceneOptimizerPluginInit():
    return NoInitFilePythonOperation()
