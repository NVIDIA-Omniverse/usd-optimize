# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from __future__ import annotations

import logging
import os
from dataclasses import dataclass
from types import MappingProxyType
from typing import Any, Callable, ClassVar, Dict, Iterable, List, Mapping, Optional, Tuple, Union

from pxr import Usd
from usd_optimize.core import analysis
from usd_validation_nvidia import BaseRuleChecker, ParameterType, ValidationEngine
from usd_validation_nvidia.capabilities import Parameters as _RequirementParameters

try:
    from usd_validation_nvidia import multiprocess_safe
except ImportError:

    def multiprocess_safe(method):  # no-op fallback; leaves the method unmarked
        return method


logger = logging.getLogger(__name__)

# Environment variable that seeds the default verbose state at import time.
# Accepted truthy values are case-insensitive ``1``/``true``/``yes``/``on``.
# The validators driver ``--verbose`` flag and :func:`set_verbose` both override
# whatever this resolves to.
_VERBOSE_ENV_VAR = "USD_OPTIMIZE_VALIDATOR_VERBOSE"

# Name of the engine parameter that toggles verbose per-prim reporting. Exposed
# through the usd-validation-nvidia parameter system so callers driving the
# ValidationEngine directly (e.g. nvidia_usd_validate) can turn it on without
# our CLI, e.g. ``--param VERBOSE=true``.
_VERBOSE_PARAM_NAME = "VERBOSE"


def _env_verbose_default() -> bool:
    """Resolve the initial verbose state from the environment."""
    return os.environ.get(_VERBOSE_ENV_VAR, "").strip().lower() in ("1", "true", "yes", "on")


def set_verbose(value: bool = True) -> None:
    """Toggle per-prim verbose issue emission for all Usd Optimize rules.

    When enabled, rules that otherwise summarize their findings as a single
    aggregate issue also emit one issue per failing prim (with the prim path in
    the issue ``Location``). Off by default to keep output volume unchanged.
    """
    BaseUsdOptimizeChecker.VERBOSE = bool(value)


# Marker set on the patched ``ValidationEngine.parameters`` getter so a
# subsequent module reload (e.g. Omniverse extension hot-reload) detects the
# already-patched property instead of wrapping it a second time and growing
# the getter chain.
_PARAMETER_PROVIDER_SENTINEL = "_usd_optimize_parameter_provider"


@dataclass(frozen=True)
class Parameter:
    """Declarative spec for a user-tunable rule argument.

    ``default`` is the value the rule uses when no Asset Validator
    user-parameter overrides it. ``op_arg`` is the key under which the value
    is forwarded to the backing Usd Optimize operation.
    """

    op_arg: str
    default: Any
    description: str = ""


@dataclass(frozen=True)
class _AssetValidatorParameter:
    """Internal shape advertised through ``ValidationEngine.parameters``.

    Matches the structure Asset Validator's parameter mapping expects:
    a display name, type, default-as-assigned-value, and optional enum
    values. Kept private; rule authors use :class:`Parameter` instead.
    """

    display_name: str
    type: ParameterType
    assigned_value: Any
    enum_values: Tuple[str, ...] | None = None


def _parameter_type(value: Any) -> ParameterType | None:
    """Map a Python default value to its Asset Validator ParameterType.

    Non-scalar defaults (lists, dicts, None) return ``None`` so they are
    skipped during parameter advertisement. Strings map to
    ``ParameterType.ENUM`` since Asset Validator has no freeform-string
    type; rules with discrete string choices can later extend
    :class:`Parameter` to supply ``enum_values``.
    """
    if isinstance(value, bool):
        return ParameterType.BOOL
    if isinstance(value, int):
        return ParameterType.INT
    if isinstance(value, float):
        return ParameterType.FLOAT
    if isinstance(value, str):
        return ParameterType.ENUM
    return None


@dataclass(frozen=True)
class ParameterFromOpArg:
    """
    Placeholder in PARAMETERS, resolved per-subclass once OPERATION_NAME is
    known
    """

    op_arg: str
    default: Any = None
    description: Optional[str] = None


