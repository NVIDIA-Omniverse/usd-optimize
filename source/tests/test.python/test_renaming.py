# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import json

from .scripts import standalone
from .test_utils import Test_Operation


class TestRenaming(Test_Operation):
    """Operation/Argument renaming tests"""

    async def test_map_config(self):
        """Test mapping a JSON config"""

        # Create a config which uses an old operation name and old
        # argument name.
        old_config = [{"operation": "findCoincidingMeshes", "meshPrimPaths": ["foo"]}]

        # Use the exposed SO mapping function to update the config
        mapped = standalone.map_config(json.dumps(old_config))
        mapped_config = json.loads(mapped)

        # Should be different, but still only one entry
        self.assertNotEqual(old_config, mapped_config)
        self.assertEqual(len(mapped_config), 1)

        mapped_operation = mapped_config[0]

        # Assert operation was renamed correctly
        self.assertEqual(mapped_operation["operation"], "findCoincidingGeometry")

        # Assert argument was renamed correctly
        self.assertEqual(mapped_operation["primPaths"], ["foo"])

    async def test_map_invalid_data(self):
        """Test mapping invalid data"""

        # Define an invalid config - "operation" typo, and an invalid operation
        # neither should be included in the returned config
        old_config = [{"opreation": "abc"}, {"operation": "no such operation"}]

        # Map
        mapped = standalone.map_config(json.dumps(old_config))
        mapped_config = json.loads(mapped)

        # Should be different
        self.assertNotEqual(old_config, mapped_config)

        # No entries - both were invalid and therefore not appended
        self.assertEqual(len(mapped_config), 0)
