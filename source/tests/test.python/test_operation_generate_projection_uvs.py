# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.scripts import standalone
from pxr import Gf, Sdf, Usd, UsdGeom, Vt

from .test_utils import Test_Operation


def _tot_st_count(stage):
    # Return count of STs and ST indices, as well as the set of values in ST
    tot_st = 0
    tot_st_indices = 0
    st_vals = set()
    it = iter(Usd.PrimRange(stage.GetPseudoRoot(), Usd.TraverseInstanceProxies()))
    for prim in it:
        if prim.IsA(UsdGeom.Mesh):
            mesh = UsdGeom.PrimvarsAPI(prim)
            if mesh.HasPrimvar("primvars:st"):
                num_st = len(mesh.GetPrimvar("primvars:st").GetAttr().Get())
                tot_st += num_st
                stIndicesPrimvars = mesh.GetPrimvar("primvars:st").GetIndicesAttr()
                if stIndicesPrimvars:
                    num_st_indices = len(stIndicesPrimvars.Get())
                    tot_st_indices += num_st_indices

                st = mesh.GetPrimvar("primvars:st").GetAttr().Get()
                for v in range(num_st):
                    st_vals.add(st[v][0])
                    st_vals.add(st[v][1])

    return tot_st, tot_st_indices, st_vals


def _prim_has_uvs(prim):
    if prim.IsA(UsdGeom.Mesh):
        mesh = UsdGeom.PrimvarsAPI(prim)
        if mesh.HasPrimvar("primvars:st"):
            return True
    return False


def _prim_spec_has_uvs(prim_spec):
    for prop in prim_spec.properties:
        if "primvars:st" in prop.name:
            return True
    return False


