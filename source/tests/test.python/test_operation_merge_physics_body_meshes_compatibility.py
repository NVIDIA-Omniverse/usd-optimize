# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from collections import Counter

from pxr import Sdf, UsdGeom

from .test_utils import Test_Operation, _get_context


class Test_Operation_MergePhysicsBodyMeshesCompatibility(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _merge(self, stage, path):
        """Merge one fixture body and return its change report."""
        _, result = self._execute_command({"paths": [path]}, _get_context(stage))
        self.assertTrue(result[0], result[1])
        return result[2]

    def test_custom_attribute_types_keep_meshes_separate(self):
        # Equal values with different declared USD types do not make custom attributes compatible.
        """Custom attribute compatibility includes the declared USD type."""
        stage = self._open_stage("mergePhysicsBodyMeshes/compatibility.usda")
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, "/AttributeTypes")

        self.assertEqual(report["merged"], [])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)
        self.assertEqual(
            {item["path"] for item in report["skipped"] if "attributeType:measurement" in item["reason"]},
            {"/AttributeTypes/A", "/AttributeTypes/B"},
        )

    def test_material_subsets_preserve_matching_custom_arrays(self):
        # Material slicing preserves matching opaque custom arrays and their metadata on the merged mesh.
        """Custom arrays survive material subset slicing unchanged."""
        stage = self._open_stage("mergePhysicsBodyMeshes/compatibility.usda")
        source = stage.GetPrimAtPath("/SubsetCustomArrays/A").GetAttribute("inertiaMatrix")
        value = source.Get()
        metadata = source.GetCustomData()

        report = self._merge(stage, "/SubsetCustomArrays")

        self.assertEqual(len(report["merged"]), 1)
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        attribute = output.GetAttribute("inertiaMatrix")
        self.assertEqual(attribute.Get(), value)
        self.assertEqual(attribute.GetCustomData(), metadata)

    def test_unsupported_material_subset_primvars_are_reported(self):
        # Unsupported nonconstant primvars prevent material slicing and leave source geometry unchanged.
        """Report primvar types that the material subset slicer cannot preserve."""
        cases = (
            (Sdf.ValueTypeNames.IntArray, [10, 20]),
            (Sdf.ValueTypeNames.BoolArray, [False, True]),
            (Sdf.ValueTypeNames.TokenArray, ["first", "second"]),
            (Sdf.ValueTypeNames.StringArray, ["first", "second"]),
        )
        for type_name, values in cases:
            with self.subTest(type_name=str(type_name)):
                stage = self._open_stage("mergePhysicsBodyMeshes/compatibility.usda")
                for name in ("A", "B"):
                    prim = stage.GetPrimAtPath("/SubsetPrimvars/" + name)
                    prim.RemoveProperty("primvars:faceId")
                    UsdGeom.PrimvarsAPI(prim).CreatePrimvar("faceId", type_name, UsdGeom.Tokens.uniform).Set(values)
                before = stage.GetRootLayer().ExportToString()

                report = self._merge(stage, "/SubsetPrimvars")

                self.assertEqual(report["merged"], [])
                self.assertEqual(stage.GetRootLayer().ExportToString(), before)
                self.assertEqual(
                    {
                        item["path"]
                        for item in report["skipped"]
                        if "slicing material subsets" in item["reason"] and "primvars:faceId" in item["reason"]
                    },
                    {"/SubsetPrimvars/A", "/SubsetPrimvars/B"},
                )

    def test_constant_integer_primvars_allow_material_slicing(self):
        # A constant integer primvar keeps the same value on every material subset.
        """Constant integer primvars can be preserved without slicing their values."""
        stage = self._open_stage("mergePhysicsBodyMeshes/compatibility.usda")
        for name in ("A", "B"):
            primvar = UsdGeom.PrimvarsAPI(stage.GetPrimAtPath("/SubsetPrimvars/" + name)).GetPrimvar("faceId")
            primvar.SetInterpolation(UsdGeom.Tokens.constant)
            primvar.Set([10])

        report = self._merge(stage, "/SubsetPrimvars")

        self.assertEqual(len(report["merged"]), 1)
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        self.assertEqual(set(UsdGeom.PrimvarsAPI(output).GetPrimvar("faceId").ComputeFlattened()), {10})

    def test_integer_primvars_without_material_subsets_merge(self):
        # Per-face integer primvars retain every value when no material slicing is needed.
        """Meshes without material subsets retain supported integer primvars."""
        stage = self._open_stage("mergePhysicsBodyMeshes/compatibility.usda")

        report = self._merge(stage, "/WholeMeshPrimvars")

        self.assertEqual(len(report["merged"]), 1)
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        values = UsdGeom.PrimvarsAPI(output).GetPrimvar("faceId").ComputeFlattened()
        self.assertEqual(Counter(values), Counter([10, 20, 10, 20]))
