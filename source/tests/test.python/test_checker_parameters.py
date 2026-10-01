# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Tests for the declarative Usd Optimize checker parameter mechanism.

Exercises the `BaseUsdOptimizeChecker.PARAMETERS` / `_effective_args` path
and the `ValidationEngine.parameters` bridge installed in
`base_usd_optimize_checker`. Synthetic subclasses are used for the
mechanism tests; real checkers (`SmallMeshChecker`, `OccludedMeshesChecker`)
are used for the integration smoke tests.
"""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any
from unittest import TestCase
from unittest.mock import MagicMock, patch

from pxr import Sdf, Usd
from usd_optimize.validators import (
    CoincidingGeometryChecker,
    ColocatedVerticesChecker,
    EmptyLeafChecker,
    FlatHierarchiesChecker,
    HighVertexCountChecker,
    NonManifoldChecker,
    NormalsChecker,
    OccludedMeshesChecker,
    PrimitiveFitChecker,
    RedundantTimeSamplesChecker,
    RtxMeshCountChecker,
    SmallMeshChecker,
    set_verbose,
)
from usd_optimize.validators.base_usd_optimize_checker import (
    BaseUsdOptimizeChecker,
    Parameter,
    _AssetValidatorParameter,
    _parameter_type,
)
from usd_validation_nvidia import ParameterMapping, ParameterType, ValidationEngine


@dataclass(frozen=True)
class _TestParameter:
    """Quacks like Asset Validator's UserParameter for ParameterMapping ingestion."""

    display_name: str
    type: ParameterType
    assigned_value: Any
    enum_values: tuple[str, ...] | None = None


def _make_mapping(values: dict[str, Any]) -> ParameterMapping:
    return ParameterMapping(
        _TestParameter(display_name=name, type=_parameter_type(value), assigned_value=value)
        for name, value in values.items()
    )


class _ParameterRule(BaseUsdOptimizeChecker):
    """Synthetic rule exercising mixed OPERATION_ARGS + PARAMETERS."""

    OPERATION_NAME = "parameterRule"
    OPERATION_ARGS = {"static": "fixed"}
    PARAMETERS = {
        "MY_THRESHOLD": Parameter(default=1.0, op_arg="threshold"),
        "ENABLED": Parameter(default=False, op_arg="enabled"),
    }


class _OtherParameterRule(BaseUsdOptimizeChecker):
    """Second synthetic rule, shares the unqualified MY_THRESHOLD name."""

    OPERATION_NAME = "otherParameterRule"
    PARAMETERS = {
        "MY_THRESHOLD": Parameter(default=10.0, op_arg="threshold"),
    }


class TestParameterDefinitions(TestCase):
    """Validate `get_parameter_definitions` advertises both naming forms."""

    def test_includes_unqualified_and_qualified_for_each_parameter(self):
        defs = {d.display_name: d for d in _ParameterRule.get_parameter_definitions()}
        self.assertIn("MY_THRESHOLD", defs)
        self.assertIn("_ParameterRule.MY_THRESHOLD", defs)
        self.assertIn("ENABLED", defs)
        self.assertIn("_ParameterRule.ENABLED", defs)

    def test_advertises_declared_default_as_assigned_value(self):
        defs = {d.display_name: d for d in _ParameterRule.get_parameter_definitions()}
        self.assertEqual(defs["MY_THRESHOLD"].assigned_value, 1.0)
        self.assertEqual(defs["ENABLED"].assigned_value, False)

    def test_maps_python_types_to_asset_validator_parameter_types(self):
        defs = {d.display_name: d for d in _ParameterRule.get_parameter_definitions()}
        self.assertEqual(defs["MY_THRESHOLD"].type, ParameterType.FLOAT)
        self.assertEqual(defs["ENABLED"].type, ParameterType.BOOL)

    def test_rule_without_parameters_advertises_only_verbose(self):
        class _NoParamsRule(BaseUsdOptimizeChecker):
            OPERATION_NAME = "noParams"

        # The global VERBOSE toggle is always advertised, even for rules that
        # declare no PARAMETERS of their own.
        defs = {d.display_name: d for d in _NoParamsRule.get_parameter_definitions()}
        self.assertEqual(set(defs), {"VERBOSE", "_NoParamsRule.VERBOSE"})
        self.assertEqual(defs["VERBOSE"].type, ParameterType.BOOL)
        self.assertEqual(defs["VERBOSE"].assigned_value, False)