class BaseUsdOptimizeChecker(BaseRuleChecker):
    """Base checker for Usd Optimize analysis

    Handles executing a Usd Optimize operation with analysis mode enabled
    and validating the result.

    The analysis payload can then be passed to a derived Checker to process
    issues specific to that operation.

    Subclasses declare static op arguments in :attr:`OPERATION_ARGS` and
    user-tunable arguments in :attr:`PARAMETERS`. The default
    :meth:`_GetArgs` overlays Asset Validator parameter overrides onto
    OPERATION_ARGS using each :class:`Parameter`'s ``op_arg`` as the
    destination key; subclasses that need custom logic can still override
    ``_GetArgs``.
    """

    OPERATION_NAME: str = None
    # Immutable defaults so subclasses that forget to assign cannot mutate the
    # inherited shared mapping in place. Subclasses override by assigning a new
    # dict (or any Mapping) — the immutability is a footgun guard, not a
    # constraint on subclass declarations.
    OPERATION_ARGS: ClassVar[Mapping[str, Any]] = MappingProxyType({})
    PARAMETERS: ClassVar[Mapping[str, Parameter]] = MappingProxyType({})

    # When True, checkers that report an aggregate count also emit one issue per
    # failing prim so the prim paths land in the issue Location (and CSV).
    # Toggle via :func:`set_verbose`, the validators driver ``--verbose`` flag, or
    # the ``USD_OPTIMIZE_VALIDATOR_VERBOSE`` env var. Seeded from the env at
    # import; left as a plain class attribute so tests can flip it directly.
    VERBOSE: ClassVar[bool] = _env_verbose_default()

    @classmethod
    def _resolve_parameter_from_operation(cls, args_info: List, arg: ParameterFromOpArg) -> Parameter:
        arg_name = arg.op_arg
        default_value = arg.default
        description = arg.description
        # find the arg in the info from the operation
        for info in args_info:
            name = info.get("name", None)
            if arg_name == name:
                # resolve default value and description if needed
                if default_value is None:
                    default_value = info.get("defaultValue", None)
                if description is None:
                    description = info.get("description", "")
        return Parameter(op_arg=arg_name, default=default_value, description=description)

    @classmethod
    def _generate_docstring(cls, description: str) -> str:
        """Build a rule docstring from ``description`` plus this rule's PARAMETERS.

        Returns ``description`` followed by a ``**Parameters:**`` section that
        lists every entry declared in :attr:`PARAMETERS` (resolved on the
        derived class via ``cls``) with its per-parameter description and
        default value. When the rule declares no parameters the description is
        returned unchanged.
        """
        doc = description.strip()
        if not cls.PARAMETERS:
            return doc

        lines = [doc, "", "**Parameters:**", ""]
        for name, param in cls.PARAMETERS.items():
            detail = ""
            default_str = ""
            if param.description:
                detail = param.description.strip()
                if detail and not detail.endswith("."):
                    detail += "."
                detail += " "
                if isinstance(param.default, float):
                    default_str = f"{param.default:.6g}"
                else:
                    default_str = str(param.default)
            lines.append(f"    - `{name}`: {detail}Default: `{default_str}`.")
        return "\n".join(lines)

    @classmethod
    def get_parameter_definitions(cls) -> List[_AssetValidatorParameter]:
        """Return Asset Validator parameter specs for this rule.

        Each declared :attr:`PARAMETERS` entry produces two definitions: an
        unqualified one (``NAME``) and a qualified one (``ClassName.NAME``)
        so callers can disambiguate when two rules expose the same parameter
        name. Defaults whose Python type doesn't map to a ParameterType
        (e.g. lists) are skipped.
        """
        defs: List[_AssetValidatorParameter] = []
        for name, param in cls.PARAMETERS.items():
            ptype = _parameter_type(param.default)
            if ptype is None:
                continue
            defs.append(_AssetValidatorParameter(name, ptype, param.default))
            defs.append(_AssetValidatorParameter(f"{cls.__name__}.{name}", ptype, param.default))

        # Advertise the global verbose toggle so it is settable through the
        # usd-validation-nvidia parameter system (in addition to set_verbose()
        # and the env var). Both an unqualified and a per-rule-qualified form
        # are offered, mirroring the PARAMETERS handling above.
        defs.append(_AssetValidatorParameter(_VERBOSE_PARAM_NAME, ParameterType.BOOL, cls.VERBOSE))
        defs.append(_AssetValidatorParameter(f"{cls.__name__}.{_VERBOSE_PARAM_NAME}", ParameterType.BOOL, cls.VERBOSE))
        return defs

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)
        # first get the args from the operation so we only have to do it once
        # this is allowed to fail silently since some checkers may not want to
        # pull args from the operation
        args_info = []
        try:
            from usd_optimize.core import UsdOptimizeCore

            args_info = UsdOptimizeCore.getInstance().getOperationArguments(cls.OPERATION_NAME)
        except Exception:
            pass
        # resolve any ParameterFromOpArg entries in PARAMETERS to regular Parameters
        resolved = {
            name: (cls._resolve_parameter_from_operation(args_info, p) if isinstance(p, ParameterFromOpArg) else p)
            for name, p in cls.PARAMETERS.items()
        }
        cls.PARAMETERS = MappingProxyType(resolved)

        # Regenerate the rule docstring from the subclass's own description (the
        # docstring it declared) with the resolved PARAMETERS appended. Only the
        # subclass's own docstring is used so we don't re-append to (or inherit)
        # a parent's already-generated text.
        own_doc = cls.__dict__.get("__doc__")
        if own_doc:
            cls.__doc__ = cls._generate_docstring(own_doc)

    def _effective_args(self) -> Dict[str, Any]:
        """Build the op-args dict from OPERATION_ARGS + user-overridden PARAMETERS.

        Qualified parameter names (``ClassName.NAME``) win over unqualified
        names when both are supplied. Parameters without a user override fall
        back to :attr:`Parameter.default`.
        """
        args = dict(self.OPERATION_ARGS)
        if not self.PARAMETERS:
            return args

        rule_name = type(self).__name__
        mapping = self.parameters
        for name, param in self.PARAMETERS.items():
            value = param.default

            for lookup in (name, f"{rule_name}.{name}"):
                entry = (
                    mapping.get(lookup) if hasattr(mapping, "get") else (mapping[lookup] if lookup in mapping else None)
                )
                # A requirement's spec value is upstream's generic default; the rule's own default wins over it.
                if entry is None or isinstance(entry, (_AssetValidatorParameter, _RequirementParameters)):
                    continue
                assigned = getattr(entry, "assigned_value", None)
                if assigned is not None:
                    value = assigned

            args[param.op_arg] = value

        return args

    def _GetArgs(self):
        """Get arguments to use in execution.

        Default implementation returns :meth:`_effective_args`. Subclasses
        with bespoke arg-building logic can override.
        """
        return self._effective_args()

    def _AnalyzeStage(self, usdStage: Usd.Stage, operation_name: str, args: Dict = None) -> Tuple:
        """
        Runs Usd Optimize analysis on the given USD stage with the specified operation, reports a failure if found,
        and the returns the analysis result.
        """
        # Implementation error
        if not operation_name:
            self._AddFailedCheck(message="Invalid rule, no operation configured")
            return None

        analysis_result: Dict = analysis.analyze(usdStage, [analysis.OperationConfig(operation_name, args=args)])
        if not analysis_result:
            self._AddFailedCheck(message="Failed to run Usd Optimize analysis with unknown error.")
            return None

        # Extract sets of duplicates from the result
        operation_result: tuple = analysis_result.get(operation_name)
        if not operation_result:
            self._AddFailedCheck(message="Failed to run Usd Optimize analysis with unknown error.")
            return None

        # result should be a 3-tuple
        if len(operation_result) != 3:
            self._AddFailedCheck(message="Usd Optimize analysis returned invalid result.")
            return None

        # did analysis run successfully?
        if operation_result[0] is False:
            self._AddFailedCheck(message=f"Analysis encountered error: {operation_result[1]}")
            return None

        # resolve the suggested operations from the analysis result
        suggested_operations = analysis.create_operations_from_analysis_result(analysis_result)

        return (operation_result[2].get("analysis"), suggested_operations)

    def _verbose_enabled(self) -> bool:
        """Resolve verbose state for this check.

        An engine parameter override (``VERBOSE`` or ``<RuleName>.VERBOSE``)
        wins so callers driving the ValidationEngine directly can toggle it
        through usd-validation-nvidia's own parameter system; otherwise the
        class-level :attr:`VERBOSE` flag (set by :func:`set_verbose` or the env
        var) applies. Reading the parameter mapping is best-effort: rules used
        outside an engine may not have one.

        The qualified per-rule form (``<RuleName>.VERBOSE``) is checked first so
        it overrides the unqualified global form, matching the precedence
        :meth:`_effective_args` gives every other parameter.
        """
        mapping = getattr(self, "parameters", None)
        if mapping is not None:
            rule_name = type(self).__name__
            for lookup in (f"{rule_name}.{_VERBOSE_PARAM_NAME}", _VERBOSE_PARAM_NAME):
                try:
                    entry = mapping.get(lookup) if hasattr(mapping, "get") else None
                except Exception:
                    entry = None
                # Skip the framework's injected default specs (advertised via
                # get_parameter_definitions); honoring them would let the
                # advertised default mask a runtime set_verbose()/env-var toggle.
                # Only a genuine user-supplied override counts. Mirrors
                # _effective_args.
                if entry is None or isinstance(entry, _AssetValidatorParameter):
                    continue
                assigned = getattr(entry, "assigned_value", None)
                if isinstance(assigned, bool):
                    return assigned
        return self.VERBOSE

    def _AddVerbosePrimWarnings(
        self,
        usdStage: Usd.Stage,
        paths: Iterable[str],
        message: Union[str, Callable[[str], str]],
    ) -> None:
        """Emit one warning per failing prim, but only in verbose mode.

        Callers keep their existing aggregate summary issue and call this right
        after it; it is a no-op when verbose is disabled, so the default output
        is unchanged. ``message`` is either a static string or a callable that
        takes the prim path and returns the per-prim message.
        """
        if not self._verbose_enabled():
            return
        for path in paths:
            text = message(path) if callable(message) else message
            self._AddWarning(message=text, at=usdStage.GetPrimAtPath(path))

    def _CheckStage(self, usdStage: Usd.Stage, analysis: dict):
        """Derived checkers should implement this function.

        Subclasses that need the suggested operations from analysis can access
        them via ``self.suggested_operations``.
        """
        pass

    # The single line that makes every rule poolable -- the engine pools only marked
    # tasks, and no subclass overrides CheckStage.
    @multiprocess_safe
    def CheckStage(self, usdStage: Usd.Stage):
        """Base setup/execution of analysis mode for a usd optimize operation"""
        analysis_result = None
        try:
            result = self._AnalyzeStage(usdStage, self.OPERATION_NAME, args=self._GetArgs())
            if result is not None:
                analysis_result, self.suggested_operations = result
        except Exception as ex:
            print("Failed to analyze stage:", ex)

        if analysis_result:
            # Defer to the derived class to process the result
            self._CheckStage(usdStage, analysis_result)


