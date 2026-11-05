# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import math

from pxr import Gf, UsdPhysics

from .test_operation_merge_physics_body_meshes_execution import _mass
from .test_utils import Test_Operation, _get_context


def _collision_mass_information(prim):
    """Supply fixed unit-volume geometry data to expose USD's explicit-inertia selection."""
    result = UsdPhysics.RigidBodyAPI.MassInformation()
    result.volume = 1.0
    result.inertia = Gf.Matrix3f(0.2)
    result.centerOfMass = Gf.Vec3f(0)
    result.localPos = Gf.Vec3f(0)
    result.localRot = Gf.Quatf(1)
    return result


def _guide_colliders(stage):
    """Return the fixture's collider-only meshes with coincident mass frames."""
    return [stage.GetPrimAtPath("/MassColliders/" + name) for name in ("A", "B")]


def _inertia_threshold_cases():
    """Return positive inertias below, at, and above USD's float tolerance boundary."""
    return (
        ("below", Gf.Vec3f(4e-6), False),
        ("boundary", Gf.Vec3f(1e-5, 1e-20, 1e-20), False),
        ("above", Gf.Vec3f(6e-6), True),
    )


class Test_Operation_MergePhysicsBodyMeshesMass(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _merge(self, stage):
        """Merge collider geometry and return the successful operation report."""
        _, result = self._execute_command({"paths": ["/MassColliders"], "mergeColliders": True}, _get_context(stage))
        self.assertTrue(result[0], result[1])
        return result[2]

    def _assert_mass_unchanged(self, before, after):
        """Compare computed mass, center, and principal inertias without an absolute floor for small inertias."""
        self.assertAlmostEqual(before[0], after[0])
        self.assertTrue(Gf.IsClose(before[2], after[2], 1e-6))
        for expected, actual in zip(sorted(before[1]), sorted(after[1])):
            self.assertTrue(math.isclose(expected, actual, rel_tol=1e-5), (expected, actual))

    def test_implicit_collider_inertia_is_not_aggregated(self):
        # Inputs at or below USD's inertia tolerance stay unchanged; larger explicit inertias can merge.
        """Apply USD's inertia threshold before combining collider mass properties."""
        for label, inertia, merge_expected in _inertia_threshold_cases():
            with self.subTest(inertia=label):
                stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
                body = stage.GetPrimAtPath("/MassColliders")
                for collider in _guide_colliders(stage):
                    _mass(collider, inertia=inertia).CreatePrincipalAxesAttr(Gf.Quatf(1))
                before_layer = stage.GetRootLayer().ExportToString()
                before_mass = UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(_collision_mass_information)

                report = self._merge(stage)

                self.assertEqual(len(report["merged"]), int(merge_expected))
                if not merge_expected:
                    self.assertEqual(stage.GetRootLayer().ExportToString(), before_layer)
                    self.assertTrue(any("implicit-inertia tolerance" in item["reason"] for item in report["skipped"]))
                after_mass = UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(_collision_mass_information)
                self._assert_mass_unchanged(before_mass, after_mass)

    def test_body_override_uses_the_same_inertia_threshold(self):
        # A body inertia USD ignores cannot authorize merging colliders with unresolved mass properties.
        """Require an effective body inertia before bypassing collider mass aggregation."""
        for label, inertia, merge_expected in _inertia_threshold_cases():
            with self.subTest(inertia=label):
                stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
                body = stage.GetPrimAtPath("/MassColliders")
                _mass(body, mass=2, inertia=inertia).CreatePrincipalAxesAttr(Gf.Quatf(1))
                before_layer = stage.GetRootLayer().ExportToString()
                before_mass = UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(_collision_mass_information)

                report = self._merge(stage)

                self.assertEqual(len(report["merged"]), int(merge_expected))
                if merge_expected:
                    self.assertEqual(report["merged"][0]["massSource"], "bodyOverride")
                else:
                    self.assertEqual(stage.GetRootLayer().ExportToString(), before_layer)
                after_mass = UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(_collision_mass_information)
                self._assert_mass_unchanged(before_mass, after_mass)

    def test_ignored_body_inertia_uses_explicit_collider_values(self):
        # An ignored body inertia still permits merging when complete collider mass properties are available.
        """Aggregate effective collider inertia when the body's authored inertia is ignored."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
        body = stage.GetPrimAtPath("/MassColliders")
        _mass(body, mass=2, inertia=Gf.Vec3f(4e-6)).CreatePrincipalAxesAttr(Gf.Quatf(1))
        for collider in _guide_colliders(stage):
            _mass(collider, inertia=Gf.Vec3f(0.1)).CreatePrincipalAxesAttr(Gf.Quatf(1))
        before_mass = UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(_collision_mass_information)

        report = self._merge(stage)

        self.assertEqual(len(report["merged"]), 1)
        self.assertEqual(report["merged"][0]["massSource"], "colliders")
        self._assert_mass_unchanged(
            before_mass, UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(_collision_mass_information)
        )