class TestEffectiveArgs(TestCase):
    """Validate `_effective_args` overlay semantics for parameter overrides."""

    def test_no_overrides_falls_back_to_declared_defaults(self):
        rule = _ParameterRule()
        self.assertEqual(
            rule._effective_args(),
            {"static": "fixed", "threshold": 1.0, "enabled": False},
        )

    def test_unqualified_override_replaces_default(self):
        rule = _ParameterRule(parameters=_make_mapping({"MY_THRESHOLD": 2.5}))
        args = rule._effective_args()
        self.assertEqual(args["threshold"], 2.5)
        self.assertEqual(args["enabled"], False)
        self.assertEqual(args["static"], "fixed")

    def test_qualified_override_replaces_default(self):
        rule = _ParameterRule(parameters=_make_mapping({"_ParameterRule.MY_THRESHOLD": 3.5}))
        self.assertEqual(rule._effective_args()["threshold"], 3.5)

    def test_qualified_override_wins_over_unqualified(self):
        rule = _ParameterRule(parameters=_make_mapping({"MY_THRESHOLD": 2.5, "_ParameterRule.MY_THRESHOLD": 3.5}))
        self.assertEqual(rule._effective_args()["threshold"], 3.5)

    def test_qualified_override_scoped_to_declaring_rule(self):
        mapping = _make_mapping(
            {
                "_ParameterRule.MY_THRESHOLD": 3.5,
                "_OtherParameterRule.MY_THRESHOLD": 7.5,
            }
        )
        self.assertEqual(_ParameterRule(parameters=mapping)._effective_args()["threshold"], 3.5)
        self.assertEqual(_OtherParameterRule(parameters=mapping)._effective_args()["threshold"], 7.5)

    def test_unknown_parameter_names_are_ignored(self):
        rule = _ParameterRule(
            parameters=_make_mapping(
                {
                    "UNKNOWN": 999.0,
                    "_ParameterRule.UNKNOWN": 999.0,
                    "_OtherParameterRule.MY_THRESHOLD": 7.5,
                }
            )
        )
        self.assertEqual(
            rule._effective_args(),
            {"static": "fixed", "threshold": 1.0, "enabled": False},
        )

    def test_operation_args_preserved_when_no_parameters_overlap(self):
        rule = _ParameterRule(parameters=_make_mapping({"MY_THRESHOLD": 2.5}))
        # The static OPERATION_ARGS key must survive _effective_args unchanged.
        self.assertEqual(rule._effective_args()["static"], "fixed")


class TestValidationEngineIntegration(TestCase):
    """Validate `ValidationEngine.parameters` advertises declared definitions."""

    def test_enabled_rule_exposes_unqualified_and_qualified_parameters(self):
        engine = ValidationEngine(init_rules=False)
        engine.enable_rule(_ParameterRule)
        parameters = engine.parameters
        self.assertIn("MY_THRESHOLD", parameters)
        self.assertIn("_ParameterRule.MY_THRESHOLD", parameters)
        self.assertIn("ENABLED", parameters)
        self.assertIn("_ParameterRule.ENABLED", parameters)

    def test_non_declared_parameters_not_added_by_bridge(self):
        engine = ValidationEngine(init_rules=False)
        engine.enable_rule(_ParameterRule)
        self.assertNotIn("UNKNOWN", engine.parameters)