def _install_parameter_provider() -> None:
    """Teach ``ValidationEngine.parameters`` about declared rule PARAMETERS.

    Wraps the engine's ``parameters`` getter so each rule's declared
    parameters appear in the mapping (with their defaults) when not already
    present from user input. User-supplied entries take precedence because
    they already populate the mapping before the wrapper merges defaults.

    Idempotent across module reloads: a sentinel on the patched getter
    prevents the wrapper chain from growing each time this module is
    re-executed (e.g. Omniverse extension hot-reload).
    """
    parameters_property = ValidationEngine.parameters
    original_getter = parameters_property.fget
    if original_getter is None:
        return
    if getattr(original_getter, _PARAMETER_PROVIDER_SENTINEL, False):
        return

    def parameters(self):
        mapping = original_getter(self)
        for rule in self.rules:
            if not isinstance(rule, type) or not issubclass(rule, BaseUsdOptimizeChecker):
                continue
            for parameter in rule.get_parameter_definitions():
                if parameter.display_name not in mapping:
                    mapping.add(parameter)
        return mapping

    setattr(parameters, _PARAMETER_PROVIDER_SENTINEL, True)
    ValidationEngine.parameters = property(
        parameters,
        parameters_property.fset,
        parameters_property.fdel,
        parameters_property.__doc__,
    )


_install_parameter_provider()