class Test_Operation_Project_UVs(Test_Operation):

    OPERATION = "generateProjectionUVs"

    async def test_ProjectUVs(self):
        """Test projecting UVs"""
        stage = self._open_stage("meshNoUVs.usd")

        # Test original number of UVs
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 0)
        self.assertEqual(num_st_indices, 0)

        # Project UVs using cube mapping
        self._execute_json(stage, "generateProjectionUVs.json")

        # Test new number of UVs. Expect 20 unique values and 24 indices on a cube with cube projection.
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 20)
        self.assertEqual(num_st_indices, 24)

        # Test that there are exactly two distinct ST values which are approximately +0.05 and -0.05.
        for st in st_vals:
            self.assertLess(abs(abs(st) - 0.05), 1e-6)
            if st < 0:
                saw_neg = True
            else:
                saw_pos = True

        self.assertTrue(saw_pos and saw_neg)

    async def test_ProjectUVsDegenerateFaces(self):
        """Test projecting UVs"""
        stage = self._open_stage("cubeDegenerateFaces.usda")

        # Test original number of UVs
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 0)
        self.assertEqual(num_st_indices, 0)

        # Project UVs using spherical mapping
        self._execute_json(stage, "generateProjectionUVsSpherical.json")

        # Test new number of UVs. Expect 20 unique values and 24 indices on a cube with cube projection.
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 7)
        self.assertEqual(num_st_indices, 28)

        # This test is mostly to verify that the spherical UV generator doesn't crash on
        # zero total face area

    async def test_ProjectUVsJSONScaleFactorAndScaleUnits(self):
        """Test that scale factor is respected via JSON"""

        stage = self._open_stage("meshNoUVs.usd")

        # Execute command
        json = """[{"operation": "generateProjectionUVs", "paths": [], "projectionType": 0, "scaleFactor": 1.0}]"""
        status = standalone.execute_commands_from_json(stage, json)

        cube = stage.GetPrimAtPath("/World/pCube1")
        self.assertTrue(cube)

        primvarsBefore = UsdGeom.PrimvarsAPI(cube).GetPrimvar("st").Get()
        self.assertEqual(len(primvarsBefore), 8)

        # Re-open stage and execute again with a different scale factor
        stage = self._open_stage("meshNoUVs.usd")
        json = """[{"operation": "generateProjectionUVs", "paths": [], "projectionType": 0, "scaleFactor": 0.02}]"""
        status = standalone.execute_commands_from_json(stage, json)

        cube = stage.GetPrimAtPath("/World/pCube1")
        self.assertTrue(cube)

        primvarsAfter = UsdGeom.PrimvarsAPI(cube).GetPrimvar("st").Get()
        self.assertEqual(len(primvarsAfter), 8)

        # Assert that the values don't match, i.e. that scaleFactor was respected
        # having been through the JSON parser
        for i in range(8):
            self.assertNotEqual(primvarsBefore[i], primvarsAfter[i])

        # Open the stage a third time and apply a different scale unit this time
        stage = self._open_stage("meshNoUVs.usd")
        json = """[{"operation": "generateProjectionUVs", "paths": [], "projectionType": 0, "scaleFactor": 0.02, "scaleUnits": 1.0}]"""
        status = standalone.execute_commands_from_json(stage, json)

        cube = stage.GetPrimAtPath("/World/pCube1")
        self.assertTrue(cube)

        primvarsAfterScaleUnits = UsdGeom.PrimvarsAPI(cube).GetPrimvar("st").Get()
        self.assertEqual(len(primvarsAfter), 8)

        # Assert that the values don't match from the case with no scale units
        for i in range(8):
            self.assertNotEqual(primvarsAfter[i], primvarsAfterScaleUnits[i])

    async def test_ProjectUVsJSONPreprojectionXform(self):
        """Test that preprojection xform is respected via JSON"""

        stage = self._open_stage("meshNoUVs.usd")

        # Execute command
        json = """[{"operation": "generateProjectionUVs", "paths": [], "projectionType": 0, "scaleFactor": 1.0, "preprojectionXform": [1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 1.0]}]"""
        status = standalone.execute_commands_from_json(stage, json)

        cube = stage.GetPrimAtPath("/World/pCube1")
        self.assertTrue(cube)

        primvarsBefore = UsdGeom.PrimvarsAPI(cube).GetPrimvar("st").Get()
        self.assertEqual(len(primvarsBefore), 8)

        # Re-open stage and execute again with a different preprojection xform
        stage = self._open_stage("meshNoUVs.usd")
        json = """[{"operation": "generateProjectionUVs", "paths": [], "projectionType": 0, "scaleFactor": 1.0, "preprojectionXform": [1.0, 0.5, 0.0, 0.0, 0.5, 1.0, 0.0, 0.5, 0.0, 1.0, 0.0, 0.0, 0.5, 0.0, 0.0, 0.5]}]"""
        status = standalone.execute_commands_from_json(stage, json)

        cube = stage.GetPrimAtPath("/World/pCube1")
        self.assertTrue(cube)

        primvarsAfter = UsdGeom.PrimvarsAPI(cube).GetPrimvar("st").Get()
        self.assertEqual(len(primvarsAfter), 8)

        # Assert that the values don't match, i.e. that the preprojection xform was respected
        # having been through the JSON parser
        for i in range(8):
            self.assertNotEqual(primvarsBefore[i], primvarsAfter[i])

    async def test_ProjectUVsCommand(self):
        """Test the Omni Command class"""

        stage = self._open_stage("meshNoUVs.usd")

        cube = stage.GetPrimAtPath("/World/pCube1")
        self.assertTrue(cube)

        args = {
            "paths": [],
            "projectionType": 4,
            "useWorldSpaceScales": True,
            "scaleFactor": 0.01,
            "scaleUnits": 0.0,
            "overwriteExisting": True,
        }

        success, result = self._execute_command(args)

        primvarsAfter = UsdGeom.PrimvarsAPI(cube).GetPrimvar("st").Get()
        self.assertEqual(len(primvarsAfter), 20)

    async def test_UVGeneration(self):
        # Test uv generation with all the supported linear units
        units = [
            UsdGeom.LinearUnits.micrometers,
            UsdGeom.LinearUnits.millimeters,
            UsdGeom.LinearUnits.centimeters,
            0.1,  # decimeters
            UsdGeom.LinearUnits.meters,
            UsdGeom.LinearUnits.kilometers,
            UsdGeom.LinearUnits.inches,
            UsdGeom.LinearUnits.feet,
            UsdGeom.LinearUnits.yards,
            UsdGeom.LinearUnits.miles,
            0.0000254,  # mils
        ]
        for unit in units:
            stage = Usd.Stage.CreateInMemory()
            self._current_stage = stage
            UsdGeom.SetStageMetersPerUnit(stage, unit)

            # # Populate a single faced mesh that is 1m x 1m
            path = Sdf.Path("/Mesh")
            mesh = UsdGeom.Mesh.Define(stage, path)
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([4]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2, 3]))

            # The point positions are divided by the meters per unit so that the effective size is 1m x 1m
            mesh.GetPointsAttr().Set(
                Vt.Vec3fArray(
                    [
                        Gf.Vec3f(0.0, 0.0, 0.0) / unit,
                        Gf.Vec3f(1.0, 0.0, 0.0) / unit,
                        Gf.Vec3f(1.0, 0.0, 1.0) / unit,
                        Gf.Vec3f(0.0, 0.0, 1.0) / unit,
                    ]
                )
            )

            # run the Projection UVs operation
            args = {
                "paths": [],
                "projectionType": 4,
                "useWorldSpaceScales": True,
                "scaleFactor": unit,
                "scaleUnits": 0.0,
                "overwriteExisting": True,
            }
            success, result = self._execute_command(args)

            # The mesh should now have uvs that are 0-1 in texcoord space
            # This matches the Omniverse Materials standard where library materials assume that a texture in 0-1 space should represent 1m
            prim = stage.GetPrimAtPath(path)
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:st"))

            # Determine the size of the uv coverage without assuming that it starts at 0, 0.
            # This does however assume that the uvs will not be rotated.
            uvRange = Gf.Range2f()
            for uv in primvar.ComputeFlattened():
                uvRange = uvRange.UnionWith(uv)
            uvSize = uvRange.GetMax() - uvRange.GetMin()

            # Assert that the coverage is close if not equal
            msg = f"UVs generated for stage with meters per unit of {unit} do not represent 1m x 1m in the 0-1 space"
            self.assertTrue(Gf.IsClose(uvSize, Gf.Vec2f(1.0, 1.0), 0.000001), msg=msg)

            # run the Projection UVs operation again but this time using 1m scaleUnits
            args = {
                "paths": [],
                "projectionType": 4,
                "useWorldSpaceScales": True,
                "scaleFactor": unit,
                "scaleUnits": 1.0,
                "overwriteExisting": True,
            }
            success, result = self._execute_command(args)

            # The mesh should now have uvs that cover the distance unit in relation to 1m
            # This matches the Omniverse Materials standard where library materials assume that a texture in 0-1 space should represent 1m
            prim = stage.GetPrimAtPath(path)
            primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:st"))

            # Determine the size of the uv coverage without assuming that it starts at 0, 0.
            # This does however assume that the uvs will not be rotated.
            uvRange = Gf.Range2f()
            for uv in primvar.ComputeFlattened():
                uvRange = uvRange.UnionWith(uv)
            uvSize = uvRange.GetMax() - uvRange.GetMin()

            # Assert that the coverage is close if not equal
            msg = f"UVs generated for stage with meters per unit of {unit} at 1m scaleUnits do not represent {unit}m x {unit}m in the 0-1 space"
            self.assertTrue(Gf.IsClose(uvSize, Gf.Vec2f(unit, unit), unit * 0.0001), msg=msg)

    async def test_prototype_meshes(self):
        """Test generate uvs operation on a scene with a variety of instancing methods and transforms"""
        stage = self._open_stage("no_uvs_scene.usda")

        # these are instances of prototypes with no scale or rotation, so no uvs should be authored on the instance
        no_uvs_in_overs = [
            "/World/Instances/External_Abstract/matrix_translate/Torus",
            "/World/Instances/External_Abstract/matrix_translate/Cube",
            "/World/Instances/External_Abstract/matrix_translate/Disk",
            "/World/Instances/External_Abstract/xform_translate/Torus",
            "/World/Instances/External_Abstract/xform_translate/Cube",
            "/World/Instances/External_Abstract/xform_translate/Disk",
            "/World/Instances/Internal_Abstract/matrix_translate/Torus",
            "/World/Instances/Internal_Abstract/matrix_translate/Cube",
            "/World/Instances/Internal_Abstract/matrix_translate/Disk",
            "/World/Instances/Internal_Abstract/xform_translate/Torus",
            "/World/Instances/Internal_Abstract/xform_translate/Cube",
            "/World/Instances/Internal_Abstract/xform_translate/Disk",
            "/World/Instances/Internal_Concrete/matrix_translate/Cube",
            "/World/Instances/Internal_Concrete/matrix_translate/Disk",
            "/World/Instances/Internal_Concrete/matrix_translate/Torus",
            # the cube in this set is rotated and gets uvs generated
            "/World/Instances/Internal_Concrete/xform_cube_rotate/Disk",
            "/World/Instances/Internal_Concrete/xform_cube_rotate/Torus",
            # this set has a scale applied to the parent xform plus scales applied to disk and torus individually
            # but the disk uses the prototype uvs (no over) because the sum of its scale ops cancel out
            "/World/Instances/Internal_Concrete/matrix_disk_scale_torus_scale/Disk",
        ]

        overs_get_uvs = [
            # this set reference an external asset, so uvs are authored as overs
            "/World/Prototypes/External/Xform/Shapes/Cube",
            "/World/Prototypes/External/Xform/Shapes/Disk",
            "/World/Prototypes/External/Xform/Shapes/Torus",
            # this set is scaled with a matrix transformation
            "/World/Instances/External_Abstract/matrix_scale/Torus",
            "/World/Instances/External_Abstract/matrix_scale/Cube",
            "/World/Instances/External_Abstract/matrix_scale/Disk",
            # this set is rotated with a rotation xform op
            "/World/Instances/External_Abstract/xform_rotate/Torus",
            "/World/Instances/External_Abstract/xform_rotate/Cube",
            "/World/Instances/External_Abstract/xform_rotate/Disk",
            # this set is scaled with a scalar xform op
            "/World/Instances/Internal_Abstract/xform_scale/Cube",
            "/World/Instances/Internal_Abstract/xform_scale/Disk",
            "/World/Instances/Internal_Abstract/xform_scale/Torus",
            # this set is rotated with a matrix transformation
            "/World/Instances/Internal_Abstract/matrix_rotate/Cube",
            "/World/Instances/Internal_Abstract/matrix_rotate/Disk",
            "/World/Instances/Internal_Abstract/matrix_rotate/Torus",
            # this cube is rotated individually from its set and its prototype
            "/World/Instances/Internal_Concrete/xform_cube_rotate/Cube",
            # this set is rotated with a rotation xform op
            "/World/Instances/Internal_Concrete/xform_rotate/Cube",
            "/World/Instances/Internal_Concrete/xform_rotate/Disk",
            "/World/Instances/Internal_Concrete/xform_rotate/Torus",
            # this set has a scale applied to the parent xform plus scales applied to disk and torus individually
            # but the disk uses the prototype uvs (no over) because the sum of its scale ops cancel out
            "/World/Instances/Internal_Concrete/matrix_disk_scale_torus_scale/Cube",
            "/World/Instances/Internal_Concrete/matrix_disk_scale_torus_scale/Torus",
        ]

        # these are concrete and abstract prototypes defined in this stage
        defs_get_uvs = [
            "/World/Shapes/Cube",
            "/World/Shapes/Disk",
            "/World/Shapes/Torus",
            "/World/Prototypes/Internal/Xform/Cube",
            "/World/Prototypes/Internal/Xform/Disk",
            "/World/Prototypes/Internal/Xform/Torus",
        ]

        # pre-check meshes, there should be no uvs authored
        for path in no_uvs_in_overs:
            self.assertFalse(_prim_has_uvs(stage.GetPrimAtPath(path)))
        for path in overs_get_uvs:
            self.assertFalse(_prim_has_uvs(stage.GetPrimAtPath(path)))
        for path in defs_get_uvs:
            self.assertFalse(_prim_has_uvs(stage.GetPrimAtPath(path)))

        # args for the operation
        args = {
            "paths": [],
            "projectionType": 4,
            "useWorldSpaceScales": True,
            "scaleFactor": UsdGeom.LinearUnits.centimeters,
            "scaleUnits": 1.0,
            "overwriteExisting": True,
        }

        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

        # check meshes for uvs
        # these should have no overs, their referenced uvs are valid
        for path in no_uvs_in_overs:
            prim = stage.GetPrimAtPath(path)
            self.assertTrue(_prim_has_uvs(prim))
            for spec in prim.GetPrimStack():
                # we only care about overs at the local path, overs are expected on reference prototypes
                if spec.path == path:
                    if spec.specifier is Sdf.SpecifierOver:
                        self.assertFalse(_prim_spec_has_uvs(spec))

        # these should have uvs authored as overs
        for path in overs_get_uvs:
            prim = stage.GetPrimAtPath(path)
            self.assertTrue(_prim_has_uvs(prim))

            # we are interested in the strongest opinion only
            spec = prim.GetPrimStack()[0]
            self.assertEqual(spec.specifier, Sdf.SpecifierOver)
            self.assertTrue(_prim_spec_has_uvs(spec))

        # these should have uvs authored on definitions
        for path in defs_get_uvs:
            prim = stage.GetPrimAtPath(path)
            self.assertTrue(_prim_has_uvs(prim))

            # we are interested in the strongest opinion only
            spec = prim.GetPrimStack()[0]
            self.assertEqual(spec.specifier, Sdf.SpecifierDef)
            self.assertTrue(_prim_spec_has_uvs(spec))

    async def test_time_varying_mesh(self):
        """Test generate uvs operation on a mesh with time varying attributes"""
        stage = self._open_stage("time_varying_mesh.usd")

        args = {
            "paths": [],
            "projectionType": 4,
            "useWorldSpaceScales": True,
            "scaleFactor": UsdGeom.LinearUnits.centimeters,
            "scaleUnits": 1.0,
            "overwriteExisting": True,
        }

        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)