class TestRealCheckerIntegration(TestCase):
    """Smoke-test that the converted real checkers expose the expected knobs."""

    def test_small_mesh_checker_declares_size_threshold(self):
        defs = {d.display_name: d for d in SmallMeshChecker.get_parameter_definitions()}
        self.assertIn("SIZE_THRESHOLD", defs)
        self.assertIn("UsdOptimizeSmallMeshChecker.SIZE_THRESHOLD", defs)
        self.assertAlmostEqual(defs["SIZE_THRESHOLD"].assigned_value, 0.001)
        self.assertEqual(defs["SIZE_THRESHOLD"].type, ParameterType.FLOAT)

    def test_small_mesh_checker_threshold_flows_to_op_arg(self):
        rule = SmallMeshChecker(parameters=_make_mapping({"SIZE_THRESHOLD": 0.5}))
        args = rule._effective_args()
        self.assertEqual(args["threshold"], 0.5)
        self.assertEqual(args["removeMethod"], 1)
        self.assertEqual(args["detectionMethod"], 0)

    def test_small_mesh_warning_message_reflects_threshold_override(self):
        """Guards against regression where the warning text hardcodes the default.

        `_CheckStage` builds the warning message via ``self._effective_args()
        ["threshold"]`` rather than ``self.PARAMETERS["SIZE_THRESHOLD"].default``;
        if a refactor reverts that, this catches it.
        """
        rule = SmallMeshChecker(parameters=_make_mapping({"SIZE_THRESHOLD": 0.3}))
        rule._AddWarning = MagicMock()
        rule.suggested_operations = []
        stage = Usd.Stage.CreateInMemory()

        rule._CheckStage(stage, {"smallGeometry": ["/Geometry/Mesh"]})

        stage_warning = next(
            call for call in rule._AddWarning.call_args_list if "below size threshold" in call.kwargs.get("message", "")
        )
        self.assertIn("0.3", stage_warning.kwargs["message"])

    def test_occluded_meshes_checker_declares_all_five_parameters(self):
        defs = {d.display_name: d for d in OccludedMeshesChecker.get_parameter_definitions()}
        for name, expected_default, expected_type in (
            ("USE_GPU", False, ParameterType.BOOL),
            ("CHECK_TRANSPARENCY", True, ParameterType.BOOL),
            ("CLUSTERED", True, ParameterType.BOOL),
            ("MINIMUM_GAP_SIZE", 0.01, ParameterType.FLOAT),
            ("MAXIMUM_GRID_RESOLUTION", 500.0, ParameterType.FLOAT),
        ):
            with self.subTest(parameter=name):
                self.assertIn(name, defs)
                self.assertIn(f"UsdOptimizeOccludedMeshesChecker.{name}", defs)
                self.assertAlmostEqual(defs[name].assigned_value, expected_default)
                self.assertEqual(defs[name].type, expected_type)

    def test_coinciding_geometry_parameters_flow_to_op_arg(self):
        rule = CoincidingGeometryChecker(parameters=_make_mapping({"TOLERANCE": 0.05, "OFFSET": 0.5, "FUZZY": True}))
        args = rule._effective_args()
        self.assertEqual(args["tolerance"], 0.05)
        self.assertEqual(args["offset"], 0.5)
        self.assertEqual(args["fuzzy"], True)

    def test_colocated_vertices_parameters_flow_to_op_arg(self):
        rule = ColocatedVerticesChecker(parameters=_make_mapping({"TOLERANCE": 0.05}))
        self.assertEqual(rule._effective_args()["tolerance"], 0.05)

    def test_flat_hierarchies_parameters_flow_to_op_arg(self):
        rule = FlatHierarchiesChecker(parameters=_make_mapping({"MAX_CHILDREN": 5, "CONSIDER_ALL_CHILDREN": False}))
        args = rule._GetArgs()
        self.assertEqual(args["maxChildren"], 5)
        self.assertEqual(args["considerAllChildren"], False)

    def test_high_vertex_count_parameters_flow_to_op_arg(self):
        rule = HighVertexCountChecker(
            parameters=_make_mapping({"LEVEL_HIGH": 40000, "LEVEL_VERY_HIGH": 60000, "LEVEL_EXTREME": 150000})
        )
        args = rule._effective_args()
        self.assertEqual(args["high"], 40000)
        self.assertEqual(args["veryHigh"], 60000)
        self.assertEqual(args["extreme"], 150000)

    def test_primitive_fit_parameters_flow_to_op_arg(self):
        rule = PrimitiveFitChecker(
            parameters=_make_mapping(
                {
                    "VERTEX_TOLERANCE": 0.5,
                    "VOLUME_TOLERANCE": 0.25,
                    "IGNORE_SUBSETS": False,
                }
            )
        )
        args = rule._effective_args()
        self.assertEqual(args["vertexTolerance"], 0.5)
        self.assertEqual(args["volumeTolerance"], 0.25)
        self.assertEqual(args["ignoreSubsets"], False)

    def test_redundant_timesamples_parameters_flow_to_op_arg(self):
        rule = RedundantTimeSamplesChecker(parameters=_make_mapping({"EPSILON_DOUBLE": 1e-6, "EPSILON_FLOAT": 1e-3}))
        args = rule._effective_args()
        self.assertEqual(args["epsilonD"], 1e-6)
        self.assertEqual(args["epsilonF"], 1e-3)

    def test_rtx_mesh_count_limit_is_advertised_parameter(self):
        # RTX_UNIQUE_MESH_COUNT_LIMIT is a checker-side threshold (not an op arg)
        # but must still be advertised as a tunable parameter.
        defs = {d.display_name: d for d in RtxMeshCountChecker.get_parameter_definitions()}
        self.assertIn("RTX_UNIQUE_MESH_COUNT_LIMIT", defs)
        self.assertIn("UsdOptimizeRtxMeshCountChecker.RTX_UNIQUE_MESH_COUNT_LIMIT", defs)
        self.assertEqual(defs["RTX_UNIQUE_MESH_COUNT_LIMIT"].assigned_value, 438000)
        self.assertEqual(defs["RTX_UNIQUE_MESH_COUNT_LIMIT"].type, ParameterType.INT)

    def test_rtx_mesh_count_limit_override_resolves(self):
        rule = RtxMeshCountChecker(parameters=_make_mapping({"RTX_UNIQUE_MESH_COUNT_LIMIT": 6}))
        self.assertEqual(rule._effective_args()["rtxUniqueMeshCountLimit"], 6)

    def test_rtx_mesh_count_limit_default_resolves(self):
        rule = RtxMeshCountChecker()
        self.assertEqual(rule._effective_args()["rtxUniqueMeshCountLimit"], 438000)

    def test_rtx_mesh_count_limit_not_forwarded_to_operation(self):
        # The threshold must not leak into the op args passed to rtxMeshCount.
        rule = RtxMeshCountChecker(parameters=_make_mapping({"RTX_UNIQUE_MESH_COUNT_LIMIT": 6}))
        self.assertEqual(rule._GetArgs(), {})

    def test_occluded_meshes_checker_overrides_flow_to_op_args(self):
        rule = OccludedMeshesChecker(
            parameters=_make_mapping(
                {
                    "USE_GPU": True,
                    "MINIMUM_GAP_SIZE": 0.5,
                    "UsdOptimizeOccludedMeshesChecker.MAXIMUM_GRID_RESOLUTION": 1000.0,
                }
            )
        )
        args = rule._effective_args()
        self.assertEqual(args["useGpu"], True)
        self.assertEqual(args["checkTransparency"], True)  # untouched default
        self.assertEqual(args["clustered"], True)  # untouched default
        self.assertEqual(args["minimumGapSize"], 0.5)
        self.assertEqual(args["maximumGridResolution"], 1000.0)


