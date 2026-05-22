# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from .test_utils import Test_Operation, _get_context

# Constants
MODE_REMOVE = 0
MODE_BLOCK = 1

# Default arguments for the command
DEFAULT_ARGS = {
    "primPaths": [],
    "attributes": [],
    "mode": MODE_REMOVE,
}


class Test_Operation_Remove_Attributes(Test_Operation):
    """Test cases for the Remove Attributes operation"""

    OPERATION = "removeAttributes"

    async def setUp(self):
        """Common setup"""

        await super().setUp()

        self.stage = self._open_stage("removeAttributes.usda")
        self.context = _get_context(self.stage)
        self.args = DEFAULT_ARGS.copy()

    async def test_remove_no_attributes(self):
        """Test operation with no attributes specified"""

        success, result = self._execute_command(self.args, context=self.context)

        # Command succeeds
        self.assertTrue(success)

        # Operation should not succeed
        self.assertFalse(result[0])

    async def test_remove_invalid_attributes(self):
        """Test operation with invalid attributes"""

        self.args["attributes"] = ["1234abcd", "#*!"]
        success, result = self._execute_command(self.args, context=self.context)

        # Command succeeds
        self.assertTrue(success)

        # Operation should not succeed
        self.assertFalse(result[0])

    async def test_remove_no_attributes_found(self):
        """Test operation with valid attributes but no matches"""

        self.args["attributes"] = ["myAwesomeAttribute"]
        success, result = self._execute_command(self.args, context=self.context)

        # Command succeeds
        self.assertTrue(success)

        # Operation succeeds
        self.assertTrue(result[0])

    async def test_removing_attribute(self):
        """Test basic attribute removal"""

        prim = self.stage.GetPrimAtPath("/World/Cube")

        attr = prim.GetAttribute("primvars:st")
        self.assertEqual(len(attr.Get()), 24)

        # Execute removing a specific attribute
        self.args["attributes"] = ["primvars:st"]

        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # Attribute no longer authored
        attr = prim.GetAttribute("primvars:st")
        self.assertFalse(attr)

    async def test_blocking_attribute(self):
        """Test blocking an attribute"""

        prim = self.stage.GetPrimAtPath("/World/Cube")

        attr = prim.GetAttribute("primvars:st")
        self.assertEqual(len(attr.Get()), 24)

        # Execute removing a specific attribute
        self.args["attributes"] = ["primvars:st"]
        self.args["mode"] = MODE_BLOCK

        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # Attribute still authored, but has no value
        attr = prim.GetAttribute("primvars:st")
        self.assertTrue(attr)
        self.assertEqual(attr.Get(), None)

    async def test_removing_namespace(self):
        """Test removing all attributes from a namespace"""

        prim = self.stage.GetPrimAtPath("/World/Cube")

        def get_attr(authored, name):
            for attr in authored:
                if attr.GetName() == name:
                    return attr

            return None

        authored = prim.GetAuthoredAttributes()
        self.assertTrue(get_attr(authored, "ui:displayGroup"))
        self.assertTrue(get_attr(authored, "ui:order"))

        # Remove everything from the ui: namespace
        self.args["attributes"] = ["ui:"]
        success, result = self._execute_command(self.args, context=self.context)
        self.assertTrue(success)
        self.assertTrue(result[0])

        # Neither should be authored now
        authored = prim.GetAuthoredAttributes()
        self.assertFalse(get_attr(authored, "ui:displayGroup"))
        self.assertFalse(get_attr(authored, "ui:order"))

    async def test_removing_over(self):
        """Test removal behaviour on a reference"""

        prim = self.stage.GetPrimAtPath("/World/ReferencedCube/Cube")

        value = prim.GetAttribute("foo").Get()
        self.assertEqual(value, 50)

        self.args["attributes"] = ["foo"]

        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # When removing from a reference, the over is removed from the
        # current edit target so now the original value is returned.
        value = prim.GetAttribute("foo").Get()
        self.assertEqual(value, 20)

    async def test_blocking_over(self):
        """Test blocking behaviour on a reference"""

        prim = self.stage.GetPrimAtPath("/World/ReferencedCube/Cube")

        value = prim.GetAttribute("foo").Get()
        self.assertEqual(value, 50)

        self.args["attributes"] = ["foo"]
        self.args["mode"] = MODE_BLOCK

        success, result = self._execute_command(self.args, context=self.context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        # When blocking from a reference, an SdfBlock is authored meaning
        # the attribute is authored but has no value, rather than the
        # underlying value bubbling up.
        value = prim.GetAttribute("foo").Get()
        self.assertEqual(value, None)
