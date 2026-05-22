# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Pcp, Sdf, Usd, UsdShade

from .test_utils import Test_Operation, _get_context


class Test_Operation_Organize_Prototypes(Test_Operation):

    OPERATION = "organizePrototypes"

    # assert the given prim exists, has no children, is instanceable
    # and has authored references.
    def assert_is_instance(self, prim: Usd.Prim):
        self.assertTrue(prim)
        self.assertTrue(prim.IsInstanceable())
        children = prim.GetAllChildren()
        self.assertEqual(len(children), 0)
        self.assertTrue(prim.HasAuthoredReferences())

    # assert the given prim is bound to a naterial with the given path.
    def assert_bound_to_material(self, prim: Usd.Prim, material_path):
        mat, _ = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()
        self.assertTrue(mat)
        path = mat.GetPath()
        self.assertTrue(path == material_path)

    # assert the given prim has a reference to the given target.
    def assert_has_reference(self, prim: Usd.Prim, target_path):
        have_ref = False
        arcs = Usd.PrimCompositionQuery.GetDirectReferences(prim).GetCompositionArcs()
        for arc in arcs:
            if arc.GetArcType() == Pcp.ArcTypeReference:
                if arc.GetTargetPrimPath() == target_path:
                    have_ref = True
                    break
        self.assertTrue(have_ref)

    # assert that the given prim spec has a display name that matches the input
    # if the prim spec has no display name field, assert that the input name is empty
    def assert_display_name(self, spec: Sdf.PrimSpec, display_name):
        display_name_key = "displayName"
        if display_name_key in spec.GetMetaDataInfoKeys():
            value = spec.GetInfo(display_name_key)
            self.assertEqual(value, display_name)
        else:
            self.assertEqual("", display_name)

    async def test_nested_prototypes(self):

        stage = self._open_stage("organizePrototypes_nestedProtos.usda")

        # use the operation to copy the nested prototypes
        args: dict = {"prototypesNamespace": "Protos"}
        context: ExecutionContext = _get_context(stage, verbose=False)
        self._execute_command(args, context)

        prim: Usd.Prim = stage.GetPrimAtPath("/World/PlaneXf")
        self.assert_is_instance(prim)
        self.assert_has_reference(prim, "/Protos/PlaneXf")

        prim = stage.GetPrimAtPath("/Protos/PlaneXf")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Protos/PlaneXf/Plane")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Protos/PlaneXf/CubeXf")
        self.assert_is_instance(prim)
        self.assert_has_reference(prim, "/Protos/CubeXf")

        prim = stage.GetPrimAtPath("/Protos/CubeXf")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Protos/CubeXf/Cube")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Protos/CubeXf/DiskXf")
        self.assert_is_instance(prim)
        self.assert_has_reference(prim, "/Protos/DiskXf")

        prim = stage.GetPrimAtPath("/Protos/DiskXf")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Protos/DiskXf/Disk")
        self.assertTrue(prim)

    async def test_prototype_material(self):

        stage = self._open_stage("organizePrototypes_protoMaterial.usda")

        # use the operation to copy the prototype and confirm material
        # bindings are correctly updated
        args: dict = {}
        context: ExecutionContext = _get_context(stage, verbose=False)
        self._execute_command(args, context)

        prim: Usd.Prim = stage.GetPrimAtPath("/World/Plane")
        self.assert_is_instance(prim)
        self.assert_has_reference(prim, "/Prototypes/Plane")

        prim: Usd.Prim = stage.GetPrimAtPath("/World/Cube")
        self.assert_bound_to_material(prim, "/Prototypes/Plane/Looks/PlaneMaterial")

        prim = stage.GetPrimAtPath("/Prototypes/Plane")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Prototypes/Plane/Looks")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Prototypes/Plane/Looks/PlaneMaterial")
        prim = stage.GetPrimAtPath("/Prototypes/Plane/Looks/PlaneMaterial/Shader")
        prim = stage.GetPrimAtPath("/Prototypes/Plane/Geometry")
        self.assertTrue(prim)
        prim = stage.GetPrimAtPath("/Prototypes/Plane/Geometry/PlaneMesh")
        self.assertTrue(prim)
        self.assert_bound_to_material(prim, "/Prototypes/Plane/Looks/PlaneMaterial")

    async def test_hierarchy_display_names(self):

        stage = self._open_stage("organizeProtos_displayNames.usda")

        # use the operation with the levels of hierarchy option
        # check that display names are applied to prototypes correctly

        args: dict = {"prototypesNamespace": "Prototypes", "hierarchyLevels": 4}
        context: ExecutionContext = _get_context(stage, verbose=False)
        self._execute_command(args, context)

        no_display_names = [
            "/Prototypes/Cube/Cube/Cube/Xform1/Cube_Mesh",
            "/Prototypes/Cube/Cube/Cube/Xform1",
            "/Prototypes/Cube/Cube/Cube",
            "/Prototypes/Sphere/Sphere/Sphere/Xform1",
            "/Prototypes/Sphere/Sphere/Sphere",
        ]

        yes_display_names = {
            "/Prototypes/Cube": "Grandparent of Cube",
            "/Prototypes/Cube/Cube": "Parent of Cube",
            "/Prototypes/Sphere": "球体の祖父母",
            "/Prototypes/Sphere/Sphere": "球の親",
            "/Prototypes/Sphere/Sphere/Sphere/Xform1/Sphere_Mesh": "球体",
        }

        layer: Sdf.Layer = stage.GetEditTarget().GetLayer()
        for path in no_display_names:
            spec: Sdf.PrimSpec = layer.GetPrimAtPath(path)
            self.assert_display_name(spec, "")

        for path, display_name in yes_display_names.items():
            spec: Sdf.PrimSpec = layer.GetPrimAtPath(path)
            self.assert_display_name(spec, display_name)
