# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Gf, Sdf, UsdGeom, UsdPhysics, UsdShade

from .test_utils import Test_Operation, _get_context


class Test_Operation_PhysicsBodyMeshReporting(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def test_custom_attribute_differences_are_preserved_and_explained(self):
        # Different custom values keep visual meshes apart and the report names the differing attribute.
        """Explain custom attribute incompatibility for visuals, including visible collider geometry."""
        for collider in (False, True):
            for attribute in ("isaac:nameOverride", "inertiaMatrix"):
                with self.subTest(collider=collider, attribute=attribute):
                    stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
                    for index, name in enumerate(("A", "B")):
                        prim = stage.GetPrimAtPath("/VisualPair/" + name)
                        if attribute == "inertiaMatrix":
                            prim.CreateAttribute(attribute, Sdf.ValueTypeNames.Matrix3d).Set(Gf.Matrix3d(index + 1))
                        else:
                            prim.CreateAttribute(attribute, Sdf.ValueTypeNames.String).Set(name)
                        if collider:
                            UsdPhysics.CollisionAPI.Apply(prim)
                    before = stage.GetRootLayer().ExportToString()

                    _, result = self._execute_command({"paths": ["/VisualPair"]}, _get_context(stage))

                    self.assertTrue(result[0], result[1])
                    self.assertEqual(result[2]["merged"], [])
                    self.assertEqual(stage.GetRootLayer().ExportToString(), before)
                    for path in ("/VisualPair/A", "/VisualPair/B"):
                        reasons = [entry["reason"] for entry in result[2]["skipped"] if entry["path"] == path]
                        self.assertTrue(any("visual" in reason and attribute in reason for reason in reasons), reasons)

    def test_unmatched_visible_collider_reports_its_visual_role(self):
        # A lone visible collider reports why its visual geometry stayed separate even with collision merging disabled.
        """Keep visual non-merges visible in diagnostics for geometry with both roles."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")

        _, result = self._execute_command({"paths": ["/LoneCollider"]}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        reasons = [entry["reason"] for entry in result[2]["skipped"] if entry["path"] == "/LoneCollider/OnlyGeometry"]
        self.assertTrue(any("visual" in reason for reason in reasons), reasons)

    def test_matching_custom_attributes_do_not_prevent_merging(self):
        # Equal custom values can merge and remain available on the output.
        """Preserve matching custom attributes while merging compatible visual geometry."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")

        _, result = self._execute_command({"paths": ["/MatchingAttributes"]}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        self.assertEqual(len(result[2]["merged"]), 1)
        self.assertEqual(result[2]["merged"][0]["vertexCount"], 6)
        self.assertEqual(result[2]["merged"][0]["faceCount"], 2)
        output = stage.GetPrimAtPath(result[2]["merged"][0]["output"])
        self.assertEqual(output.GetAttribute("asset:category").Get(), "bodywork")

    def test_subset_and_collider_counts_describe_each_output(self):
        # Counts describe each authored visual or collider mesh, and a body override has no aggregated mass payload.
        """Report geometry counts independently of material subsets and the retained body mass override."""
        stage = self._open_stage("mergePhysicsBodyMeshes/materials.usda")

        _, result = self._execute_command({"paths": ["/RenderSubsets"], "mergeColliders": True}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        self.assertCountEqual([item["role"] for item in result[2]["merged"]], ["visual", "collision"])
        for item in result[2]["merged"]:
            with self.subTest(role=item["role"]):
                output = UsdGeom.Mesh(stage.GetPrimAtPath(item["output"]))
                self.assertEqual(item["vertexCount"], len(output.GetPointsAttr().Get()))
                self.assertEqual(item["faceCount"], len(output.GetFaceVertexCountsAttr().Get()))
                self.assertEqual(item["faceCount"], 2)
                if item["role"] == "visual":
                    self.assertEqual(len(UsdShade.MaterialBindingAPI(output).GetMaterialBindSubsets()), 2)
                else:
                    self.assertEqual(item["massSource"], "bodyOverride")
                    self.assertNotIn("mass", item)

    def test_aggregated_mass_report_matches_authored_properties(self):
        # Aggregated mass values match the output collider, with principal axes serialized in real-first order.
        """Keep the reported mass, center, inertia, and quaternion consistent with the authored collider."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")

        _, result = self._execute_command({"paths": ["/RotatedInertia"], "mergeColliders": True}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        collisions = [item for item in result[2]["merged"] if item["role"] == "collision"]
        self.assertEqual(len(collisions), 1)
        item = collisions[0]
        self.assertEqual(item["vertexCount"], 6)
        self.assertEqual(item["faceCount"], 2)
        self.assertEqual(item["massSource"], "colliders")
        mass = UsdPhysics.MassAPI(stage.GetPrimAtPath(item["output"]))
        payload = item["mass"]
        self.assertAlmostEqual(payload["mass"], mass.GetMassAttr().Get(), places=6)
        axes = mass.GetPrincipalAxesAttr().Get()
        expected_vectors = {
            "centerOfMass": mass.GetCenterOfMassAttr().Get(),
            "diagonalInertia": mass.GetDiagonalInertiaAttr().Get(),
            "principalAxes": [axes.GetReal(), *axes.GetImaginary()],
        }
        for name, expected in expected_vectors.items():
            with self.subTest(field=name):
                self.assertEqual(len(payload[name]), len(expected))
                for actual, value in zip(payload[name], expected):
                    self.assertAlmostEqual(actual, value, places=6)
