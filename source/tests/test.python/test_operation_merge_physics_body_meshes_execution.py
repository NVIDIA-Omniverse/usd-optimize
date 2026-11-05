# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from collections import Counter

from pxr import Gf, Sdf, Usd, UsdGeom, UsdPhysics, UsdShade

from .test_utils import Test_Operation, _get_context


def _body(stage, path):
    """Define a rigid body for a merge scenario."""
    prim = UsdGeom.Xform.Define(stage, path).GetPrim()
    UsdPhysics.RigidBodyAPI.Apply(prim)
    return prim


def _mesh(stage, path, offset=0):
    """Define a static triangle with an optional point offset."""
    mesh = UsdGeom.Mesh.Define(stage, path)
    mesh.CreatePointsAttr([(offset, 0, 0), (offset + 1, 0, 0), (offset, 1, 0)])
    mesh.CreateFaceVertexCountsAttr([3])
    mesh.CreateFaceVertexIndicesAttr([0, 1, 2])
    mesh.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
    return mesh.GetPrim()


def _world_points(prims, time=Usd.TimeCode.Default()):
    """Count world-space mesh points at the requested time."""
    cache = UsdGeom.XformCache(time)
    return Counter(
        tuple(round(value, 5) for value in cache.GetLocalToWorldTransform(prim).Transform(point))
        for prim in prims
        for point in UsdGeom.Mesh(prim).GetPointsAttr().Get(time)
    )


def _mass(prim, mass=1.0, center=(0, 0, 0), inertia=(1, 1, 1)):
    """Author complete explicit mass properties for a collider scenario."""
    api = UsdPhysics.MassAPI.Apply(prim)
    api.CreateMassAttr(mass)
    api.CreateCenterOfMassAttr(center)
    api.CreateDiagonalInertiaAttr(inertia)
    return api


def _collider(stage, path, translation=(0, 0, 0)):
    """Define a translated triangle collider with explicit mass properties."""
    prim = _mesh(stage, path)
    UsdPhysics.CollisionAPI.Apply(prim)
    _mass(prim)
    UsdGeom.Xformable(prim).AddTranslateOp().Set(translation)
    return prim


