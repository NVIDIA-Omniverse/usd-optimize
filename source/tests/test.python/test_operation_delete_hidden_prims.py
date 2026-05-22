# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import UsdGeom

from .test_utils import Test_Operation


def _count_hidden(stage):
    """Count hidden prims in a stage"""
    count = 0
    for prim in stage.Traverse():
        imageable = UsdGeom.Imageable(prim)
        if imageable:
            vis_attr = imageable.GetVisibilityAttr()
            if vis_attr.Get() == UsdGeom.Tokens.invisible:
                count += 1

    return count


class Test_Operation_Delete_Hidden_Prims(Test_Operation):

    OPERATION = "deleteHiddenPrims"

    async def test_delete_hidden_prims(self):
        """Test basic deletion of hidden prims"""

        stage = self._open_stage("deleteHiddenPrims.usda")

        count = _count_hidden(stage)
        self.assertEqual(count, 3)

        self._execute_command({})

        count = _count_hidden(stage)
        self.assertEqual(count, 0)
