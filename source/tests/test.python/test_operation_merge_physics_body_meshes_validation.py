# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Sdf, UsdGeom

from .test_utils import Test_Operation, _get_context


class Test_Operation_PhysicsBodyMeshValidation(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _assert_valid_siblings_merge(self, stage, body_path, guarded, reason):
        """Require a specific candidate exclusion while the fixture's two valid siblings still merge."""
        _, result = self._execute_command({"paths": [body_path]}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        self.assertEqual(len(result[2]["merged"]), 1)
        self.assertCountEqual(result[2]["merged"][0]["sources"], [body_path + "/GoodA", body_path + "/GoodB"])
        self.assertTrue(guarded.IsActive())
        self.assertTrue(
            any(item["path"] == str(guarded.GetPath()) and reason in item["reason"] for item in result[2]["skipped"]),
            result[2]["skipped"],
        )

    def test_malformed_inherited_primvars_do_not_block_other_merges(self):
        # An invalid inherited index excludes the affected shape or mesh while valid sibling meshes still merge.
        """Handle malformed inherited primvars consistently for analytic shapes and polygon meshes."""
        for kind in ("Cube", "Sphere", "Mesh"):
            with self.subTest(kind=kind):
                stage = self._open_stage("mergePhysicsBodyMeshes/validation.usda")
                parent = stage.GetPrimAtPath("/InheritedPrimvar/Malformed")
                for child in parent.GetChildren():
                    child.SetActive(child.GetName() == kind)
                guarded = stage.GetPrimAtPath("/InheritedPrimvar/Malformed/" + kind)
                primvar = UsdGeom.PrimvarsAPI(parent).GetPrimvar("tint")

                self._assert_valid_siblings_merge(stage, "/InheritedPrimvar", guarded, "invalid indexed primvar")

                self.assertEqual(guarded.GetTypeName(), kind)
                self.assertEqual(list(primvar.Get()), [1])
                self.assertEqual(list(primvar.GetIndices()), [2])

    def test_valid_inherited_indexed_primvars_allow_shape_merging(self):
        # Valid inherited indices allow analytic shapes to convert and merge without losing their primvar value.
        """Preserve a valid indexed constant when Cube and Sphere visuals merge."""
        stage = self._open_stage("mergePhysicsBodyMeshes/validation.usda")
        parent = stage.GetPrimAtPath("/InheritedPrimvar/Malformed")
        UsdGeom.PrimvarsAPI(parent).GetPrimvar("tint").SetIndices([0])
        stage.GetPrimAtPath("/InheritedPrimvar/Malformed/Mesh").SetActive(False)
        shapes = {"/InheritedPrimvar/Malformed/Cube", "/InheritedPrimvar/Malformed/Sphere"}

        _, result = self._execute_command({"paths": ["/InheritedPrimvar"]}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        outputs = [item["output"] for item in result[2]["merged"] if set(item["sources"]) == shapes]
        self.assertEqual(len(outputs), 1)
        primvar = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath(outputs[0])).GetPrimvar("tint")
        self.assertEqual(list(primvar.ComputeFlattened()), [1])

    def test_unusable_relative_transforms_are_reported(self):
        # Reflected, singular, and non-finite relative transforms leave affected geometry outside valid merge groups.
        """Preserve unusable transforms while merging independent valid geometry."""
        cases = (
            ("reflected", (-1, 1, 1), "singular or reflected transform"),
            ("singular", (0, 1, 1), "singular or reflected transform"),
            ("nan", (float("nan"), 1, 1), "non-finite transform"),
            ("infinity", (float("inf"), 1, 1), "non-finite transform"),
        )
        for name, value, reason in cases:
            with self.subTest(transform=name):
                stage = self._open_stage("mergePhysicsBodyMeshes/validation.usda")
                guarded = stage.GetPrimAtPath("/GuardedGeometry/Guarded")
                scale = UsdGeom.Xformable(guarded).AddScaleOp()
                scale.Set(value)
                before = str(scale.Get())

                self._assert_valid_siblings_merge(stage, "/GuardedGeometry", guarded, reason)

                self.assertEqual(str(scale.Get()), before)

    def test_grouped_primvar_elements_are_reported(self):
        # A primvar with elementSize greater than one remains intact and does not prevent other meshes from merging.
        """Report unsupported primvar element sizes without changing their values."""
        stage = self._open_stage("mergePhysicsBodyMeshes/validation.usda")
        guarded = stage.GetPrimAtPath("/GuardedGeometry/Guarded")
        primvar = UsdGeom.PrimvarsAPI(guarded).CreatePrimvar("weights", Sdf.ValueTypeNames.FloatArray, "constant", 2)
        primvar.Set([1, 2])

        self._assert_valid_siblings_merge(stage, "/GuardedGeometry", guarded, "primvar element size is not supported")

        self.assertEqual(primvar.GetElementSize(), 2)
        self.assertEqual(list(primvar.Get()), [1, 2])

    def test_motion_vectors_are_reported(self):
        # Meshes with velocities or accelerations remain intact while independent geometry can still merge.
        """Keep motion vectors on their original geometry until a transform policy exists."""
        for name in ("velocities", "accelerations"):
            with self.subTest(attribute=name):
                stage = self._open_stage("mergePhysicsBodyMeshes/validation.usda")
                guarded = stage.GetPrimAtPath("/GuardedGeometry/Guarded")
                attribute = guarded.CreateAttribute(name, Sdf.ValueTypeNames.Vector3fArray)
                attribute.Set([(1, 0, 0)] * 3)
                before = attribute.Get()

                self._assert_valid_siblings_merge(stage, "/GuardedGeometry", guarded, "mesh motion vectors")

                self.assertEqual(attribute.Get(), before)

    def test_deformed_geometry_is_reported(self):
        # Skinned, deformable, and particle meshes stay intact instead of being merged as static geometry.
        """Keep applied deformation schemas and their source geometry unchanged."""
        for schema in ("SkelBindingAPI", "PhysxDeformableBodyAPI", "PhysxParticleSetAPI"):
            with self.subTest(schema=schema):
                stage = self._open_stage("mergePhysicsBodyMeshes/validation.usda")
                guarded = stage.GetPrimAtPath("/GuardedGeometry/Guarded")
                guarded.AddAppliedSchema(schema)

                self._assert_valid_siblings_merge(stage, "/GuardedGeometry", guarded, "deformed geometry")

                self.assertIn(schema, guarded.GetMetadata("apiSchemas").GetAppliedItems())