class Test_Operation_MergePhysicsBodyMeshesExecution(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _merge(self, stage, **args):
        """Execute the operation successfully and return its change report."""
        _, result = self._execute_command(args, _get_context(stage))
        self.assertTrue(result[0], result[1])
        return result[2]

    def test_invalid_traversal_starts_leave_geometry_unchanged(self):
        # A non-empty list with no valid traversal starts leaves existing mergeable geometry unchanged.
        """A non-empty list with no valid traversal starts leaves existing mergeable geometry unchanged."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, paths=["/CompatibleMeshes/Missing", "/CompatibleMeshes/AlsoMissing"])

        self.assertEqual(stage.GetRootLayer().ExportToString(), before)
        self.assertEqual(report["merged"], [])

    def test_instance_preservation_and_selected_local_copy(self):
        # Make Local changes only selected instances that contain a successful merge.
        """Make Local changes only selected instances that contain a successful merge."""
        asset = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        asset_before = asset.GetRootLayer().ExportToString()
        stage = self._open_stage("mergePhysicsBodyMeshes/composition.usda")
        before = stage.GetRootLayer().ExportToString()

        self.assertEqual(self._merge(stage, paths=["/SelectedInstances"])["merged"], [])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)
        report = self._merge(stage, paths=["/SelectedInstances/Selected"], instanceMode=1)

        self.assertEqual(len(report["merged"]), 1)
        self.assertFalse(stage.GetPrimAtPath("/SelectedInstances/Selected").IsInstance())
        self.assertTrue(stage.GetPrimAtPath("/SelectedInstances/Other").IsInstance())
        self.assertEqual(asset.GetRootLayer().ExportToString(), asset_before)
        self.assertTrue(stage.GetPrimAtPath("/SelectedInstances/Other/A").IsInstanceProxy())

    def test_make_local_preserves_instance_without_merge(self):
        # Make Local leaves a selected instance unchanged when it has no mergeable pair.
        """Make Local leaves a selected instance unchanged when it has no mergeable pair."""
        stage = self._open_stage("mergePhysicsBodyMeshes/composition.usda")
        instance = stage.GetPrimAtPath("/SingleInstance/Selected")
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, paths=["/SingleInstance/Selected"], instanceMode=1)

        self.assertEqual(report["merged"], [])
        self.assertTrue(instance.IsInstance())
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    def test_selection_inside_instance_does_not_localize_ancestor(self):
        # Selecting an instance proxy cannot change an ancestor outside the requested hierarchy.
        """Selecting an instance proxy cannot change an ancestor outside the requested hierarchy."""
        stage = self._open_stage("mergePhysicsBodyMeshes/composition.usda")
        instance = stage.GetPrimAtPath("/InsideInstance/Instance")
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, paths=["/InsideInstance/Instance/Parts"], instanceMode=1)

        self.assertEqual(report["merged"], [])
        self.assertTrue(instance.IsInstance())
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    def test_reference_sources_are_replaced_in_current_layer(self):
        # Root-layer overrides replace referenced geometry without editing or duplicating its source layer.
        """Root-layer overrides replace referenced geometry without editing or duplicating its source layer."""
        asset = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        before = asset.GetRootLayer().ExportToString()
        stage = self._open_stage("mergePhysicsBodyMeshes/composition.usda")

        points = _world_points([stage.GetPrimAtPath("/ReferencedGeometry/Robot/" + name) for name in ("A", "B")])

        report = self._merge(stage, paths=["/ReferencedGeometry"])

        self.assertEqual(asset.GetRootLayer().ExportToString(), before)
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        self.assertEqual(_world_points([output]), points)
        self.assertEqual(
            len([prim for prim in Usd.PrimRange(stage.GetPrimAtPath("/ReferencedGeometry")) if prim.IsA(UsdGeom.Mesh)]),
            1,
        )
        subsets = UsdShade.MaterialBindingAPI(output).GetMaterialBindSubsets()
        self.assertCountEqual(
            [UsdShade.MaterialBindingAPI(subset).ComputeBoundMaterial()[0].GetPath() for subset in subsets],
            [Sdf.Path("/ReferencedGeometry/Robot/Materials/" + name) for name in ("A", "B")],
        )

    def test_mapped_reference_edit_target_maps_material_relationships(self):
        # An explicitly selected reference-layer edit target authors paths in that layer's namespace.
        """An explicitly selected reference-layer edit target authors paths in that layer's namespace."""
        asset = Usd.Stage.CreateInMemory()
        _body(asset, "/Asset")
        for name in ("A", "B"):
            prim = _mesh(asset, "/Asset/" + name)
            material = UsdShade.Material.Define(asset, "/Asset/Materials/" + name)
            UsdShade.MaterialBindingAPI.Apply(prim).Bind(material)
        stage = Usd.Stage.CreateInMemory()
        robot = stage.DefinePrim("/Robot")
        robot.GetReferences().AddReference(asset.GetRootLayer().identifier, "/Asset")
        node = robot.GetPrimIndex().rootNode.children[0]
        stage.SetEditTarget(Usd.EditTarget(asset.GetRootLayer(), node))

        report = self._merge(stage)

        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        self.assertTrue(asset.GetPrimAtPath("/Asset/MergedVisuals"))
        for subset in UsdShade.MaterialBindingAPI(output).GetMaterialBindSubsets():
            self.assertTrue(UsdShade.MaterialBindingAPI(subset).ComputeBoundMaterial()[0])

    def test_unloaded_payload_stays_unloaded(self):
        # Unloaded payloads are reported and remain unloaded while loaded bodies can merge.
        """Unloaded payloads are reported and remain unloaded while loaded bodies can merge."""
        stage = self._open_stage("mergePhysicsBodyMeshes/composition.usda")
        stage.Unload("/PayloadLoading/Unloaded")

        load_set = stage.GetLoadSet()

        report = self._merge(stage, paths=["/PayloadLoading"])

        self.assertEqual(len(report["merged"]), 1)
        self.assertEqual(stage.GetLoadSet(), load_set)
        self.assertTrue(
            any(
                item["path"] == "/PayloadLoading/Unloaded" and "unloaded" in item["reason"]
                for item in report["skipped"]
            )
        )

    def test_shape_visuals_merge_without_converting_colliders(self):
        # Analytic collider shapes stay analytic while their visual geometry can merge.
        """Analytic collider shapes stay analytic while their visual geometry can merge."""
        stage = Usd.Stage.CreateInMemory()
        _body(stage, "/Body")
        for name, schema in (
            ("Cube", UsdGeom.Cube),
            ("Sphere", UsdGeom.Sphere),
            ("Cylinder", UsdGeom.Cylinder),
            ("Cone", UsdGeom.Cone),
        ):
            prim = schema.Define(stage, "/Body/" + name).GetPrim()
            UsdPhysics.CollisionAPI.Apply(prim)
            UsdGeom.Xformable(prim).AddTranslateOp().Set((len(name), 0, 0))

        report = self._merge(stage, mergeColliders=True)

        self.assertTrue(report["merged"])
        self.assertTrue(all(item["role"] == "visual" for item in report["merged"]))
        for name, type_name in (("Cube", "Cube"), ("Sphere", "Sphere"), ("Cylinder", "Cylinder"), ("Cone", "Cone")):
            prim = stage.GetPrimAtPath("/Body/" + name)
            self.assertEqual(prim.GetTypeName(), type_name)
            self.assertTrue(prim.HasAPI(UsdPhysics.CollisionAPI))

    def test_normals_and_uv_generation_are_controlled(self):
        # Missing normals and UVs permit merging only when the corresponding preparation is enabled.
        """Missing normals and UVs permit merging only when the corresponding preparation is enabled."""
        for attribute, option in (("normals", "generateNormals"), ("st", "generateUVs")):
            for enabled in (False, True):
                with self.subTest(attribute=attribute, enabled=enabled):
                    stage = Usd.Stage.CreateInMemory()
                    _body(stage, "/Body")
                    a = _mesh(stage, "/Body/A")
                    _mesh(stage, "/Body/B", 2)
                    if attribute == "normals":
                        UsdGeom.Mesh(a).CreateNormalsAttr([(0, 0, 1)] * 3)
                        UsdGeom.Mesh(a).SetNormalsInterpolation("vertex")
                    else:
                        UsdGeom.PrimvarsAPI(a).CreatePrimvar("st", Sdf.ValueTypeNames.TexCoord2fArray, "vertex").Set(
                            [(0, 0), (1, 0), (0, 1)]
                        )

                    report = self._merge(stage, **{option: enabled})

                    self.assertEqual(len(report["merged"]), 1 if enabled else 0)
                    if enabled:
                        output = stage.GetPrimAtPath(report["merged"][0]["output"])
                        self.assertTrue(UsdGeom.PrimvarsAPI(output).GetPrimvar(attribute).HasValue())

    def test_inherited_and_indexed_primvars_survive(self):
        # Inherited constants and indexed vertex values retain their values after moving to the body.
        """Inherited constants and indexed vertex values retain their values after moving to the body."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        report = self._merge(stage, paths=["/IndexedPrimvars"])

        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        primvars = UsdGeom.PrimvarsAPI(output)
        self.assertEqual(primvars.GetPrimvar("label").GetInterpolation(), "constant")
        self.assertEqual(list(primvars.GetPrimvar("label").ComputeFlattened()), ["part"])
        self.assertEqual(
            list(primvars.GetPrimvar("st").ComputeFlattened()), [Gf.Vec2f(0), Gf.Vec2f(1), Gf.Vec2f(0)] * 2
        )

    def test_invalid_primvar_defaults_fail_before_editing(self):
        # Invalid default syntax, reserved names, and unusable numeric values cannot partially edit the stage.
        """Invalid default syntax, reserved names, and unusable numeric values cannot partially edit the stage."""
        for default in ("broken", "st=0,0", "invalid name=1", "weight=nan", "weight=abc"):
            with self.subTest(default=default):
                stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
                before = stage.GetRootLayer().ExportToString()

                _, result = self._execute_command(
                    {"paths": ["/InvalidPrimvarDefaults"], "primvarDefaults": [default]}, _get_context(stage)
                )

                self.assertFalse(result[0])
                self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    def test_primvar_defaults_only_validate_visual_geometry(self):
        # Collider-only primvars cannot reject visual defaults; visible colliders still validate them.
        """Validate supplied defaults only against geometry that contributes visuals."""
        for role in ("guide", "invisible", "visible"):
            with self.subTest(role=role):
                stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
                collider_path = "/MissingPrimvars/Body/Collider"
                Sdf.CopySpec(stage.GetRootLayer(), "/MissingPrimvars/Body/A", stage.GetRootLayer(), collider_path)
                collider = stage.GetPrimAtPath(collider_path)
                UsdPhysics.CollisionAPI.Apply(collider)
                collider.RemoveProperty("primvars:tint")
                tint = UsdGeom.PrimvarsAPI(collider).CreatePrimvar("tint", Sdf.ValueTypeNames.FloatArray, "constant")
                tint.Set([1])
                if role == "guide":
                    UsdGeom.Imageable(collider).CreatePurposeAttr("guide")
                elif role == "invisible":
                    UsdGeom.Imageable(collider).CreateVisibilityAttr("invisible")
                before = stage.GetRootLayer().ExportToString()

                _, result = self._execute_command(
                    {"paths": ["/MissingPrimvars"], "primvarDefaults": ["tint=0,1,0"]}, _get_context(stage)
                )

                if role == "visible":
                    self.assertFalse(result[0])
                    self.assertIn(collider_path + ".primvars:tint", result[1])
                    self.assertEqual(stage.GetRootLayer().ExportToString(), before)
                else:
                    self.assertTrue(result[0], result[1])
                    self.assertEqual(len(result[2]["merged"]), 1)
                    self.assertEqual(
                        set(result[2]["merged"][0]["sources"]), {"/MissingPrimvars/Body/A", "/MissingPrimvars/Body/B"}
                    )
                    self.assertTrue(collider.IsActive())
                    self.assertEqual(list(tint.Get()), [1])

    def test_scalar_primvars_leave_inputs_unchanged(self):
        # Local and inherited scalar primvars keep their meshes unchanged with a clear skip reason.
        """Retain scalar primvar data that the geometry merger cannot preserve."""
        for inherited in (False, True):
            with self.subTest(inherited=inherited):
                stage = Usd.Stage.CreateInMemory()
                body = _body(stage, "/Body")
                prims = [_mesh(stage, "/Body/A"), _mesh(stage, "/Body/B", 2)]
                for prim in [body] if inherited else prims:
                    UsdGeom.PrimvarsAPI(prim).CreatePrimvar("weight", Sdf.ValueTypeNames.Float, "constant").Set(1.0)
                before = stage.GetRootLayer().ExportToString()

                report = self._merge(stage)

                self.assertEqual(stage.GetRootLayer().ExportToString(), before)
                for prim in prims:
                    self.assertTrue(
                        any(
                            item["path"] == str(prim.GetPath())
                            and "scalar primvar cannot be preserved" in item["reason"]
                            for item in report["skipped"]
                        )
                    )

    def test_invalid_mesh_data_does_not_prevent_other_merges(self):
        # Invalid topology, primvar sizes, and subset faces are left unchanged with specific reasons.
        """Invalid topology, primvar sizes, and subset faces are left unchanged with specific reasons."""
        for invalid, reason in (
            ("topology", "empty or invalid topology"),
            ("primvar", "primvar size does not match its interpolation"),
            ("subset", "material subset indices are invalid or overlap"),
        ):
            with self.subTest(invalid=invalid):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                _mesh(stage, "/Body/A")
                _mesh(stage, "/Body/B")
                bad = _mesh(stage, "/Body/Invalid")
                if invalid == "topology":
                    UsdGeom.Mesh(bad).GetFaceVertexIndicesAttr().Set([0, 1, 9])
                elif invalid == "primvar":
                    UsdGeom.PrimvarsAPI(bad).CreatePrimvar("st", Sdf.ValueTypeNames.TexCoord2fArray, "vertex").Set(
                        [(0, 0)]
                    )
                else:
                    subset = UsdShade.MaterialBindingAPI.Apply(bad).CreateMaterialBindSubset("Faces", [9])
                    material = UsdShade.Material.Define(stage, "/Material")
                    UsdShade.MaterialBindingAPI.Apply(subset.GetPrim()).Bind(material)

                report = self._merge(stage)

                self.assertEqual(len(report["merged"]), 1)
                self.assertTrue(bad.IsActive())
                self.assertTrue(
                    any(item["path"] == "/Body/Invalid" and reason in item["reason"] for item in report["skipped"])
                )

    def test_read_only_edit_target_failure_leaves_layers_unchanged(self):
        # A read-only edit target fails without changing any layer.
        """A read-only edit target fails without changing any layer."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        layer = stage.GetRootLayer()
        before = layer.ExportToString()
        layer.SetPermissionToEdit(False)
        try:
            _, result = self._execute_command({"paths": ["/CompatibleMeshes"]}, _get_context(stage))
        finally:
            layer.SetPermissionToEdit(True)

        self.assertFalse(result[0])
        self.assertIn("not editable", result[1])
        self.assertEqual(layer.ExportToString(), before)

    def test_nested_bodies_keep_independent_outputs(self):
        # Nested rigid bodies merge independently even without a transform-stack reset.
        """Nested rigid bodies merge independently even without a transform-stack reset."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        report = self._merge(stage, paths=["/NestedBodies"])

        self.assertCountEqual(
            [item["rigidBody"] for item in report["merged"]], ["/NestedBodies/Body", "/NestedBodies/Body/Nested"]
        )

    def test_visibility_animation_remains_inherited_from_output_parent(self):
        # Merged geometry inherits its retained parent's animation without exposing locally hidden geometry.
        """Merged geometry inherits its retained parent's animation without exposing locally hidden geometry."""
        for mesh_parent in (False, True):
            with self.subTest(mesh_parent=mesh_parent):
                stage = Usd.Stage.CreateInMemory()
                parent = _mesh(stage, "/Body") if mesh_parent else UsdGeom.Xform.Define(stage, "/Body").GetPrim()
                UsdPhysics.RigidBodyAPI.Apply(parent)
                visibility = UsdGeom.Imageable(parent).CreateVisibilityAttr("invisible")
                visibility.Set("invisible", 0)
                visibility.Set("inherited", 1)
                a = _mesh(stage, "/Body/A")
                b = _mesh(stage, "/Body/B", 2)
                hidden = _mesh(stage, "/Body/Hidden", 4)
                UsdGeom.Imageable(hidden).CreateVisibilityAttr("invisible")
                visible_points = _world_points([a, b] + ([parent] if mesh_parent else []))

                report = self._merge(stage)

                self.assertEqual(len(report["merged"]), 1)
                output = stage.GetPrimAtPath(report["merged"][0]["output"])
                self.assertEqual(UsdGeom.Imageable(output).ComputeVisibility(0), "invisible")
                self.assertEqual(UsdGeom.Imageable(output).ComputeVisibility(1), "inherited")
                self.assertEqual(_world_points([output]), visible_points)
                self.assertTrue(hidden.IsActive())
                self.assertEqual(UsdGeom.Imageable(hidden).ComputeVisibility(1), "invisible")
                visibility.Set("inherited", 0)
                self.assertEqual(UsdGeom.Imageable(output).ComputeVisibility(0), "inherited")

    def test_single_sample_transform_and_visibility_bound_output(self):
        # A single time sample still needs a retained boundary because default-time evaluation can differ.
        """A single time sample still needs a retained boundary because default-time evaluation can differ."""
        for sampled_property in ("transform", "visibility"):
            with self.subTest(sampled_property=sampled_property):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                parent = UsdGeom.Xform.Define(stage, "/Body/Scope").GetPrim()
                if sampled_property == "transform":
                    UsdGeom.Xformable(parent).AddTranslateOp().Set((3, 0, 0), 0)
                else:
                    UsdGeom.Imageable(parent).CreateVisibilityAttr().Set("invisible", 0)
                a = _mesh(stage, "/Body/Scope/A")
                b = _mesh(stage, "/Body/Scope/B", 2)
                points = _world_points([a, b], 0)
                visibility = UsdGeom.Imageable(a).ComputeVisibility(0)

                report = self._merge(stage)

                self.assertEqual(report["merged"][0]["outputParent"], "/Body/Scope")
                output = stage.GetPrimAtPath(report["merged"][0]["output"])
                self.assertEqual(_world_points([output], 0), points)
                self.assertEqual(UsdGeom.Imageable(output).ComputeVisibility(0), visibility)

    def test_single_sample_geometry_is_left_unchanged(self):
        # Geometry authored at even one numeric time remains intact while other compatible meshes merge.
        """Geometry authored at even one numeric time remains intact while other compatible meshes merge."""
        for property_name in ("points", "subset"):
            with self.subTest(property_name=property_name):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                _mesh(stage, "/Body/A")
                _mesh(stage, "/Body/B")
                sampled = _mesh(stage, "/Body/Sampled")
                if property_name == "points":
                    attribute = UsdGeom.Mesh(sampled).GetPointsAttr()
                    attribute.Set([(2, 0, 0), (3, 0, 0), (2, 1, 0)], 0)
                else:
                    material = UsdShade.Material.Define(stage, "/Material")
                    subset = UsdShade.MaterialBindingAPI.Apply(sampled).CreateMaterialBindSubset("Faces", [0])
                    UsdShade.MaterialBindingAPI.Apply(subset.GetPrim()).Bind(material)
                    attribute = subset.GetIndicesAttr()
                    attribute.Set([], 0)
                before = attribute.Get(0)

                report = self._merge(stage)

                self.assertEqual(len(report["merged"]), 1)
                self.assertTrue(sampled.IsActive())
                self.assertEqual(attribute.Get(0), before)
                self.assertTrue(
                    any(
                        item["path"] == "/Body/Sampled" and "time-sampled" in item["reason"]
                        for item in report["skipped"]
                    )
                )

    def test_collection_physics_materials_are_preserved(self):
        # Resolved collection physics bindings survive merging and keep differing materials separate.
        """Resolved collection physics bindings survive merging and keep differing materials separate."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        report = self._merge(stage, paths=["/CollectionBindings"], mergeColliders=True)

        outputs = [stage.GetPrimAtPath(item["output"]) for item in report["merged"] if item["role"] == "collision"]
        self.assertEqual(len(outputs), 2)
        self.assertCountEqual(
            [UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial("physics")[0].GetPath() for prim in outputs],
            [Sdf.Path("/CollectionBindings/Materials/Left"), Sdf.Path("/CollectionBindings/Materials/Right")],
        )

    def test_joint_target_collider_frame_is_preserved(self):
        # A collider targeted by a joint retains its frame while its collision geometry moves to a child.
        """A collider targeted by a joint retains its frame while its collision geometry moves to a child."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        target = stage.GetPrimAtPath("/JointColliderFrame/Body/Target")
        joint = UsdPhysics.FixedJoint(stage.GetPrimAtPath("/JointColliderFrame/Joint"))
        before_matrix = UsdGeom.XformCache().GetLocalToWorldTransform(target)
        collision_points = _world_points([target])

        report = self._merge(stage, paths=["/JointColliderFrame"])

        self.assertEqual(len(report["merged"]), 1)
        self.assertEqual(target.GetTypeName(), "Xform")
        self.assertEqual(UsdGeom.XformCache().GetLocalToWorldTransform(target), before_matrix)
        self.assertEqual(joint.GetBody1Rel().GetTargets(), [Sdf.Path("/JointColliderFrame/Body/Target")])
        self.assertEqual(joint.GetLocalPos1Attr().Get(), Gf.Vec3f(1, 2, 3))
        collider = stage.GetPrimAtPath("/JointColliderFrame/Body/Target/Collision")
        self.assertTrue(collider.HasAPI(UsdPhysics.CollisionAPI))
        self.assertEqual(_world_points([collider]), collision_points)
        self.assertEqual(UsdPhysics.MassAPI(collider).GetMassAttr().Get(), 1)

    def test_retained_frame_still_supplies_unmerged_descendant_primvars(self):
        # Retaining a geometry frame does not remove inherited values from its unmerged descendants.
        """Retaining a geometry frame does not remove inherited values from its unmerged descendants."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        parent = stage.GetPrimAtPath("/RetainedPrimvarFrame/Body")
        unmerged = stage.GetPrimAtPath("/RetainedPrimvarFrame/Body/Unmerged")
        self._merge(stage, paths=["/RetainedPrimvarFrame"])

        self.assertEqual(parent.GetTypeName(), "Xform")
        self.assertTrue(unmerged.IsActive())
        inherited = UsdGeom.PrimvarsAPI(unmerged).FindPrimvarWithInheritance("label")
        self.assertEqual(list(inherited.Get()), ["body"])

    def test_rotated_mass_tensor_and_offset_center(self):
        # Mass aggregation rotates anisotropic tensors and applies parallel-axis offsets in the output frame.
        """Mass aggregation rotates anisotropic tensors and applies parallel-axis offsets in the output frame."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        report = self._merge(stage, paths=["/RotatedInertia"], mergeColliders=True)

        output = next(item for item in report["merged"] if item["role"] == "collision")
        mass = UsdPhysics.MassAPI(stage.GetPrimAtPath(output["output"]))
        self.assertEqual(mass.GetMassAttr().Get(), 2)
        self.assertEqual(mass.GetCenterOfMassAttr().Get(), Gf.Vec3f(1, 0.5, 0))
        diagonal = mass.GetDiagonalInertiaAttr().Get()
        rotation = Gf.Matrix3f(mass.GetPrincipalAxesAttr().Get())
        tensor = (
            rotation.GetTranspose() * Gf.Matrix3f(diagonal[0], 0, 0, 0, diagonal[1], 0, 0, 0, diagonal[2]) * rotation
        )
        expected = Gf.Matrix3f(3.5, -1, 0, -1, 5, 0, 0, 0, 8.5)
        for row in range(3):
            for column in range(3):
                self.assertAlmostEqual(tensor[row][column], expected[row][column], places=4)

    def test_inherited_primvar_block_keeps_output_inside_blocked_scope(self):
        # Geometry under a primvar value block retains its resolved inheritance within that scope.
        """Geometry under a primvar value block retains its resolved inheritance within that scope."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        before = (
            UsdGeom.PrimvarsAPI(stage.GetPrimAtPath("/BlockedPrimvar/Body/Parts/A"))
            .FindPrimvarWithInheritance("label")
            .Get()
        )

        report = self._merge(stage, paths=["/BlockedPrimvar"])

        self.assertEqual(report["merged"][0]["outputParent"], "/BlockedPrimvar/Body/Parts")
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        self.assertEqual(UsdGeom.PrimvarsAPI(output).FindPrimvarWithInheritance("label").Get(), before)

    def test_repeated_collider_merge_is_unchanged(self):
        # Repeating a completed visual and collider merge does not duplicate outputs or change mass.
        """Repeating a completed visual and collider merge does not duplicate outputs or change mass."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        self._merge(stage, paths=["/ColliderPair"], mergeColliders=True)
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, paths=["/ColliderPair"], mergeColliders=True)

        self.assertEqual(report["merged"], [])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    def test_weaker_edit_target_cannot_leave_duplicate_rendered_colliders(self):
        # If a stronger layer prevents replacement, all edits in the selected target are rolled back.
        """If a stronger layer prevents replacement, all edits in the selected target are rolled back."""
        stage = Usd.Stage.CreateInMemory()
        _body(stage, "/Body")
        for name in ("A", "B"):
            prim = _collider(stage, "/Body/" + name)
            UsdGeom.Imageable(prim).CreatePurposeAttr("render")
        weaker = Sdf.Layer.CreateAnonymous()
        stage.GetRootLayer().subLayerPaths.append(weaker.identifier)
        stage.SetEditTarget(weaker)
        before = [layer.ExportToString() for layer in (stage.GetRootLayer(), weaker)]

        _, result = self._execute_command({}, _get_context(stage))

        self.assertFalse(result[0])
        self.assertEqual([layer.ExportToString() for layer in (stage.GetRootLayer(), weaker)], before)

    def test_scaled_centers_follow_usd_explicit_mass_rules(self):
        # Authored centres scale into physical space while authored inertia retains its explicit value.
        """Authored centres scale into physical space while authored inertia retains its explicit value."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        report = self._merge(stage, paths=["/ScaledMass"], mergeColliders=True)

        output = next(item for item in report["merged"] if item["role"] == "collision")
        mass = UsdPhysics.MassAPI(stage.GetPrimAtPath(output["output"]))
        self.assertEqual(mass.GetCenterOfMassAttr().Get(), Gf.Vec3f(2.5, 0, 0))
        self.assertEqual(sorted(mass.GetDiagonalInertiaAttr().Get()), [2, 6.5, 6.5])

    def test_nested_instances_are_localized_only_for_selected_merges(self):
        # Make Local reaches nested instances without editing their referenced definitions.
        """Make Local reaches nested instances without editing their referenced definitions."""
        asset = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        before = asset.GetRootLayer().ExportToString()
        stage = self._open_stage("mergePhysicsBodyMeshes/composition.usda")
        instance = stage.GetPrimAtPath("/NestedInstances/Instance")

        report = self._merge(stage, paths=["/NestedInstances"], instanceMode=1)

        self.assertEqual(len(report["merged"]), 1)
        self.assertFalse(instance.IsInstance())
        self.assertFalse(stage.GetPrimAtPath("/NestedInstances/Instance/Child").IsInstance())
        self.assertEqual(asset.GetRootLayer().ExportToString(), before)

    def test_authored_api_applications_survive_without_schema_plugins(self):
        # Unregistered API applications remain separate when different and survive a compatible merge.
        """Unregistered API applications remain separate when different and survive a compatible merge."""
        for same in (False, True):
            with self.subTest(same=same):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                for name in ("A", "B"):
                    prim = _mesh(stage, "/Body/" + name)
                    schema = "CustomGeometryAPI" if same or name == "A" else "OtherGeometryAPI"
                    prim.SetMetadata("apiSchemas", Sdf.TokenListOp.CreateExplicit([schema]))

                report = self._merge(stage)

                self.assertEqual(len(report["merged"]), 1 if same else 0)
                if same:
                    output = stage.GetPrimAtPath(report["merged"][0]["output"])
                    self.assertIn("CustomGeometryAPI", output.GetMetadata("apiSchemas").GetAppliedItems())

    def test_visual_meshes_merge_with_world_geometry_preserved(self):
        # Compatible meshes merge below their body and retain their world-space geometry.
        """Compatible meshes merge below their body and retain their world-space geometry."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        body = stage.GetPrimAtPath("/TransformedVisuals/Body")
        a = stage.GetPrimAtPath("/TransformedVisuals/Body/Parts/A")
        b = stage.GetPrimAtPath("/TransformedVisuals/Body/Parts/B")
        before = _world_points([a, b])

        report = self._merge(stage, paths=["/TransformedVisuals"])

        self.assertEqual(len(report["merged"]), 1)
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        self.assertEqual(output.GetParent(), body)
        self.assertEqual(_world_points([output]), before)
        self.assertFalse(a.IsActive())
        self.assertFalse(b.IsActive())

    def test_selected_subtrees_do_not_use_ancestor_bodies(self):
        # Selected bodies merge while starts below another body leave that body's geometry unchanged.
        """Selected bodies merge while starts below another body leave that body's geometry unchanged."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        retained_paths = [
            root + "/" + name
            for root in ("/SelectedSubtrees/Body/Left", "/SelectedSubtrees/Body/Right")
            for name in ("A", "B")
        ]
        retained_points = _world_points([stage.GetPrimAtPath(path) for path in retained_paths])

        report = self._merge(
            stage, paths=["/SelectedSubtrees/Body/Left", "/SelectedSubtrees/Body/Right", "/SelectedSubtrees/OtherBody"]
        )

        self.assertEqual([item["outputParent"] for item in report["merged"]], ["/SelectedSubtrees/OtherBody"])
        for path in retained_paths:
            self.assertTrue(stage.GetPrimAtPath(path).IsActive())
        self.assertEqual(_world_points([stage.GetPrimAtPath(path) for path in retained_paths]), retained_points)

    def test_collider_geometry_is_retained_for_visual_merging(self):
        # Visual merging combines rendered geometry while keeping the original colliders and mass properties.
        """Visual merging combines rendered geometry while keeping the original colliders and mass properties."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        colliders = [stage.GetPrimAtPath("/ColliderVisuals/Body/" + name) for name in ("ColliderA", "ColliderB")]
        before = _world_points(colliders)

        report = self._merge(stage, paths=["/ColliderVisuals"])

        self.assertEqual(len(report["merged"]), 1)
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        self.assertFalse(output.HasAPI(UsdPhysics.CollisionAPI))
        self.assertFalse(output.HasAPI(UsdPhysics.MassAPI))
        self.assertEqual(_world_points(colliders), before)
        for collider in colliders:
            self.assertTrue(collider.IsActive())
            self.assertTrue(collider.HasAPI(UsdPhysics.CollisionAPI))
            self.assertEqual(UsdPhysics.MassAPI(collider).GetMassAttr().Get(), 2.0)
            self.assertEqual(UsdGeom.Imageable(collider).ComputePurpose(), UsdGeom.Tokens.guide)

    def test_body_mesh_retains_identity_and_joint_target(self):
        # A Mesh body retains its frame and joint target when its geometry merges with descendant geometry.
        """A Mesh body retains its frame and joint target when its geometry merges with descendant geometry."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        body = stage.GetPrimAtPath("/BodyMeshJoint/Body")
        child = stage.GetPrimAtPath("/BodyMeshJoint/Body/Child")
        joint = UsdPhysics.FixedJoint(stage.GetPrimAtPath("/BodyMeshJoint/Joint"))
        before = _world_points([body, child])

        report = self._merge(stage, paths=["/BodyMeshJoint"])

        self.assertEqual(len(report["merged"]), 1)
        self.assertTrue(body.IsA(UsdGeom.Xform))
        self.assertTrue(body.HasAPI(UsdPhysics.RigidBodyAPI))
        self.assertEqual(joint.GetBody1Rel().GetTargets(), [Sdf.Path("/BodyMeshJoint/Body")])
        self.assertEqual(_world_points([stage.GetPrimAtPath(report["merged"][0]["output"])]), before)

    def test_render_materials_are_retained_as_face_bindings(self):
        # Different render materials can merge through material subsets, or stay separate when requested.
        """Different render materials can merge through material subsets, or stay separate when requested."""
        for separate in (False, True):
            with self.subTest(separate=separate):
                stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
                materials = [Sdf.Path("/RenderMaterials/Materials/" + name) for name in ("A", "B")]
                report = self._merge(stage, paths=["/RenderMaterials"], considerMaterials=separate)

                if separate:
                    self.assertEqual(report["merged"], [])
                else:
                    self.assertEqual(len(report["merged"]), 1)
                    output = stage.GetPrimAtPath(report["merged"][0]["output"])
                    bindings = [
                        UsdShade.MaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial()[0].GetPath()
                        for subset in UsdShade.MaterialBindingAPI(output).GetMaterialBindSubsets()
                    ]
                    self.assertCountEqual(bindings, materials)

    def test_missing_primvars_require_explicit_defaults(self):
        # A missing custom primvar keeps meshes separate unless the caller supplies a value of the existing type.
        """A missing custom primvar keeps meshes separate unless the caller supplies a value of the existing type."""
        for fill in (False, True):
            with self.subTest(fill=fill):
                stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
                report = self._merge(stage, paths=["/MissingPrimvars"], primvarDefaults=["tint=0,1,0"] if fill else [])

                self.assertEqual(len(report["merged"]), 1 if fill else 0)
                if fill:
                    output = stage.GetPrimAtPath(report["merged"][0]["output"])
                    values = UsdGeom.PrimvarsAPI(output).GetPrimvar("tint").ComputeFlattened()
                    self.assertIn(Gf.Vec3f(1, 0, 0), values)
                    self.assertIn(Gf.Vec3f(0, 1, 0), values)

    def test_double_sided_reconciliation_requires_permission(self):
        # Differing double-sided values merge only when the caller enables reconciliation.
        """Differing double-sided values merge only when the caller enables reconciliation."""
        for reconcile in (False, True):
            with self.subTest(reconcile=reconcile):
                stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
                report = self._merge(stage, paths=["/DoubleSided"], makeDoubleSided=reconcile)

                self.assertEqual(len(report["merged"]), 1 if reconcile else 0)
                if reconcile:
                    output = UsdGeom.Mesh(stage.GetPrimAtPath(report["merged"][0]["output"]))
                    self.assertTrue(output.GetDoubleSidedAttr().Get())

    def test_singletons_and_incompatible_geometry_remain_unchanged(self):
        # A singleton or a mesh with unsupported primvars remains unchanged rather than losing its data.
        """A singleton or a mesh with unsupported primvars remains unchanged rather than losing its data."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        b = stage.GetPrimAtPath("/UnsupportedPrimvar/Body/B")
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, paths=["/UnsupportedPrimvar"])

        self.assertEqual(report["merged"], [])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)
        self.assertTrue(b.IsActive())

    def test_colliders_merge_with_mass_and_parallel_axis_inertia(self):
        # Collider merging preserves total mass, centre of mass, and inertia in the retained body frame.
        """Collider merging preserves total mass, centre of mass, and inertia in the retained body frame."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        report = self._merge(stage, paths=["/ParallelAxisMass"], mergeColliders=True)

        collisions = [item for item in report["merged"] if item["role"] == "collision"]
        self.assertEqual(len(collisions), 1)
        output = stage.GetPrimAtPath(collisions[0]["output"])
        mass = UsdPhysics.MassAPI(output)
        self.assertAlmostEqual(mass.GetMassAttr().Get(), 2.0)
        self.assertTrue(Gf.IsClose(mass.GetCenterOfMassAttr().Get(), Gf.Vec3f(0), 1e-5))
        self.assertTrue(Gf.IsClose(mass.GetDiagonalInertiaAttr().Get(), Gf.Vec3f(2, 4, 4), 1e-4))
        self.assertEqual(
            [p for p in Usd.PrimRange(stage.GetPrimAtPath("/ParallelAxisMass")) if p.HasAPI(UsdPhysics.CollisionAPI)],
            [output],
        )
        self.assertEqual(collisions[0]["massSource"], "colliders")

    def test_complete_body_mass_override_is_preserved(self):
        # A complete body override permits merging colliders without adding collider mass or changing the override.
        """A complete body override permits merging colliders without adding collider mass or changing the override."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        body = stage.GetPrimAtPath("/BodyMassOverride/Body")
        before = {name: body.GetAttribute(name).Get() for name in UsdPhysics.MassAPI.GetSchemaAttributeNames()}

        report = self._merge(stage, paths=["/BodyMassOverride"], mergeColliders=True)

        collisions = [item for item in report["merged"] if item["role"] == "collision"]
        self.assertEqual(len(collisions), 1)
        output = stage.GetPrimAtPath(collisions[0]["output"])
        self.assertFalse(output.HasAPI(UsdPhysics.MassAPI))
        self.assertEqual({name: body.GetAttribute(name).Get() for name in before}, before)
        self.assertEqual(collisions[0]["massSource"], "bodyOverride")

    def test_unresolved_mass_keeps_colliders_separate(self):
        # Implicit collider mass is preserved instead of inventing mass or inertia during a collider merge.
        """Implicit collider mass is preserved instead of inventing mass or inertia during a collider merge."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        colliders = [stage.GetPrimAtPath("/MissingColliderMass/Body/" + name) for name in ("A", "B")]
        before = _world_points(colliders)

        report = self._merge(stage, paths=["/MissingColliderMass"], mergeColliders=True)

        self.assertFalse(any(item["role"] == "collision" for item in report["merged"]))
        self.assertEqual(_world_points(colliders), before)
        self.assertTrue(all(prim.HasAPI(UsdPhysics.CollisionAPI) for prim in colliders))
        self.assertTrue(any("explicit mass" in item["reason"] for item in report["skipped"]))

    def test_collision_enabled_state_keeps_colliders_separate(self):
        # Different collision-enabled states keep colliders separate even when their visual geometry merges.
        """Preserve each collider's enabled state when its visual geometry can merge."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        a = stage.GetPrimAtPath("/DisabledCollision/Body/A")
        b = stage.GetPrimAtPath("/DisabledCollision/Body/B")
        report = self._merge(stage, paths=["/DisabledCollision"], mergeColliders=True)

        self.assertFalse(any(item["role"] == "collision" for item in report["merged"]))
        self.assertFalse(UsdPhysics.CollisionAPI(a).GetCollisionEnabledAttr().Get())
        self.assertTrue(UsdPhysics.CollisionAPI(b).GetCollisionEnabledAttr().Get())

    def test_invalid_collider_subset_does_not_prevent_other_merges(self):
        # Invalid collider faces stay unchanged while valid colliders still merge.
        """Skip invalid subset indices without aborting valid collider merges."""
        for face_index in (-1, 9):
            with self.subTest(face_index=face_index):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                _collider(stage, "/Body/ValidA")
                _collider(stage, "/Body/ValidB", (2, 0, 0))
                invalid = _collider(stage, "/Body/Invalid")
                subset = UsdShade.MaterialBindingAPI.Apply(invalid).CreateMaterialBindSubset("Faces", [face_index])
                before = _world_points([invalid])

                report = self._merge(stage, mergeColliders=True)

                collisions = [item for item in report["merged"] if item["role"] == "collision"]
                self.assertEqual(len(collisions), 1)
                self.assertCountEqual(collisions[0]["sources"], ["/Body/ValidA", "/Body/ValidB"])
                self.assertTrue(invalid.HasAPI(UsdPhysics.CollisionAPI))
                self.assertEqual(_world_points([invalid]), before)
                self.assertEqual(list(subset.GetIndicesAttr().Get()), [face_index])
                self.assertTrue(
                    any(
                        item["path"] == "/Body/Invalid" and "material subset indices" in item["reason"]
                        for item in report["skipped"]
                    )
                )

    def test_body_simulation_owner_overrides_collider_owners(self):
        # Authored and default body owners override differing collider-local owners during merging.
        """Use the rigid body's authored or default owner when grouping and authoring colliders."""
        for ownership in ("authored", "unauthored", "empty"):
            with self.subTest(ownership=ownership):
                stage = Usd.Stage.CreateInMemory()
                body = _body(stage, "/Body")
                body_scene = UsdPhysics.Scene.Define(stage, "/BodyScene")
                expected_owners = [body_scene.GetPath()] if ownership == "authored" else []
                if ownership != "unauthored":
                    UsdPhysics.RigidBodyAPI(body).CreateSimulationOwnerRel().SetTargets(expected_owners)
                for index, name in enumerate(("A", "B")):
                    collider = _collider(stage, "/Body/" + name, (index * 2, 0, 0))
                    local_scene = UsdPhysics.Scene.Define(stage, "/LocalScene" + name)
                    UsdPhysics.CollisionAPI(collider).CreateSimulationOwnerRel().SetTargets([local_scene.GetPath()])

                report = self._merge(stage, mergeColliders=True)

                collisions = [item for item in report["merged"] if item["role"] == "collision"]
                self.assertEqual(len(collisions), 1)
                output = stage.GetPrimAtPath(collisions[0]["output"])
                self.assertEqual(UsdPhysics.CollisionAPI(output).GetSimulationOwnerRel().GetTargets(), expected_owners)
                self.assertEqual(UsdPhysics.RigidBodyAPI(body).GetSimulationOwnerRel().GetTargets(), expected_owners)

    def test_schema_defaults_and_explicit_equal_collision_values_match(self):
        # Equal collision behavior merges whether values and the default mesh-collision schema were authored or not.
        """Equal collision behavior merges whether values and the default mesh-collision schema were authored or not."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        report = self._merge(stage, paths=["/SchemaDefaults"], mergeColliders=True)

        self.assertEqual(len([item for item in report["merged"] if item["role"] == "collision"]), 1)

    def test_convex_hull_fallback_requires_explicit_choice(self):
        # Different approximation methods stay separate unless convex-hull fallback is enabled.
        """Different approximation methods stay separate unless convex-hull fallback is enabled."""
        for fallback in (False, True):
            with self.subTest(fallback=fallback):
                stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
                report = self._merge(
                    stage,
                    paths=["/ApproximationMismatch"],
                    mergeColliders=True,
                    collisionApproximationFallback=int(fallback),
                )

                collisions = [item for item in report["merged"] if item["role"] == "collision"]
                self.assertEqual(len(collisions), 1 if fallback else 0)
                if fallback:
                    output = stage.GetPrimAtPath(collisions[0]["output"])
                    self.assertEqual(
                        UsdPhysics.MeshCollisionAPI(output).GetApproximationAttr().Get(), UsdPhysics.Tokens.convexHull
                    )
                    self.assertEqual(collisions[0]["approximation"], "convexHull")

    def test_physx_settings_survive_without_physx_schema_plugins(self):
        # Authored PhysX settings remain on merged colliders and differences keep groups separate.
        """Authored PhysX settings remain on merged colliders and differences keep groups separate."""
        for same in (False, True):
            with self.subTest(same=same):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                for index, name in enumerate(("A", "B")):
                    prim = _collider(stage, "/Body/" + name, (index * 2, 0, 0))
                    prim.AddAppliedSchema("PhysxCollisionAPI")
                    prim.CreateAttribute("physxCollision:contactOffset", Sdf.ValueTypeNames.Float).Set(
                        0.01 if same else 0.01 + index * 0.01
                    )

                report = self._merge(stage, mergeColliders=True)

                collisions = [item for item in report["merged"] if item["role"] == "collision"]
                self.assertEqual(len(collisions), 1 if same else 0)
                if same:
                    output = stage.GetPrimAtPath(collisions[0]["output"])
                    self.assertAlmostEqual(output.GetAttribute("physxCollision:contactOffset").Get(), 0.01)
                    self.assertIn("PhysxCollisionAPI", output.GetMetadata("apiSchemas").GetAppliedItems())

    def test_inherited_physics_materials_always_keep_groups_separate(self):
        # Collider grouping resolves inherited physics materials while visual grouping remains independent.
        """Collider grouping resolves inherited physics materials while visual grouping remains independent."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        report = self._merge(stage, paths=["/PhysicsMaterials"], mergeColliders=True)

        collisions = [item for item in report["merged"] if item["role"] == "collision"]
        self.assertEqual(len(collisions), 2)
        materials = [
            UsdShade.MaterialBindingAPI(stage.GetPrimAtPath(item["output"]))
            .ComputeBoundMaterial("physics")[0]
            .GetPath()
            for item in collisions
        ]
        self.assertCountEqual(
            materials, [Sdf.Path("/PhysicsMaterials/Materials/Left"), Sdf.Path("/PhysicsMaterials/Materials/Right")]
        )

    def test_repeated_visual_merge_does_not_duplicate_retained_colliders(self):
        # Retained guide colliders do not produce another visual copy when the operation is run again.
        """Retained guide colliders do not produce another visual copy when the operation is run again."""
        stage = self._open_stage("mergePhysicsBodyMeshes/colliders.usda")
        self._merge(stage, paths=["/ColliderPair"])
        before = stage.GetRootLayer().ExportToString()

        report = self._merge(stage, paths=["/ColliderPair"])

        self.assertEqual(report["merged"], [])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    def test_full_and_preview_material_bindings_are_preserved(self):
        # Per-purpose material bindings survive a merge as face assignments to the original materials.
        """Per-purpose material bindings survive a merge as face assignments to the original materials."""
        stage = self._open_stage("mergePhysicsBodyMeshes/execution.usda")
        report = self._merge(stage, paths=["/MaterialPurposes"])

        self.assertEqual(len(report["merged"]), 1)
        output = stage.GetPrimAtPath(report["merged"][0]["output"])
        subsets = UsdShade.MaterialBindingAPI(output).GetMaterialBindSubsets()
        for purpose in ("full", "preview"):
            paths = [
                UsdShade.MaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial(purpose)[0].GetPath()
                for subset in subsets
            ]
            self.assertCountEqual(
                paths, [Sdf.Path("/MaterialPurposes/Materials/" + name + purpose) for name in ("A", "B")]
            )

    def test_unbound_faces_get_no_empty_material_bindings(self):
        # Unbound faces stay unbound without empty binding relationships, which USD's binding validator rejects.
        """Unbound faces stay unbound without empty binding relationships, which USD's binding validator rejects."""
        for bound in ([], ["A"]):
            with self.subTest(bound=bound):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Robot")
                for offset, name in enumerate(("A", "B")):
                    prim = _mesh(stage, "/Robot/" + name, offset * 2)
                    if name in bound:
                        material = UsdShade.Material.Define(stage, "/Materials/" + name)
                        UsdShade.MaterialBindingAPI.Apply(prim).Bind(material)

                report = self._merge(stage, paths=["/Robot"])

                output = stage.GetPrimAtPath(report["merged"][0]["output"])
                prims = [output] + [s.GetPrim() for s in UsdShade.MaterialBindingAPI(output).GetMaterialBindSubsets()]
                bindings = [
                    rel
                    for prim in prims
                    for rel in prim.GetAuthoredRelationships()
                    if rel.GetName().startswith("material:binding")
                ]
                self.assertEqual([rel.GetPath() for rel in bindings if not rel.GetTargets()], [])
                materials = [UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()[0] for prim in prims]
                self.assertCountEqual(
                    [m.GetPath() for m in materials if m], [Sdf.Path("/Materials/" + name) for name in bound]
                )

    def test_retained_body_collider_keeps_its_mass_contribution(self):
        # Moving a body's own collider below its retained frame preserves the body's computed mass properties.
        """Preserve mass settings that describe both a body override and its own collision geometry."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
        body = stage.GetPrimAtPath("/RetainedBodyMass")

        def mass_information(prim):
            """Supply unit-volume colliders in the fixture's untransformed body frame."""
            result = UsdPhysics.RigidBodyAPI.MassInformation()
            result.volume = 1.0
            result.inertia = Gf.Matrix3f(0.2)
            result.centerOfMass = Gf.Vec3f(0)
            result.localPos = Gf.Vec3f(UsdGeom.XformCache().GetLocalToWorldTransform(prim).ExtractTranslation())
            result.localRot = Gf.Quatf(1)
            return result

        before = UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(mass_information)

        report = self._merge(stage, paths=["/RetainedBodyMass"])

        self.assertEqual([item["role"] for item in report["merged"]], ["visual"])
        after = UsdPhysics.RigidBodyAPI(body).ComputeMassProperties(mass_information)
        self.assertAlmostEqual(after[0], before[0])
        self.assertTrue(Gf.IsClose(after[1], before[1], 1e-5))
        self.assertTrue(Gf.IsClose(after[2], before[2], 1e-5))

    def test_retained_collider_subsets_keep_applied_apis(self):
        # Moving collision geometry below a retained body frame preserves subset APIs and physics materials.
        """Keep authored subset APIs even when their schema plugins are unavailable."""
        stage = self._open_stage("mergePhysicsBodyMeshes/ancillary.usda")
        body_path = Sdf.Path("/RetainedSubset/Body")
        subset_path = body_path.AppendChild("Face")
        expected_schemas = stage.GetPrimAtPath(subset_path).GetMetadata("apiSchemas").GetAppliedItems()
        expected_materials = {Sdf.Path("/RetainedSubset/" + name) for name in ("BaseMaterial", "FaceMaterial")}

        def parsed_materials():
            """Return parser-visible physics materials for this fixture's mesh colliders."""
            parse = getattr(UsdPhysics, "UsdPhysicsLoadStageFromPrimRange", None)
            if parse is None:
                parse = UsdPhysics.LoadUsdPhysicsFromRange
            parsed = parse(stage, [Sdf.Path("/RetainedSubset")])
            _, shapes = parsed[UsdPhysics.ObjectType.MeshShape]
            return {shape.primPath: set(shape.materials) for shape in shapes}

        self.assertEqual(parsed_materials(), {body_path: expected_materials})

        report = self._merge(stage, paths=["/RetainedSubset"])

        self.assertEqual([item["role"] for item in report["merged"]], ["visual"])
        collision_path = body_path.AppendChild("Collision")
        copied_subset = stage.GetPrimAtPath(collision_path.AppendChild("Face"))
        self.assertTrue(copied_subset.HasAPI(UsdShade.MaterialBindingAPI))
        self.assertEqual(copied_subset.GetMetadata("apiSchemas").GetAppliedItems(), expected_schemas)
        self.assertEqual(parsed_materials(), {collision_path: expected_materials})
