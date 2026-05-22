# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Stub for omni.scene.optimizer.core.scripts.commands.

Tests should use ``_execute_operation`` / ``_execute_command`` from
``test_utils`` instead of instantiating ``SceneOptimizerOperation`` directly.
"""


class SceneOptimizerOperation:
    """Stub that raises immediately — tests should use _execute_command."""

    def __init__(self, operation, args=None, context=None):
        raise NotImplementedError(
            "SceneOptimizerOperation is not available in this build. "
            "Use _execute_command() or _execute_operation() from test_utils instead."
        )