class TestFixerParameterHookup(TestCase):
    """Validate that each checker's fix (Suggestion callable) runs its backing
    operation with the tuned parameter overrides, not hardcoded defaults."""

    @staticmethod
    def _first_op_args(mock_optimize):
        """Return the args of the first OperationConfig the fixer built.

        ``analysis.optimize(usdStage, operations)`` is called positionally, so
        ``operations`` is the second positional arg.
        """
        operations = mock_optimize.call_args[0][1]
        return operations[0].args

    def test_colocated_vertices_fixer_uses_tolerance(self):
        rule = ColocatedVerticesChecker(parameters=_make_mapping({"TOLERANCE": 0.05}))
        stage = Usd.Stage.CreateInMemory()
        with patch("usd_optimize.validators.colocated_vertices_checker.analysis.optimize") as mock_optimize:
            rule._mesh_merge_vertices(stage, stage.GetPrimAtPath("/"))
        args = self._first_op_args(mock_optimize)
        self.assertEqual(args["tolerance"], 0.05)
        self.assertEqual(args["mergeVertices"], True)  # fix intent preserved

    def test_redundant_timesamples_fixer_uses_epsilon(self):
        rule = RedundantTimeSamplesChecker(parameters=_make_mapping({"EPSILON_DOUBLE": 1e-6, "EPSILON_FLOAT": 1e-3}))
        stage = Usd.Stage.CreateInMemory()
        attr = stage.DefinePrim("/Foo").CreateAttribute("bar", Sdf.ValueTypeNames.Double)
        with patch("usd_optimize.validators.redundant_timesamples_checker.analysis.optimize") as mock_optimize:
            rule._remove_redundant_timesamples(stage, attr)
        args = self._first_op_args(mock_optimize)
        self.assertEqual(args["epsilonD"], 1e-6)
        self.assertEqual(args["epsilonF"], 1e-3)
        self.assertEqual(args["attributePaths"], ["/Foo.bar"])  # target still set

    def test_fit_primitives_fixer_uses_tolerances(self):
        rule = PrimitiveFitChecker(
            parameters=_make_mapping({"VERTEX_TOLERANCE": 0.5, "VOLUME_TOLERANCE": 0.25, "IGNORE_SUBSETS": False})
        )
        stage = Usd.Stage.CreateInMemory()
        with patch("usd_optimize.validators.primitive_fit_checker.analysis.optimize") as mock_optimize:
            rule._fit_primitives("sphere", stage, stage.GetPrimAtPath("/"))
        args = self._first_op_args(mock_optimize)
        self.assertEqual(args["vertexTolerance"], 0.5)
        self.assertEqual(args["volumeTolerance"], 0.25)
        self.assertEqual(args["ignoreSubsets"], False)
        self.assertEqual(args["fitSphere"], True)  # per-suggestion fit target
        self.assertEqual(args["ignoreNonConstPrimvars"], False)


