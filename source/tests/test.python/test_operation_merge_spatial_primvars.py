# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Gf, Sdf, UsdGeom, UsdPhysics

from .test_utils import Test_Operation, _execute_operation, _get_context


class Test_Operation_MergeSpatialPrimvars(Test_Operation):
    """Check spatial primvar preservation through both merge operations."""

    def _merge(self, stage, operation):
        """Merge the fixture under its existing parent and return the single output mesh."""
        if operation == "merge":
            args = {
                "mergePoint": 8,
                "considerMaterials": False,
                "materialAlbedoAsVertexColors": False,
                "originalGeomOption": 1,
            }
        else:
            UsdPhysics.RigidBodyAPI.Apply(stage.GetPrimAtPath("/World"))
            args = {"paths": ["/World"]}
        _, result = _execute_operation(operation, args, _get_context(stage))
        self.assertTrue(result[0], result[1])
        meshes = [prim for prim in stage.Traverse() if prim.IsA(UsdGeom.Mesh)]
        self.assertEqual(len(meshes), 1)
        return meshes[0]

    def _assert_primvar(self, prim, name, type_name, expected):
        """Compare declared type and flattened values without depending on source order in the merged mesh."""
        primvar = UsdGeom.PrimvarsAPI(prim).GetPrimvar(name)
        self.assertEqual(primvar.GetTypeName(), type_name)
        actual_values = [tuple(round(component, 5) for component in value) for value in primvar.ComputeFlattened()]
        expected_values = [tuple(round(component, 5) for component in value) for value in expected]
        self.assertCountEqual(actual_values, expected_values)

    def test_merge_preserves_spatial_primvar_roles(self):
        # Both merge operations retain points, indexed vectors, and normals in the output parent's coordinate frame.
        """Transform spatial values while preserving their declared roles and leaving colors and triples unchanged."""
        for operation in ("merge", "mergePhysicsBodyMeshes"):
            with self.subTest(operation=operation):
                stage = self._open_stage("mergeSpatialPrimvars.usda")

                output = self._merge(stage, operation)

                self._assert_primvar(
                    output,
                    "rest",
                    Sdf.ValueTypeNames.Point3fArray,
                    [(1, 1, 0), (0, 0, 0), (1, 0, 0), (9, 2, 0), (10, 0, 0), (10, 2, 0)],
                )
                self._assert_primvar(
                    output,
                    "tangents",
                    Sdf.ValueTypeNames.Vector3fArray,
                    [(1, 1, 0), (1, 0, 0), (1, 1, 0), (-1, 2, 0), (0, 2, 0), (-1, 2, 0)],
                )
                normals = [Gf.Vec3f(1, 1, 0).GetNormalized(), Gf.Vec3f(-1, 0.5, 0).GetNormalized()]
                self._assert_primvar(output, "surfaceNormal", Sdf.ValueTypeNames.Normal3fArray, normals)
                self._assert_primvar(output, "normals", Sdf.ValueTypeNames.Normal3fArray, normals * 3)
                self._assert_primvar(output, "displayColor", Sdf.ValueTypeNames.Color3fArray, [(0.2, 0.4, 0.6)])
                self._assert_primvar(output, "customTriple", Sdf.ValueTypeNames.Float3Array, [(1, 1, 0)])

    def test_untyped_tangent_name_does_not_imply_a_transform(self):
        # A tangent-like name does not change the meaning of an untyped float3 array.
        """Keep untyped tangent values unchanged instead of inferring a vector role from their name."""
        stage = self._open_stage("mergeSpatialPrimvars.usda")
        for name in ("Original", "Transformed"):
            prim = stage.GetPrimAtPath("/World/" + name)
            # Keep the existing [0, 1, 0] indices while replacing the value attribute's type.
            prim.RemoveProperty("primvars:tangents")
            UsdGeom.PrimvarsAPI(prim).CreatePrimvar("tangents", Sdf.ValueTypeNames.Float3Array, "vertex").Set(
                [(1, 1, 0), (1, 0, 0)]
            )

        output = self._merge(stage, "merge")

        self._assert_primvar(output, "tangents", Sdf.ValueTypeNames.Float3Array, [(1, 1, 0), (1, 0, 0), (1, 1, 0)] * 2)
