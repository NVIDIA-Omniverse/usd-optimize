# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


__all__ = ["Operation"]

import base64
import json

from pxr import UsdUtils


class Operation:
    """
    Base class for Scene Optimizer python plugin operations.

    In order to create a new operation, derive a new class from this class and implement the `author` and `version
    properties, and the `execute` method.

    The operation is registered by implementing a global function named `SO_PLUGIN_INIT` that returns an instance of the
    derived operation.

    The operation's arguments should be declared in the constructor using the `add_argument` method.
    """

    ArgumentDisplayTypeBool = "bool"
    ArgumentDisplayTypeCode = "code"
    ArgumentDisplayTypeEnum = "enum"
    ArgumentDisplayTypeFloat = "float"
    ArgumentDisplayTypeFloatArray = "floatArray"
    ArgumentDisplayTypeFloatSlider = "floatSlider"
    ArgumentDisplayTypeInt = "int"
    ArgumentDisplayTypeIntSlider = "intSlider"
    ArgumentDisplayTypePrimPath = "primPath"
    ArgumentDisplayTypePrimPaths = "primPaths"
    ArgumentDisplayTypeText = "text"
    ArgumentDisplayTypeTextList = "textList"

    def __init__(self, name, display_name, description):
        """
        Constructor.

        Args:
            name (str): The unique name of the operation.
            display_name (str): The display name of the operation for UI purposes.
            description (str): A description of what the operation does.
        """
        self.__name = name
        self.__display_name = display_name
        self.__description = description
        self.__argument_names = set()
        self.__arguments = []
        self.__usd_stage = None

    @property
    def name(self):
        """
        The unique name of the operation.
        """
        return self.__name

    @property
    def display_name(self):
        """
        The display name of the operation for UI purposes.
        """
        return self.__display_name

    @property
    def description(self):
        """
        A description of what the operation does.
        """
        return self.__description

    @property
    def author(self):
        """
        To be implemented by derived classes. Returns a `str` representing the author of the operation.
        """
        raise NotImplementedError("author must be implemented by derived classes")  # pragma: no cover

    @property
    def version(self):
        """
        To be implemented by derived classes. Returns a `tuple` of three `int`s representing the version of the
        operation.
        """
        raise NotImplementedError("version must be implemented by derived classes")  # pragma: no cover

    @property
    def visible(self):
        """
        Returns whether the operation is visible in the UI.
        """
        return True

    def add_argument(
        self,
        name: str,
        display_name: str,
        display_type: str,
        description: str,
        default_value,
        enum_values: dict = None,
        join_next: tuple = None,
        min: float = None,
        max: float = None,
        placeholder: str = None,
        precision: int = None,
        visible: bool = None,
        enable_if: str = None,
        visible_if: str = None,
        metadata: dict = {},
    ):
        """
        Adds an argument to the operation, should be called in derived a class' constructor.

        Args:
            name (str): The unique name of the argument.
            display_name (str): The display name of the argument for UI purposes.
            display_type (str): The display type of the argument. One of the `ArgumentDisplayType*` constants.
            description (str): A description of what the argument does.
            default_value: The default value of the argument.
            enum_values (dict): Used if the display type is `ArgumentDisplayTypeEnum`: A dictionary of values mapped to
                                the enum labels representing the value.
            join_next (tuple): If set groups this argument with the next argument in the UI. Defined by a tuple of two
                               strings, the first string is the name of the argument group, and the second is the
                               description of the group.
            min (float): The minimum value of the argument for slider display types.
            max (float): The maximum value of the argument for slider display types.
            placeholder (str): The placeholder text in the UI if the argument value is empty.
            precision (int): Indicates how many digits of precision to display for float arguments.
            visible (bool): Whether the argument is visible in the UI.
            enable_if (str): A string representing a boolean expression that determines whether the argument is enabled.
            visible_if (str): A string representing a boolean expression that determines whether the argument is
                              visible.
            metadata (dict): A dictionary of arbitrary metadata for the argument.
        """
        if name in self.__argument_names:
            return

        # serialize keyword args into metadata
        if enum_values is not None:
            metadata["enums"] = enum_values
        if join_next is not None:
            metadata["joinNext"] = join_next[0]
            metadata["joinNextDescription"] = join_next[1]
        if min is not None:
            metadata["min"] = min
        if max is not None:
            metadata["max"] = max
        if placeholder is not None:
            metadata["placeholder"] = placeholder
        if precision is not None:
            metadata["precision"] = precision
        if visible is not None:
            metadata["visible"] = visible
        if enable_if is not None:
            metadata["enableIf"] = enable_if
        if visible_if is not None:
            metadata["visibleIf"] = visible_if

        # does the value need encoding?
        if display_type == Operation.ArgumentDisplayTypeCode and isinstance(default_value, str):
            readable_bytes = default_value.encode("ascii")
            base64_bytes = base64.b64encode(readable_bytes)
            default_value = base64_bytes.decode("ascii")

        arg_data = {
            "name": name,
            "displayName": display_name,
            "displayType": display_type,
            "description": description,
            "defaultValue": default_value,
            "metadata": metadata,
        }

        # store
        self.__argument_names.add(name)
        self.__arguments.append(arg_data)

    def get_usd_stage(self):
        """
        Returns the USD stage that the operation is being executed on.
        """
        return self.__usd_stage

    def execute(self, args):
        """
        To be implemented by derived classes. Executes the operation.

        Args:
            args (dict): A dictionary mapping from argument names to their execution values.

        Returns:
            bool: Whether the operation was successful.
        """
        raise NotImplementedError("execute must be implemented by derived classes")  # pragma: no cover

    def _serialize_arguments(self):
        arg_list = []
        for arg_data in self.__arguments:
            arg_list.append(json.dumps(arg_data))
        return arg_list

    def _execute(self, stage_id, args):
        # retrieve the Usd stage from the stage cache
        stage_cache = UsdUtils.StageCache.Get()
        self.__usd_stage = stage_cache.Find(stage_cache.Id.FromLongInt(stage_id))
        return self.execute(json.loads(args))
