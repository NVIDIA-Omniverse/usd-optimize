# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.operation import Operation
from pxr import Usd


class RemoveUntypedPrims(Operation):
    def __init__(self):
        super().__init__(
            "removeUntypedPrims", "Remove Untyped Prims", "Removes untyped prims that are not under /Render."
        )

    @property
    def author(self):
        return "Scene Optimizer (Internal)"

    @property
    def version(self):
        return (1, 0, 0)

    @property
    def visible(self):
        return False

    def execute(self, _args):
        stage = self.get_usd_stage()

        # Does not include instance proxies
        remove = []
        for prim in Usd.PrimRange(stage.GetPseudoRoot()):
            prefixes = prim.GetPath().GetPrefixes()

            if prefixes:
                if prefixes[0] == "/Render":
                    continue

            if not prim.IsA(Usd.SchemaBase):
                remove.append(prim)

        for prim_to_remove in remove:
            stage.RemovePrim(prim_to_remove.GetPath())

        return True


#####################################
# Register Scene Optimizer Plugin
#####################################


def sceneOptimizerPluginInit():
    return RemoveUntypedPrims()
