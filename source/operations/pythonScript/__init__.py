# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import base64

from omni.scene.optimizer.core.operation import Operation


class PythonScriptOperation(Operation):
    def __init__(self):
        super().__init__("pythonScript", "Python Script", "Executes a user defined Python script.")
        self.add_argument(
            "python",
            "Python Script",
            Operation.ArgumentDisplayTypeCode,
            "The Python script to execute.",
            'print("Hello world!")',
        )

    @property
    def author(self):
        return "Scene Optimizer (Internal)"

    @property
    def version(self):
        return (1, 0, 0)

    def execute(self, args):
        # decode the python code from the arguments
        base64_value = args["python"]
        base64_bytes = base64_value.encode("ascii")
        readable_bytes = base64.b64decode(base64_bytes)
        readable_value = readable_bytes.decode("ascii")
        # execute the python code
        _locals = {"stage": self.get_usd_stage()}
        env = dict(_locals, **globals())
        exec(readable_value, env, env)
        return True


#####################################
# Register Scene Optimizer Plugin
#####################################


def sceneOptimizerPluginInit():
    return PythonScriptOperation()
