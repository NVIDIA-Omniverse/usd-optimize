# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Sdf, Ts, UsdGeom, UsdPhysics

from .test_operation_merge_physics_body_meshes_execution import _mesh, _world_points
from .test_utils import Test_Operation, _get_context


class Test_Operation_MergePhysicsBodyMeshesInstances(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _merge(self, stage, path):
        """Make selected instances local and return the successful merge report."""
        _, result = self._execute_command({"paths": [path], "instanceMode": 1}, _get_context(stage))
        self.assertTrue(result[0], result[1])
        return result[2]

    def test_sources_in_child_instance_are_replaced(self):
        # Sources below a child instance are replaced even when the merged output is outside that instance.
        """Localize child instances used by visual source replacement."""
        for intermediate in (False, True):
            for collision in (False, True):
                with self.subTest(intermediate=intermediate, collision=collision):
                    asset = self._open_stage("mergePhysicsBodyMeshes/instance_sources.usda")
                    stage = self._open_stage("mergePhysicsBodyMeshes/instances.usda")
                    root = "/" + ("Nested" if intermediate else "Direct") + ("Colliders" if collision else "Visuals")
                    visuals = stage.GetPrimAtPath(root + "/link/visuals")
                    source_parent = root + "/link/visuals" + ("/Parts" if intermediate else "")
                    sources = [stage.GetPrimAtPath(source_parent + "/" + name) for name in ("A", "B")]
                    before_points = _world_points(sources)
                    asset_before = asset.GetRootLayer().ExportToString()

                    report = self._merge(stage, root)

                    self.assertEqual(len(report["merged"]), 1)
                    output = stage.GetPrimAtPath(report["merged"][0]["output"])
                    self.assertEqual(output.GetParent().GetPath(), Sdf.Path(root + "/link"))
                    self.assertEqual(_world_points([output]), before_points)
                    self.assertFalse(visuals.IsInstance())
                    self.assertEqual(asset.GetRootLayer().ExportToString(), asset_before)
                    for name in ("A", "B"):
                        source = stage.GetPrimAtPath(source_parent + "/" + name)
                        if collision:
                            self.assertTrue(source.HasAPI(UsdPhysics.CollisionAPI))
                            self.assertEqual(UsdGeom.Imageable(source).ComputePurpose(), UsdGeom.Tokens.guide)
                        else:
                            self.assertFalse(source.IsActive())

    def test_localization_does_not_activate_ignored_opinions(self):
        # An instance stays unchanged if localization would activate ignored descendant content.
        """Preserve instancing when otherwise ignored content would become effective."""
        for change in ("value", "sample", "spline", "metadata", "relationship", "connection", "schema", "child"):
            with self.subTest(change=change):
                stage = self._open_stage("mergePhysicsBodyMeshes/instances.usda")
                visuals = stage.GetPrimAtPath("/IgnoredOpinions/Visuals")
                visuals.SetInstanceable(False)
                collider = stage.GetPrimAtPath("/IgnoredOpinions/Visuals/Collider")
                if change == "value":
                    UsdGeom.Cylinder(collider).GetAxisAttr().Set("X")
                elif change == "sample":
                    UsdGeom.Cylinder(collider).GetRadiusAttr().Set(3, 1)
                elif change == "spline":
                    spline = Ts.Spline()
                    knot = Ts.Knot()
                    knot.SetTime(1)
                    knot.SetValue(3)
                    spline.SetKnot(knot)
                    UsdGeom.Cylinder(collider).GetRadiusAttr().SetSpline(spline)
                elif change == "metadata":
                    collider.SetDisplayName("Ignored name")
                elif change == "relationship":
                    collider.CreateRelationship("tracking").SetTargets(["/Target"])
                elif change == "connection":
                    collider.CreateAttribute("input", Sdf.ValueTypeNames.Float).SetConnections(["/Target.output"])
                elif change == "schema":
                    UsdPhysics.MassAPI.Apply(collider).CreateMassAttr(3)
                else:
                    _mesh(stage, "/IgnoredOpinions/Visuals/IgnoredMesh", 4)
                visuals.SetInstanceable(True)
                before = stage.GetRootLayer().ExportToString()

                report = self._merge(stage, "/IgnoredOpinions")

                self.assertEqual(report["merged"], [])
                self.assertTrue(visuals.IsInstance())
                self.assertEqual(stage.GetRootLayer().ExportToString(), before)
                self.assertEqual(
                    UsdGeom.Cylinder(stage.GetPrimAtPath("/IgnoredOpinions/Visuals/Collider")).GetAxisAttr().Get(), "Z"
                )
                self.assertTrue(
                    any(
                        item["path"] == "/IgnoredOpinions/Visuals"
                        and "localizing the instance changes composed content" in item["reason"]
                        for item in report["skipped"]
                    )
                )

    def test_unsafe_nested_instance_does_not_block_sibling_merge(self):
        # A safe outer instance can merge siblings while an unsafe nested instance stays unchanged.
        """Keep an unsafe nested instance while merging safe sibling geometry."""
        asset = self._open_stage("mergePhysicsBodyMeshes/instance_sources.usda")
        stage = self._open_stage("mergePhysicsBodyMeshes/instances.usda")
        outer = stage.GetPrimAtPath("/UnsafeNested")
        definitions_before = asset.GetRootLayer().ExportToString()

        report = self._merge(stage, "/UnsafeNested")

        self.assertEqual(len(report["merged"]), 1)
        self.assertCountEqual(report["merged"][0]["sources"], ["/UnsafeNested/SafeA", "/UnsafeNested/SafeB"])
        self.assertFalse(outer.IsInstance())
        self.assertTrue(stage.GetPrimAtPath("/UnsafeNested/Nested").IsInstance())
        self.assertEqual(
            UsdGeom.Cylinder(stage.GetPrimAtPath("/UnsafeNested/Nested/Collider")).GetAxisAttr().Get(), "Z"
        )
        self.assertEqual(asset.GetRootLayer().ExportToString(), definitions_before)
