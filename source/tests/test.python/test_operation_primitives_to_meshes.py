# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Gf, Sdf, Usd, UsdGeom

from .test_utils import Test_Operation, _get_meshes

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "convertSpheres": True,
    "sphereLongitudeDivisions": 32,
    "sphereLatitudeDivisions": 16,
    "convertCylinders": True,
    "cylinderWallDivisions": 32,
    "cylinderLatitudeDivisions": 1,
    "cylinderEndcaps": True,
    "convertCapsules": True,
    "capsuleRadialDivisions": 32,
    "capsuleLengthDivisions": 1,
    "capsuleCapDivisions": 8,
    "convertCones": True,
    "coneSideDivisions": 64,
    "coneLengthDivisions": 3,
    "coneBases": True,
    "convertCubes": True,
}


def _get_primitive_shapes(stage):
    return [UsdGeom.Gprim(x) for x in stage.TraverseAll() if x.IsA(UsdGeom.Gprim) and not x.IsA(UsdGeom.PointBased)]


def _get_all_gprims(stage):
    return [UsdGeom.Gprim(x) for x in stage.TraverseAll() if x.IsA(UsdGeom.Gprim)]


def _get_converted_meshes(stage):
    """Meshes that replaced a primitive: they reference a prototype instead of authoring points."""
    return [x for x in _get_meshes(stage) if x.HasAuthoredReferences()]