def _per_prim_messages(mock_add_warning, summary_substrings):
    """Return messages emitted that are not one of the aggregate summaries.

    ``summary_substrings`` lists fragments that identify the rule's aggregate
    summary issue(s); everything else is a verbose per-prim row.
    """
    out = []
    for call in mock_add_warning.call_args_list:
        message = call.kwargs.get("message", "")
        if any(sub in message for sub in summary_substrings):
            continue
        out.append(message)
    return out


class TestVerboseReporting(TestCase):
    """Validate the optional verbose per-prim emission plumbing.

    Uses synthetic analysis payloads (with the additive ``*Paths`` keys) and a
    mocked ``_AddWarning`` so the tests don't depend on real assets or the exact
    prim paths inside them; the C++ path collection is covered separately.
    """

    def tearDown(self):
        # Never leak the global toggle into other tests.
        set_verbose(False)

    def _run_check(self, rule, analysis_data):
        rule._AddWarning = MagicMock()
        rule._AddInfo = MagicMock()
        rule.suggested_operations = []
        stage = Usd.Stage.CreateInMemory()
        rule._CheckStage(stage, analysis_data)
        return rule._AddWarning

    def test_verbose_param_advertised_as_bool(self):
        defs = {d.display_name: d for d in NonManifoldChecker.get_parameter_definitions()}
        self.assertIn("VERBOSE", defs)
        self.assertIn("UsdOptimizeNonManifoldChecker.VERBOSE", defs)
        self.assertEqual(defs["VERBOSE"].type, ParameterType.BOOL)

    def test_disabled_by_default_emits_only_summary(self):
        rule = NonManifoldChecker()
        add_warning = self._run_check(
            rule,
            {"meshesThatAreNonManifolds": 2, "meshesThatAreNonManifoldsPaths": ["/A", "/B"]},
        )
        per_prim = _per_prim_messages(add_warning, ["nonManifold mesh"])
        self.assertEqual(per_prim, [])

    def test_class_flag_enables_per_prim(self):
        rule = NonManifoldChecker()
        rule.VERBOSE = True
        add_warning = self._run_check(
            rule,
            {"meshesThatAreNonManifolds": 2, "meshesThatAreNonManifoldsPaths": ["/A", "/B"]},
        )
        per_prim = _per_prim_messages(add_warning, ["nonManifold mesh"])
        self.assertEqual(per_prim, ["NonManifold mesh found", "NonManifold mesh found"])

    def test_engine_parameter_enables_per_prim(self):
        # No class-level flag set; the engine parameter alone drives verbosity.
        rule = NonManifoldChecker(parameters=_make_mapping({"VERBOSE": True}))
        add_warning = self._run_check(
            rule,
            {"meshesThatAreNonManifolds": 1, "meshesThatAreNonManifoldsPaths": ["/A"]},
        )
        per_prim = _per_prim_messages(add_warning, ["nonManifold mesh"])
        self.assertEqual(per_prim, ["NonManifold mesh found"])

    def test_qualified_engine_parameter_enables_per_prim(self):
        rule = NonManifoldChecker(parameters=_make_mapping({"UsdOptimizeNonManifoldChecker.VERBOSE": True}))
        add_warning = self._run_check(
            rule,
            {"meshesThatAreNonManifolds": 1, "meshesThatAreNonManifoldsPaths": ["/A"]},
        )
        self.assertEqual(_per_prim_messages(add_warning, ["nonManifold mesh"]), ["NonManifold mesh found"])

    def test_qualified_parameter_overrides_unqualified(self):
        # Global VERBOSE=False but per-rule override True: qualified form wins,
        # matching _effective_args precedence.
        rule = NonManifoldChecker(
            parameters=_make_mapping({"VERBOSE": False, "UsdOptimizeNonManifoldChecker.VERBOSE": True})
        )
        add_warning = self._run_check(
            rule,
            {"meshesThatAreNonManifolds": 1, "meshesThatAreNonManifoldsPaths": ["/A"]},
        )
        self.assertEqual(_per_prim_messages(add_warning, ["nonManifold mesh"]), ["NonManifold mesh found"])

    def test_qualified_parameter_can_disable_when_global_enabled(self):
        # The reverse: global VERBOSE=True, per-rule override False -> off.
        rule = NonManifoldChecker(
            parameters=_make_mapping({"VERBOSE": True, "UsdOptimizeNonManifoldChecker.VERBOSE": False})
        )
        add_warning = self._run_check(
            rule,
            {"meshesThatAreNonManifolds": 1, "meshesThatAreNonManifoldsPaths": ["/A"]},
        )
        self.assertEqual(_per_prim_messages(add_warning, ["nonManifold mesh"]), [])

    def test_occluded_meshes_verbose_lists_each_prim(self):
        rule = OccludedMeshesChecker()
        rule.VERBOSE = True
        add_warning = self._run_check(rule, {"occludedMeshes": ["/A", "/B", "/C"]})
        per_prim = _per_prim_messages(add_warning, ["occluded mesh"])
        self.assertEqual(per_prim, ["Occluded mesh found"] * 3)

    def test_empty_leaf_verbose_lists_each_prim(self):
        rule = EmptyLeafChecker()
        rule.VERBOSE = True
        add_warning = self._run_check(rule, ["/A", "/B"])
        per_prim = _per_prim_messages(add_warning, ["empty leaf"])
        self.assertEqual(per_prim, ["Empty leaf primitive found"] * 2)

    def test_normals_verbose_lists_each_prim(self):
        rule = NormalsChecker()
        rule.VERBOSE = True
        add_warning = self._run_check(
            rule,
            {"totalNonUnitLengthStrict": 2, "nonUnitLengthStrictPaths": ["/A", "/B"]},
        )
        per_prim = _per_prim_messages(add_warning, ["meshes with normals", "mesh with normals"])
        self.assertEqual(per_prim, ["Mesh with normals that are not of length 1 found"] * 2)

    def test_primitive_fit_verbose_lists_each_prim(self):
        rule = PrimitiveFitChecker()
        rule.VERBOSE = True
        analysis_data = {
            "totalMeshCount": 2,
            "composedCount": 0,
            "totalFaceCount": 20,
            "totalVertexCount": 16,
            "primitives": {
                "sphere": {
                    "meshCount": 2,
                    "faceCount": 20,
                    "vertexCount": 16,
                    "nonconstPrimvarMeshCount": 0,
                    "nonconstPrimvarFaceCount": 0,
                    "nonconstPrimvarVertexCount": 0,
                    "meshPaths": ["/A", "/B"],
                    "nonconstPrimvarMeshPaths": [],
                },
            },
        }
        add_warning = self._run_check(rule, analysis_data)
        per_prim = _per_prim_messages(add_warning, ["Found "])
        self.assertEqual(per_prim, ["Mesh can be replaced by a sphere GPrim"] * 2)

    def test_set_verbose_toggles_class_flag(self):
        # Establish a known baseline so the test is independent of any ambient
        # env-driven (USD_OPTIMIZE_VALIDATOR_VERBOSE) or leaked process state.
        set_verbose(False)
        self.assertFalse(BaseUsdOptimizeChecker.VERBOSE)
        set_verbose(True)
        self.assertTrue(BaseUsdOptimizeChecker.VERBOSE)
        set_verbose(False)
        self.assertFalse(BaseUsdOptimizeChecker.VERBOSE)

    def test_injected_default_param_does_not_mask_class_flag(self):
        # Simulate the engine injecting the advertised VERBOSE default spec
        # (an _AssetValidatorParameter) into the mapping. It must NOT be treated
        # as a user override, so a runtime set_verbose()/class flag still wins.
        mapping = ParameterMapping([_AssetValidatorParameter("VERBOSE", ParameterType.BOOL, False)])
        rule = NonManifoldChecker(parameters=mapping)
        rule.VERBOSE = True
        add_warning = self._run_check(
            rule,
            {"meshesThatAreNonManifolds": 1, "meshesThatAreNonManifoldsPaths": ["/A"]},
        )
        self.assertEqual(_per_prim_messages(add_warning, ["nonManifold mesh"]), ["NonManifold mesh found"])
