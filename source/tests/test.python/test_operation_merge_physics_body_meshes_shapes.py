# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Usd, UsdGeom, UsdPhysics

from .test_operation_merge_physics_body_meshes import _body, _scope
from .test_utils import Test_Operation, _get_context


class Test_Operation_PhysicsBodyShapeDiscovery(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _discover(self, stage, path):
        """Return candidate discovery without changing the source layer."""
        before = stage.GetRootLayer().ExportToString()
        _, result = self._execute_command({"paths": [path]}, _get_context(stage, analysis=True))
        self.assertTrue(result[0], result[1])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)
        return result[2]["analysis"]

    def test_capsules_are_visual_and_collision_candidates(self):
        # Capsule discovery includes visual geometry and classifies colliders without converting either shape.
        """Include capsules in candidate scopes with their existing collision role."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")

        analysis = self._discover(stage, "/CapsuleRoles")

        self.assertEqual(
            analysis["scopes"],
            [
                _scope(
                    "/CapsuleRoles",
                    "/CapsuleRoles",
                    visual_shapes=["/CapsuleRoles/Visual"],
                    collision_shapes=["/CapsuleRoles/Collider"],
                )
            ],
        )

    def test_unsupported_geometry_is_reported_once(self):
        # Unsupported geometric prims remain unchanged and each gets one reason identifying its type.
        """Report unsupported geometry without treating it as a merge candidate."""
        stage = Usd.Stage.CreateInMemory()
        _body(stage, "/Body")
        schemas = [UsdGeom.Plane, UsdGeom.Points, UsdGeom.BasisCurves]
        for name in ("Cylinder_1", "Capsule_1"):
            schema = getattr(UsdGeom, name, None)
            if schema:
                schemas.append(schema)
        expected = []
        for schema in schemas:
            prim = schema.Define(stage, "/Body/" + schema.__name__).GetPrim()
            expected.append({"path": str(prim.GetPath()), "reason": "unsupported geometry type: " + prim.GetTypeName()})

        analysis = self._discover(stage, "/Body")

        self.assertEqual(analysis["scopes"], [])
        self.assertCountEqual(analysis["skipped"], expected)

    def test_material_hierarchies_do_not_fill_skipped_geometry_report(self):
        # Materials and their containers do not appear as skipped geometry or add geometry candidates.
        """Keep material definitions out of geometry diagnostics."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")

        analysis = self._discover(stage, "/MaterialHierarchy")

        self.assertEqual(
            analysis["scopes"], [_scope("/MaterialHierarchy", "/MaterialHierarchy", "/MaterialHierarchy/Visual")]
        )
        self.assertEqual(analysis["skipped"], [])


class Test_Operation_PhysicsBodyShapeExecution(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def test_capsule_visuals_merge_while_collision_shapes_are_retained(self):
        # Capsule visuals merge into a Mesh while the original analytic colliders retain their dimensions.
        """Merge rendered capsule geometry without replacing its analytic collision shape."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
        sources = ["/CapsulePair/Left", "/CapsulePair/Right"]

        _, result = self._execute_command({"paths": ["/CapsulePair"]}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        self.assertEqual(len(result[2]["merged"]), 1)
        merged = result[2]["merged"][0]
        self.assertEqual(merged["role"], "visual")
        self.assertCountEqual(merged["sources"], sources)
        output = UsdGeom.Mesh(stage.GetPrimAtPath(merged["output"]))
        self.assertTrue(output.GetPointsAttr().Get())
        for path in sources:
            capsule = UsdGeom.Capsule(stage.GetPrimAtPath(path))
            self.assertTrue(capsule)
            self.assertTrue(capsule.GetPrim().HasAPI(UsdPhysics.CollisionAPI))
            self.assertEqual(capsule.GetPurposeAttr().Get(), UsdGeom.Tokens.guide)
            self.assertEqual(capsule.GetRadiusAttr().Get(), 0.5)
            self.assertEqual(capsule.GetHeightAttr().Get(), 2.0)
            self.assertEqual(capsule.GetAxisAttr().Get(), UsdGeom.Tokens.z)