class Test_Operation_Primitives_to_Meshes(Test_Operation):

    OPERATION = "primitivesToMeshes"

    def _assert_extent_matches_points(self, prim):
        """The extent must be authored on the prim itself and equal the extent of its composed points."""
        extent_attr = UsdGeom.Boundable(prim).GetExtentAttr()
        self.assertTrue(extent_attr.HasAuthoredValue(), f"{prim.GetPath()} has no extent")
        # HasAuthoredValue() also counts an extent that composes in from the prototype reference
        spec = prim.GetStage().GetEditTarget().GetLayer().GetAttributeAtPath(extent_attr.GetPath())
        self.assertTrue(spec and spec.HasDefaultValue(), f"{prim.GetPath()} does not author its own extent")
        extent = extent_attr.Get()
        expected = UsdGeom.PointBased.ComputeExtent(UsdGeom.Mesh(prim).GetPointsAttr().Get())
        self.assertEqual(len(extent), 2)
        for got, want in zip(extent, expected):
            self.assertTrue(
                Gf.IsClose(got, want, 1e-4), f"{prim.GetPath()} extent {extent} != points extent {expected}"
            )

    def _define_primitives(self, stage, with_extents):
        """Define one of each convertible primitive. The source extents are optional, like in real assets."""
        shapes = [
            (UsdGeom.Cube.Define(stage, "/World/Cube"), {"size": 6.0}),
            (UsdGeom.Cylinder.Define(stage, "/World/Cylinder"), {"radius": 2.0, "height": 10.0, "axis": "Z"}),
            (UsdGeom.Sphere.Define(stage, "/World/Sphere"), {"radius": 4.0}),
            (UsdGeom.Cone.Define(stage, "/World/Cone"), {"radius": 3.0, "height": 8.0}),
        ]
        for index, (shape, attributes) in enumerate(shapes):
            for name, value in attributes.items():
                shape.GetPrim().GetAttribute(name).Set(value)
            shape.AddTranslateOp().Set(Gf.Vec3d(10.0 * index, 0.0, 0.0))
            if with_extents:
                shape.GetExtentAttr().Set(UsdGeom.Boundable.ComputeExtentFromPlugins(shape, Usd.TimeCode.Default()))
        return [shape for shape, _ in shapes]

    async def test_primitives_to_meshes(self):
        """Test primitive to mesh conversion"""

        stage = self._open_stage("primitives.usda")

        # Check the primitives in the stage
        before_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(before_prims), 5)
        names = [x.GetPrim().GetName() for x in before_prims]
        self.assertTrue("Sphere" in names)
        self.assertTrue("Cylinder" in names)
        self.assertTrue("Cone" in names)
        self.assertTrue("Cube" in names)
        self.assertTrue("Capsule" in names)

        # Check that there are no meshes in the stage
        before_meshes = _get_meshes(stage)
        self.assertEqual(len(before_meshes), 0)

        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)

        # The operation should execute successfully.
        self.assertTrue(success)

        # All supported primitives should have been converted.
        after_prims = _get_primitive_shapes(stage)
        self.assertEqual(len(after_prims), 0)

        # check generated meshes
        after_primitive_shapes = _get_meshes(stage)
        for mesh in after_primitive_shapes:
            mesh_is_old_prim_with_references = mesh.GetPrim().HasAuthoredReferences() and (
                mesh.GetPrim().GetName() == "Sphere"
                or mesh.GetPrim().GetName() == "Cylinder"
                or mesh.GetPrim().GetName() == "Cone"
                or mesh.GetPrim().GetName() == "Cube"
                or mesh.GetPrim().GetName() == "Capsule"
            )
            mesh_is_new_prim_without_references = not mesh.GetPrim().HasAuthoredReferences() and (
                mesh.GetPrim().GetName() == "sphere_Y_32_16"
                or mesh.GetPrim().GetName() == "cylinder_Y_32_1"
                or mesh.GetPrim().GetName() == "cone_Y_64_3"
                or mesh.GetPrim().GetName() == "cube_Y"
                or mesh.GetPrim().GetName().startswith("capsule_Y_32_1_8_")
            )
            self.assertTrue(mesh_is_old_prim_with_references or mesh_is_new_prim_without_references)

    async def test_primitives_to_meshes_selections(self):
        """Test primitive to mesh conversion selections"""

        stage = self._open_stage("primitives.usda")

        # Iterate through all combinations of flags
        for mask in range(32):
            # Start with the default arguments
            args = DEFAULT_ARGS.copy()

            # Set test flags based on mask
            args["convertSpheres"] = True if (mask & 1) != 0 else False
            args["convertCylinders"] = True if (mask & 2) != 0 else False
            args["convertCones"] = True if (mask & 4) != 0 else False
            args["convertCubes"] = True if (mask & 8) != 0 else False
            args["convertCapsules"] = True if (mask & 16) != 0 else False

            # Execute the command
            success, result = self._execute_command(args)
            self.assertTrue(success)

            # check generated primitives
            after_gprims = _get_all_gprims(stage)
            for gprim in after_gprims:
                if gprim.GetPrim().GetName() == "Sphere":
                    if args["convertSpheres"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Sphere))
                elif gprim.GetPrim().GetName() == "Cylinder":
                    if args["convertCylinders"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cylinder))
                elif gprim.GetPrim().GetName() == "Cone":
                    if args["convertCones"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cone))
                elif gprim.GetPrim().GetName() == "Cube":
                    if args["convertCubes"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Cube))
                elif gprim.GetPrim().GetName() == "Capsule":
                    if args["convertCapsules"]:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Mesh))
                    else:
                        self.assertTrue(gprim.GetPrim().IsA(UsdGeom.Capsule))

            stage.Reload()

    async def test_converted_primitives_have_extents(self):
        """Converted meshes keep an extent, so usd-validation-nvidia's ExtentsChecker stays clean (NVBug 6850403)"""

        stage = self._open_stage("primitives.usda")

        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)

        converted = _get_converted_meshes(stage)
        self.assertEqual(len(converted), 5)
        for mesh in converted:
            self._assert_extent_matches_points(mesh)

    async def test_capsule_dimensions_axes_and_transforms(self):
        """Caps retain their radius for every axis, including short bodies and zero-height capsules."""
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        expected = {}
        for axis_index, axis in enumerate(("X", "Y", "Z")):
            for index, (radius, height) in enumerate(((0.5, 1.0), (2.0, 9.0), (3.0, 0.2), (1.0, 0.0))):
                capsule = UsdGeom.Capsule.Define(stage, f"/World/Capsule_{axis}_{index}")
                capsule.CreateAxisAttr(axis)
                capsule.CreateRadiusAttr(radius)
                capsule.CreateHeightAttr(height)
                capsule.AddTranslateOp().Set(Gf.Vec3d(10.0, -2.0, 4.0))
                capsule.AddRotateXYZOp().Set(Gf.Vec3f(15.0, 30.0, 45.0))
                capsule.AddScaleOp().Set(Gf.Vec3f(2.0, 0.5, 1.5))
                capsule.CreateDisplayColorAttr([(0.2, 0.4, 0.6)])
                # Force replacement of a stale extent.
                capsule.CreateExtentAttr([Gf.Vec3f(-100.0), Gf.Vec3f(100.0)])
                expected[capsule.GetPath()] = (
                    radius,
                    height,
                    axis_index,
                    capsule.ComputeLocalToWorldTransform(Usd.TimeCode.Default()),
                )

        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)
        self.assertTrue(result[0], result)
        for path, (radius, height, axis_index, original_transform) in expected.items():
            prim = stage.GetPrimAtPath(path)
            self.assertTrue(prim.IsA(UsdGeom.Mesh))
            mesh = UsdGeom.Mesh(prim)
            expected_transform = Gf.Matrix4d().SetScale(radius) * original_transform
            self.assertTrue(
                Gf.IsClose(mesh.ComputeLocalToWorldTransform(Usd.TimeCode.Default()), expected_transform, 1e-6)
            )
            self.assertTrue(Gf.IsClose(mesh.GetDisplayColorAttr().Get()[0], Gf.Vec3f(0.2, 0.4, 0.6), 1e-6))
            for attribute in ("radius", "height", "axis"):
                self.assertIsNone(prim.GetAttribute(attribute).Get(), f"{path}: {attribute} was not blocked")
            self._assert_extent_matches_points(prim)
            extent = mesh.GetExtentAttr().Get()
            half_size = Gf.Vec3f(1.0)
            half_size[axis_index] += height * 0.5 / radius
            self.assertTrue(Gf.IsClose(extent[0], -half_size, 1e-4), f"{path}: {extent}")
            self.assertTrue(Gf.IsClose(extent[1], half_size, 1e-4), f"{path}: {extent}")
            # Every point must lie on a cylinder or one of the two spheres centered at its endpoints.
            for point in mesh.GetPointsAttr().Get():
                scaled_point = Gf.Vec3d(point) * radius
                center = Gf.Vec3d(0.0)
                center[axis_index] = max(-height * 0.5, min(height * 0.5, scaled_point[axis_index]))
                self.assertAlmostEqual((scaled_point - center).GetLength(), radius, places=4)

    async def test_capsule_defaults(self):
        """An unauthored capsule uses the USD schema's radius, height and axis defaults."""
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        capsule = UsdGeom.Capsule.Define(stage, "/Capsule")
        expected = UsdGeom.Boundable.ComputeExtentFromPlugins(capsule, Usd.TimeCode.Default())
        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)
        self.assertTrue(result[0], result)
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/Capsule"))
        self._assert_extent_matches_points(mesh.GetPrim())
        transform = mesh.ComputeLocalToWorldTransform(Usd.TimeCode.Default())
        for got, want in zip(mesh.GetExtentAttr().Get(), expected):
            self.assertTrue(Gf.IsClose(Gf.Vec3f(transform.Transform(Gf.Vec3d(got))), want, 1e-5))

    async def test_capsule_prototypes_and_subdivisions(self):
        """Matching ratios share a radius-one prototype; dimensions are restored by uniform scaling."""
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        dimensions = (("A", 1.0, 2.0), ("B", 2.0, 4.0), ("C", 0.5, 1.0), ("D", 2.0, 2.0), ("E", 1.0, 5.0))
        for name, radius, height in dimensions:
            capsule = UsdGeom.Capsule.Define(stage, f"/{name}")
            capsule.CreateRadiusAttr(radius)
            capsule.CreateHeightAttr(height)
        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)
        self.assertTrue(result[0], result)

        def prototype_path(name):
            return stage.GetRootLayer().GetPrimAtPath(f"/{name}").referenceList.prependedItems[0].primPath

        first_prototype = prototype_path("A")
        self.assertEqual(first_prototype, prototype_path("B"))
        self.assertEqual(first_prototype, prototype_path("C"))
        self.assertNotEqual(first_prototype, prototype_path("D"))
        self.assertNotEqual(first_prototype, prototype_path("E"))
        self.assertEqual(first_prototype.name, "capsule_Z_32_1_8_hr8000000")
        for name, radius, height in dimensions:
            mesh = UsdGeom.Mesh(stage.GetPrimAtPath(f"/{name}"))
            transform = mesh.ComputeLocalToWorldTransform(Usd.TimeCode.Default())
            self.assertTrue(Gf.IsClose(transform, Gf.Matrix4d().SetScale(radius), 1e-6))
            self._assert_extent_matches_points(mesh.GetPrim())
            bounds = [transform.Transform(Gf.Vec3d(point)) for point in mesh.GetExtentAttr().Get()]
            half_size = Gf.Vec3d(radius, radius, height * 0.5 + radius)
            self.assertTrue(Gf.IsClose(bounds[0], -half_size, 1e-5))
            self.assertTrue(Gf.IsClose(bounds[1], half_size, 1e-5))
        first_count = len(UsdGeom.Mesh(stage.GetPrimAtPath("/A")).GetPointsAttr().Get())
        subdivisions = (("capsuleRadialDivisions", 64), ("capsuleLengthDivisions", 3), ("capsuleCapDivisions", 16))
        for argument, value in subdivisions:
            capsule = UsdGeom.Capsule.Define(stage, "/Subdivided")
            capsule.CreateRadiusAttr(1.0)
            capsule.CreateHeightAttr(2.0)
            args = DEFAULT_ARGS.copy()
            args[argument] = value
            success, result = self._execute_command(args)
            self.assertTrue(success)
            self.assertTrue(result[0], result)
            self.assertNotEqual(first_prototype, prototype_path("Subdivided"))
            mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/Subdivided"))
            self.assertGreater(len(mesh.GetPointsAttr().Get()), first_count)
            self._assert_extent_matches_points(mesh.GetPrim())
            stage.RemovePrim("/Subdivided")

    async def test_huge_division_counts_are_capped(self):
        """INT_MAX divisions overflowed OmniMesh's 32-bit face and corner counts and crashed"""

        for argument in [name for name in DEFAULT_ARGS if name.endswith("Divisions")]:
            stage = self._open_stage("primitives.usda")
            args = DEFAULT_ARGS.copy()
            args[argument] = 2**31 - 1
            success, _ = self._execute_command(args)
            self.assertTrue(success, argument)
            self.assertEqual(_get_primitive_shapes(stage), [], argument)

    async def test_capsule_ratio_reuse_across_runs_and_edited_prototypes(self):
        """Ratio keys persist across runs, while edited prototypes still require disambiguation."""
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage

        def convert(name, radius, height):
            capsule = UsdGeom.Capsule.Define(stage, f"/{name}")
            capsule.CreateRadiusAttr(radius)
            capsule.CreateHeightAttr(height)
            success, result = self._execute_command(DEFAULT_ARGS.copy())
            self.assertTrue(success)
            self.assertTrue(result[0], result)
            return stage.GetRootLayer().GetPrimAtPath(f"/{name}").referenceList.prependedItems[0].primPath

        first_prototype = convert("A", 1.0, 2.0)
        self.assertEqual(first_prototype, convert("B", 5.0, 10.0))
        points_attr = UsdGeom.Mesh(stage.GetPrimAtPath(first_prototype)).GetPointsAttr()
        points = points_attr.Get()
        points[0] += Gf.Vec3f(1.0)
        points_attr.Set(points)
        replacement = convert("C", 0.5, 1.0)
        self.assertNotEqual(first_prototype, replacement)
        self.assertEqual(replacement.name, first_prototype.name + "_1")
        self.assertEqual(replacement, convert("D", 2.0, 4.0))
        self.assertEqual(convert("ZeroHeight", 1.0, 0.0), convert("NegativeZeroHeight", 2.0, -0.0))
        self.assertEqual(replacement, convert("NearbyRatio", 1.0, 2.0000001))
        self.assertNotEqual(replacement, convert("DifferentRatio", 1.0, 2.02))

    async def test_capsule_ratio_codes_use_20_fraction_bits_and_uppercase_hex(self):
        """Nearby ratios use the same canonical geometry in either conversion order."""
        prototype_points = []
        for heights in ((2.0000001, 2.0000002), (2.0000002, 2.0000001)):
            stage = Usd.Stage.CreateInMemory()
            self._current_stage = stage
            for index, height in enumerate(heights):
                capsule = UsdGeom.Capsule.Define(stage, f"/Capsule_{index}")
                capsule.CreateRadiusAttr(1.0)
                capsule.CreateHeightAttr(height)
                success, result = self._execute_command(DEFAULT_ARGS.copy())
                self.assertTrue(success)
                self.assertTrue(result[0], result)
            references = [
                stage.GetRootLayer().GetPrimAtPath(f"/Capsule_{index}").referenceList.prependedItems[0].primPath
                for index in range(2)
            ]
            self.assertEqual(references[0], references[1])
            self.assertEqual(references[0].name, "capsule_Z_32_1_8_hr8000000")
            mesh = UsdGeom.Mesh(stage.GetPrimAtPath(references[0]))
            prototype_points.append(list(mesh.GetPointsAttr().Get()))
            self.assertEqual(mesh.GetExtentAttr().Get()[1], Gf.Vec3f(1.0, 1.0, 2.0))
            # Ratio 1.0 exercises the uppercase A-F digits; ratio zero exercises leading zeroes.
            for name, height, code in (
                ("HexLetters", 1.0, "7F00000"),
                ("ZeroHeight", 0.0, "0000000"),
                ("FractionBits", 1.0 + 0xABCDE / 2**20, "7FABCDE"),
            ):
                capsule = UsdGeom.Capsule.Define(stage, f"/{name}")
                capsule.CreateRadiusAttr(1.0)
                capsule.CreateHeightAttr(height)
                success, result = self._execute_command(DEFAULT_ARGS.copy())
                self.assertTrue(success)
                self.assertTrue(result[0], result)
                reference = stage.GetRootLayer().GetPrimAtPath(f"/{name}").referenceList.prependedItems[0].primPath
                self.assertEqual(reference.name, f"capsule_Z_32_1_8_hr{code}")
        self.assertEqual(prototype_points[0], prototype_points[1])

    async def test_capsule_ratios_are_converted_to_float_before_quantizing(self):
        """Float rounding and underflow share canonical prototypes without collision suffixes."""
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        references = []
        for name, height in (
            ("Normal", 2.0),
            ("Rounded", 2.0 - 2.0**-25),
            ("Truncated", 2.0 + 3.0 * 2.0**-22),
            ("Tiny", 2.0**-255),
            ("Zero", 0.0),
        ):
            capsule = UsdGeom.Capsule.Define(stage, f"/{name}")
            capsule.CreateRadiusAttr(1.0)
            capsule.CreateHeightAttr(height)
            success, result = self._execute_command(DEFAULT_ARGS.copy())
            self.assertTrue(success)
            self.assertTrue(result[0], result)
            reference = stage.GetRootLayer().GetPrimAtPath(f"/{name}").referenceList.prependedItems[0].primPath
            references.append(reference)
            extent = UsdGeom.Mesh(stage.GetPrimAtPath(reference)).GetExtentAttr().Get()
            self.assertAlmostEqual(extent[1][2], 1.0 + height * 0.5, places=5)
        self.assertEqual(references[0].name, "capsule_Z_32_1_8_hr8000000")
        self.assertEqual(references[0], references[1])
        self.assertEqual(references[0], references[2])
        self.assertEqual(references[3].name, "capsule_Z_32_1_8_hr0000000")
        self.assertEqual(references[3], references[4])

    async def test_capsules_without_a_valid_ratio_are_skipped(self):
        """Nonpositive radii and nonfinite ratios cannot define a normalized capsule prototype."""
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        dimensions = (
            (0.0, 1.0),
            (-1.0, 1.0),
            (float("inf"), 1.0),
            (1.0, float("nan")),
            (1.0, -1.0),
            (1e-308, 1e308),
            (1.0, 1e40),
        )
        for index, (radius, height) in enumerate(dimensions):
            capsule = UsdGeom.Capsule.Define(stage, f"/Invalid_{index}")
            capsule.CreateRadiusAttr(radius)
            capsule.CreateHeightAttr(height)
        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)
        self.assertTrue(result[0], result)
        self.assertEqual(len(_get_primitive_shapes(stage)), len(dimensions))
        self.assertFalse(_get_meshes(stage))

    async def test_capsules_with_animation_or_references_are_skipped(self):
        """Capsules obey the operation's existing restrictions on animation and references."""
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        animated = UsdGeom.Capsule.Define(stage, "/Animated")
        animated.CreateHeightAttr().Set(2.0, Usd.TimeCode(1.0))
        animated.GetHeightAttr().Set(4.0, Usd.TimeCode(2.0))
        source = Sdf.Layer.CreateAnonymous(".usda")
        UsdGeom.Capsule.Define(Usd.Stage.Open(source), "/Capsule")
        referenced = stage.DefinePrim("/Referenced")
        referenced.GetReferences().AddReference(source.identifier, "/Capsule")
        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)
        self.assertTrue(result[0], result)
        self.assertTrue(animated.GetPrim().IsA(UsdGeom.Capsule))
        self.assertTrue(referenced.IsA(UsdGeom.Capsule))

    async def test_converted_primitives_have_extents_when_the_source_has_none(self):
        """Sources that never authored an extent still convert to meshes with one"""

        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        self._define_primitives(stage, with_extents=False)

        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)

        converted = _get_converted_meshes(stage)
        self.assertEqual(len(converted), 4)
        for mesh in converted:
            self._assert_extent_matches_points(mesh)

    async def test_source_extent_from_a_weaker_layer_is_replaced(self):
        """An extent authored under the source's old size must not win over the converted mesh's extent"""

        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        shapes = self._define_primitives(stage, with_extents=False)

        # A weaker layer that only carries each primitive's extent, so only a stronger opinion can replace it
        sublayer = Sdf.Layer.CreateAnonymous(".usda")
        sub_stage = Usd.Stage.Open(sublayer)
        old_extents = {}
        for shape in shapes:
            old_extents[shape.GetPath()] = UsdGeom.Boundable.ComputeExtentFromPlugins(shape, Usd.TimeCode.Default())
            extent_attr = sub_stage.OverridePrim(shape.GetPath()).CreateAttribute(
                "extent", Sdf.ValueTypeNames.Float3Array
            )
            extent_attr.Set(old_extents[shape.GetPath()])
        stage.GetRootLayer().subLayerPaths.append(sublayer.identifier)
        for shape in shapes:
            # The old extent really does come from the weaker layer
            self.assertEqual(shape.GetExtentAttr().Get(), old_extents[shape.GetPath()])

        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)

        converted = _get_converted_meshes(stage)
        self.assertEqual(len(converted), 4)
        for mesh in converted:
            self._assert_extent_matches_points(mesh)
            # The mesh is a unit shape in its own space; the old extent was scaled by the primitive's size
            extent = UsdGeom.Boundable(mesh).GetExtentAttr().Get()
            self.assertTrue(Gf.IsClose(extent[0], Gf.Vec3f(-1.0), 1e-4), f"{mesh.GetPath()}: {extent}")
            self.assertTrue(Gf.IsClose(extent[1], Gf.Vec3f(1.0), 1e-4), f"{mesh.GetPath()}: {extent}")

    async def test_primitives_defined_in_a_weaker_layer_are_converted(self):
        """Primitives whose type comes from a sublayer become meshes, and their sublayer extents are replaced"""

        sublayer = Sdf.Layer.CreateAnonymous(".usda")
        self._define_primitives(Usd.Stage.Open(sublayer), with_extents=True)
        stage = Usd.Stage.CreateInMemory()
        self._current_stage = stage
        stage.GetRootLayer().subLayerPaths.append(sublayer.identifier)

        success, result = self._execute_command(DEFAULT_ARGS.copy())
        self.assertTrue(success)

        converted = _get_converted_meshes(stage)
        self.assertEqual(len(converted), 4)
        for mesh in converted:
            self._assert_extent_matches_points(mesh)
