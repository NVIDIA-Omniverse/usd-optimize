# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Gf, Usd, UsdGeom, UsdPhysics

from .test_utils import Test_Operation, _get_context


class Test_Operation_PhysicsBodyVisibility(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _merge(self, stage, **args):
        """Execute successfully and return the merge report."""
        _, result = self._execute_command(args, _get_context(stage))
        self.assertTrue(result[0], result[1])
        return result[2]

    def _open_guide_colliders(self):
        """Open two guide colliders with a complete body mass override and guide display enabled."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
        body = stage.GetPrimAtPath("/MeshVisibility/Body")
        mass = UsdPhysics.MassAPI.Apply(body)
        mass.CreateMassAttr(10)
        mass.CreateCenterOfMassAttr(Gf.Vec3f(0))
        mass.CreateDiagonalInertiaAttr(Gf.Vec3f(1))
        UsdGeom.VisibilityAPI.Apply(body).CreateGuideVisibilityAttr("visible")
        for name in ("A", "B"):
            UsdGeom.Imageable(stage.GetPrimAtPath("/MeshVisibility/Body/Parts/" + name)).CreatePurposeAttr("guide")
        return stage

    def test_merged_colliders_keep_guide_visibility(self):
        # Matching collider visibility survives merging, including visibility inherited below the output parent.
        """Keep directly hidden, ancestor-hidden, and visible guide collider groups in the same display state."""
        for hidden_at in ("self", "parent", "none"):
            with self.subTest(hidden_at=hidden_at):
                stage = self._open_guide_colliders()
                paths = ["/MeshVisibility/Body/Parts/" + name for name in ("A", "B")]
                if hidden_at == "parent":
                    UsdGeom.Imageable(stage.GetPrimAtPath("/MeshVisibility/Body/Parts")).CreateVisibilityAttr(
                        "invisible"
                    )
                elif hidden_at == "self":
                    for path in paths:
                        UsdGeom.Imageable(stage.GetPrimAtPath(path)).CreateVisibilityAttr("invisible")
                expected = "visible" if hidden_at == "none" else "invisible"

                report = self._merge(stage, paths=["/MeshVisibility"], mergeColliders=True)

                self.assertEqual(len(report["merged"]), 1)
                merged = report["merged"][0]
                self.assertEqual(merged["role"], "collision")
                self.assertCountEqual(merged["sources"], paths)
                output = UsdGeom.Imageable(stage.GetPrimAtPath(merged["output"]))
                self.assertEqual(output.ComputePurpose(), "guide")
                self.assertEqual(output.ComputeEffectiveVisibility("guide"), expected)

    def test_colliders_with_different_visibility_stay_separate(self):
        # Combining hidden and visible guide colliders must not reveal or hide either source's geometry.
        """Keep collider groups separate when their guide visibility differs."""
        stage = self._open_guide_colliders()
        UsdGeom.Imageable(stage.GetPrimAtPath("/MeshVisibility/Body/Parts/A")).CreateVisibilityAttr("invisible")
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, paths=["/MeshVisibility"], mergeColliders=True)

        self.assertEqual(report["merged"], [])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    def test_static_hidden_colliders_do_not_create_visuals(self):
        # Static hidden colliders remain unchanged when only visual merging is requested.
        """Preserve hidden mesh and analytic colliders without creating visual copies."""
        cases = (
            ("Mesh", "default", "self"),
            ("Mesh", "proxy", "aboveBody"),
            ("Cylinder", "proxy", "self"),
            ("Cylinder", "default", "parent"),
        )
        for geometry, purpose, hidden_at in cases:
            with self.subTest(geometry=geometry, purpose=purpose, hidden_at=hidden_at):
                stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
                root = "/" + geometry + "Visibility"
                asset = UsdGeom.Imageable(stage.GetPrimAtPath(root))
                parent = UsdGeom.Imageable(stage.GetPrimAtPath(root + "/Body/Parts"))
                if hidden_at == "aboveBody":
                    asset.CreateVisibilityAttr("invisible")
                elif hidden_at == "parent":
                    parent.CreateVisibilityAttr("invisible")
                for name in ("A", "B"):
                    prim = stage.GetPrimAtPath(root + "/Body/Parts/" + name)
                    imageable = UsdGeom.Imageable(prim)
                    imageable.CreatePurposeAttr(purpose)
                    if hidden_at == "self":
                        imageable.CreateVisibilityAttr("invisible")
                before = stage.GetRootLayer().ExportToString()

                report = self._merge(stage, paths=[root])

                self.assertEqual(report["merged"], [])
                self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    def test_static_hidden_visual_meshes_still_merge(self):
        # Hidden geometry without CollisionAPI remains eligible for visual merging and stays hidden.
        """Merge visual-only meshes with direct or inherited invisible visibility."""
        for inherited in (False, True):
            with self.subTest(inherited=inherited):
                stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
                body = stage.GetPrimAtPath("/VisualPair")
                if inherited:
                    UsdGeom.Imageable(body).CreateVisibilityAttr("invisible")
                paths = ["/VisualPair/A", "/VisualPair/B"]
                for path in paths:
                    prim = stage.GetPrimAtPath(path)
                    if not inherited:
                        UsdGeom.Imageable(prim).CreateVisibilityAttr("invisible")

                report = self._merge(stage, paths=["/VisualPair"])

                self.assertEqual(len(report["merged"]), 1)
                merged = report["merged"][0]
                self.assertEqual(merged["role"], "visual")
                self.assertCountEqual(merged["sources"], paths)
                output = UsdGeom.Imageable(stage.GetPrimAtPath(merged["output"]))
                self.assertEqual(output.ComputeVisibility(), "invisible")
                self.assertEqual(output.ComputeVisibility(1), "invisible")

    def test_sampled_ancestor_visibility_keeps_collider_visuals(self):
        # Colliders that can become visible retain visual output, including with a single visibility sample.
        """Retain visual geometry and its inherited visibility at default and numeric times."""
        for samples in (((0, "invisible"), (1, "inherited")), ((1, "inherited"),)):
            with self.subTest(samples=samples):
                stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
                parent = UsdGeom.Imageable(stage.GetPrimAtPath("/AnimatedColliders/Animated"))
                visibility = parent.GetVisibilityAttr()
                for time, value in samples:
                    visibility.Set(value, time)
                paths = ["/AnimatedColliders/Animated/A", "/AnimatedColliders/Animated/B"]
                times = [Usd.TimeCode.Default(), 0, 1, 2]
                expected = [UsdGeom.Imageable(stage.GetPrimAtPath(paths[0])).ComputeVisibility(time) for time in times]

                report = self._merge(stage, paths=["/AnimatedColliders"])

                self.assertEqual(len(report["merged"]), 1)
                merged = report["merged"][0]
                self.assertEqual(merged["role"], "visual")
                self.assertCountEqual(merged["sources"], paths)
                output = stage.GetPrimAtPath(merged["output"])
                self.assertFalse(output.HasAPI(UsdPhysics.CollisionAPI))
                self.assertEqual([UsdGeom.Imageable(output).ComputeVisibility(time) for time in times], expected)
                for path in paths:
                    self.assertTrue(stage.GetPrimAtPath(path).HasAPI(UsdPhysics.CollisionAPI))

    def test_hidden_colliders_merge_without_hidden_visual_copies(self):
        # Collision merging combines hidden colliders while only animated siblings contribute visual output.
        """Keep collision merging available independently of static visibility."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
        hidden_paths = ["/MixedVisibility/Hidden/A", "/MixedVisibility/Hidden/B"]
        animated_paths = ["/MixedVisibility/Animated/A", "/MixedVisibility/Animated/B"]

        report = self._merge(stage, paths=["/MixedVisibility"], mergeColliders=True)

        visuals = [item for item in report["merged"] if item["role"] == "visual"]
        self.assertEqual(len(visuals), 1)
        self.assertCountEqual(visuals[0]["sources"], animated_paths)
        output = UsdGeom.Imageable(stage.GetPrimAtPath(visuals[0]["output"]))
        self.assertEqual(output.ComputeVisibility(0), "invisible")
        self.assertEqual(output.ComputeVisibility(1), "inherited")
        collisions = [item for item in report["merged"] if item["role"] == "collision"]
        self.assertCountEqual([path for item in collisions for path in item["sources"]], hidden_paths + animated_paths)
        for item in collisions:
            self.assertTrue(stage.GetPrimAtPath(item["output"]).HasAPI(UsdPhysics.CollisionAPI))
