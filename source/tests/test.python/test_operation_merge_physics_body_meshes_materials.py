# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Sdf, Usd, UsdPhysics, UsdShade

from .test_utils import Test_Operation, _get_context


def _parsed_materials(stage, path):
    """Return the physics parser's material paths for mesh colliders under the selected path."""
    parse = getattr(UsdPhysics, "UsdPhysicsLoadStageFromPrimRange", None)
    if parse is None:
        parse = UsdPhysics.LoadUsdPhysicsFromRange
    parsed = parse(stage, [Sdf.Path(path)])
    _, shapes = parsed[UsdPhysics.ObjectType.MeshShape]
    return {shape.primPath: set(shape.materials) for shape in shapes}


class Test_Operation_MergePhysicsBodyMeshesMaterials(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _merge_colliders(self, stage, path):
        """Merge the fixture's colliders and return its single collision output."""
        _, result = self._execute_command({"paths": [path], "mergeColliders": True}, _get_context(stage))
        self.assertTrue(result[0], result[1])
        outputs = [item["output"] for item in result[2]["merged"] if item["role"] == "collision"]
        self.assertEqual(len(outputs), 1)
        return Sdf.Path(outputs[0])

    def test_physics_bindings_require_applied_apis(self):
        # Merging preserves the parser's material assignment for inherited, collection, and direct bindings.
        """Physics bindings require MaterialBindingAPI and a physics material."""
        for binding in ("inherited", "strong_inherited", "collection", "direct"):
            for binding_api in (False, True):
                for physics_api in (False, True):
                    with self.subTest(binding=binding, binding_api=binding_api, physics_api=physics_api):
                        stage = self._open_stage("mergePhysicsBodyMeshes/materials.usda")
                        body = stage.GetPrimAtPath("/BindingAPIs")
                        material = UsdShade.Material(stage.GetPrimAtPath("/BindingAPIs/Material"))
                        if physics_api:
                            UsdPhysics.MaterialAPI.Apply(material.GetPrim())
                        colliders = []
                        for name in ("A", "B"):
                            prim = stage.GetPrimAtPath("/BindingAPIs/" + name)
                            if binding_api:
                                UsdShade.MaterialBindingAPI.Apply(prim)
                            if binding == "direct":
                                prim.CreateRelationship("material:binding:physics").SetTargets([material.GetPath()])
                            colliders.append(prim)
                        if binding in ("inherited", "strong_inherited"):
                            strength = (
                                "strongerThanDescendants" if binding == "strong_inherited" else "weakerThanDescendants"
                            )
                            UsdShade.MaterialBindingAPI.Apply(body).Bind(
                                material, bindingStrength=strength, materialPurpose="physics"
                            )
                        elif binding == "collection":
                            collection = Usd.CollectionAPI.Apply(body, "Colliders")
                            collection.CreateIncludesRel().SetTargets([prim.GetPath() for prim in colliders])
                            UsdShade.MaterialBindingAPI.Apply(body).Bind(
                                collection, material, "Colliders", materialPurpose="physics"
                            )
                        expected = {material.GetPath()} if binding_api and physics_api else set()
                        self.assertEqual(
                            _parsed_materials(stage, "/BindingAPIs"), {prim.GetPath(): expected for prim in colliders}
                        )

                        output = self._merge_colliders(stage, "/BindingAPIs")

                        self.assertEqual(_parsed_materials(stage, "/BindingAPIs"), {output: expected})

    def test_unbound_physics_subset_keeps_collider_material(self):
        # A subset without MaterialBindingAPI cannot replace the collider's valid physics material during merging.
        """An ignored subset binding keeps the collider's base physics material."""
        stage = self._open_stage("mergePhysicsBodyMeshes/materials.usda")
        expected = {Sdf.Path("/IgnoredSubset/Material")}
        colliders = [Sdf.Path("/IgnoredSubset/" + name) for name in ("A", "B")]
        self.assertEqual(_parsed_materials(stage, "/IgnoredSubset"), {path: expected for path in colliders})

        output = self._merge_colliders(stage, "/IgnoredSubset")

        self.assertEqual(_parsed_materials(stage, "/IgnoredSubset"), {output: expected})

    def test_subset_physics_binding_is_independent_of_collider_api(self):
        # A valid subset physics binding survives even when the collider has no MaterialBindingAPI.
        """Subset physics bindings are resolved independently from the collider's binding API."""
        stage = self._open_stage("mergePhysicsBodyMeshes/materials.usda")
        expected = {Sdf.Path("/SubsetBinding/Material")}
        colliders = [Sdf.Path("/SubsetBinding/" + name) for name in ("A", "B")]
        self.assertEqual(_parsed_materials(stage, "/SubsetBinding"), {path: expected for path in colliders})

        output = self._merge_colliders(stage, "/SubsetBinding")

        self.assertEqual(_parsed_materials(stage, "/SubsetBinding"), {output: expected})

    def test_render_material_subsets_do_not_split_collision_groups(self):
        # Different render-only subset materials stay on the visual output and do not prevent collider merging.
        """Render subset materials do not become physics compatibility values."""
        stage = self._open_stage("mergePhysicsBodyMeshes/materials.usda")
        materials = [Sdf.Path("/RenderSubsets/Materials/" + name) for name in ("Red", "Blue")]
        self.assertEqual(
            _parsed_materials(stage, "/RenderSubsets"),
            {Sdf.Path("/RenderSubsets/" + name): set() for name in ("Red", "Blue")},
        )

        _, result = self._execute_command({"paths": ["/RenderSubsets"], "mergeColliders": True}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        collisions = [item for item in result[2]["merged"] if item["role"] == "collision"]
        visuals = [item for item in result[2]["merged"] if item["role"] == "visual"]
        self.assertEqual(len(collisions), 1)
        self.assertEqual(len(visuals), 1)
        self.assertEqual(_parsed_materials(stage, "/RenderSubsets"), {Sdf.Path(collisions[0]["output"]): set()})
        output = stage.GetPrimAtPath(visuals[0]["output"])
        bindings = [
            UsdShade.MaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial()[0].GetPath()
            for subset in UsdShade.MaterialBindingAPI(output).GetMaterialBindSubsets()
        ]
        self.assertCountEqual(bindings, materials)
