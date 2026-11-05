# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


import os
import subprocess
import tempfile

from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade, Vt

from .test_cli import _CLI
from .test_utils import Test_Operation, _get_context, _get_meshes

CONFORM_THEN_MERGE = '[{"operation": "conformMeshes"}, {"operation": "merge"}]'
MERGE_ONLY = '[{"operation": "merge"}]'


def _define_quad(stage, path, x):
    """Define a one face mesh whose corner is at x."""
    mesh = UsdGeom.Mesh.Define(stage, path)
    mesh.CreatePointsAttr([Gf.Vec3f(x, 0, 0), Gf.Vec3f(x + 1, 0, 0), Gf.Vec3f(x + 1, 1, 0), Gf.Vec3f(x, 1, 0)])
    mesh.CreateFaceVertexCountsAttr([4])
    mesh.CreateFaceVertexIndicesAttr([0, 1, 2, 3])
    return mesh


def _add_uvs(mesh, name, values, type_name=Sdf.ValueTypeNames.TexCoord2fArray):
    primvar = UsdGeom.PrimvarsAPI(mesh).CreatePrimvar(name, type_name, UsdGeom.Tokens.faceVarying)
    primvar.Set(Vt.Vec2fArray(values))


def _two_quads():
    """A stage with two meshes, /World/a and /World/b, that merge as they are."""
    stage = Usd.Stage.CreateInMemory()
    UsdGeom.Xform.Define(stage, "/World")
    return stage, _define_quad(stage, "/World/a", 0), _define_quad(stage, "/World/b", 2)


UVS_A = [(0, 0), (0.5, 0), (0.5, 0.5), (0, 0.5)]
UVS_B = [(0.25, 0.25), (1, 0.25), (1, 1), (0.25, 1)]


def _flat_values(prim, name):
    """The values of a primvar of a merged mesh, expanded through its indices."""
    return list(UsdGeom.PrimvarsAPI(prim).GetPrimvar(name).ComputeFlattened())


def _bind_uv_material(stage, path, prim, varname, through_material_input=False):
    """Bind to prim a material whose texture reads the UV set varname, set on the reader or on a material input."""
    material = UsdShade.Material.Define(stage, path)
    reader = UsdShade.Shader.Define(stage, path + "/reader")
    reader.CreateIdAttr("UsdPrimvarReader_float2")
    reader_varname = reader.CreateInput("varname", Sdf.ValueTypeNames.String)
    if through_material_input:
        material_input = material.CreateInput("stPrimvarName", Sdf.ValueTypeNames.String)
        material_input.Set(varname)
        reader_varname.ConnectToSource(material_input)
    else:
        reader_varname.Set(varname)
    texture = UsdShade.Shader.Define(stage, path + "/texture")
    texture.CreateIdAttr("UsdUVTexture")
    texture.CreateInput("st", Sdf.ValueTypeNames.Float2).ConnectToSource(reader.ConnectableAPI(), "result")
    UsdShade.MaterialBindingAPI.Apply(prim).Bind(material)


class Test_Operation_Conform_Meshes(Test_Operation):

    OPERATION = "conformMeshes"

    async def test_merge_alone_keeps_meshes_apart(self):
        """Without conformMeshes, merge leaves apart meshes that have a UV set on only one of them."""
        stage, a, _ = _two_quads()
        _add_uvs(a, "st", UVS_A)
        self._execute_json_string(stage, MERGE_ONLY)
        self.assertEqual(len(_get_meshes(stage)), 2)

    async def test_uvs_generated_for_the_mesh_without(self):
        """A mesh with no UVs gets some when another mesh in its group has them, and the meshes then merge."""
        stage, a, _ = _two_quads()
        _add_uvs(a, "st", UVS_A)
        self._execute_json_string(stage, CONFORM_THEN_MERGE)

        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 1)
        values = _flat_values(meshes[0], "st")
        self.assertEqual(len(values), 8)
        # The mesh that had UVs keeps them.
        for expected in UVS_A:
            self.assertIn(Gf.Vec2f(*expected), values)

    async def test_normals_generated_for_the_mesh_without(self):
        """A polygonal mesh with no normals gets some when another mesh has them, and the meshes then merge."""
        stage, a, b = _two_quads()
        a.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        b.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        a.CreateNormalsAttr([(0, 1, 0)] * 4)
        a.SetNormalsInterpolation(UsdGeom.Tokens.vertex)
        self._execute_json_string(stage, CONFORM_THEN_MERGE)

        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 1)
        values = _flat_values(meshes[0], "normals")
        # The authored normals survive, and the generated normals are the flat quad's.
        self.assertIn(Gf.Vec3f(0, 1, 0), values)
        self.assertIn(Gf.Vec3f(0, 0, 1), values)

    async def test_normals_not_generated_for_subdivision_surfaces(self):
        """A subdivision surface computes its own normals, so none are generated, and as merge then keeps the meshes
        apart, nothing else is changed to match."""
        stage, a, b = _two_quads()
        a.CreateNormalsAttr([(0, 1, 0)] * 4)
        a.SetNormalsInterpolation(UsdGeom.Tokens.vertex)
        a.CreateDoubleSidedAttr(True)
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.uniform).Set(
            Vt.FloatArray([2.5])
        )
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        self.assertFalse(b.GetNormalsAttr().HasAuthoredValue())
        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("normals").HasAuthoredValue())
        self.assertFalse(b.GetSubdivisionSchemeAttr().HasAuthoredValue())
        self.assertFalse(b.GetDoubleSidedAttr().HasAuthoredValue())
        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("weight").HasAuthoredValue())

    async def test_uv_names_are_made_the_same(self):
        """st on one mesh and st0 on another are renamed to a common name, keeping the values."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st", UVS_A)
        _add_uvs(b, "st0", UVS_B)
        self._execute_json_string(stage, CONFORM_THEN_MERGE)

        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 1)
        names = [p.GetBaseName() for p in UsdGeom.PrimvarsAPI(meshes[0]).GetPrimvarsWithAuthoredValues()]
        self.assertEqual(names, ["st"])
        values = _flat_values(meshes[0], "st")
        for expected in UVS_A + UVS_B:
            self.assertIn(Gf.Vec2f(*expected), values)

    async def test_uv_name_follows_the_majority(self):
        """When more meshes use st0 than st, the st mesh is renamed to st0, indices included."""
        stage, a, b = _two_quads()
        c = _define_quad(stage, "/World/c", 4)
        _add_uvs(a, "st", UVS_A)
        UsdGeom.PrimvarsAPI(a).GetPrimvar("st").SetIndices(Vt.IntArray([3, 2, 1, 0]))
        _add_uvs(b, "st0", UVS_B)
        _add_uvs(c, "st0", UVS_B)
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        self.assertEqual(
            [
                m.GetPrimvar("st0").IsDefined()
                for m in map(UsdGeom.PrimvarsAPI, (a.GetPrim(), b.GetPrim(), c.GetPrim()))
            ],
            [True, True, True],
        )
        self.assertFalse(UsdGeom.PrimvarsAPI(a.GetPrim()).GetPrimvar("st").IsDefined())
        self.assertFalse(a.GetPrim().HasAttribute("primvars:st:indices"))
        self.assertEqual(list(UsdGeom.PrimvarsAPI(a).GetPrimvar("st0").GetIndices()), [3, 2, 1, 0])

    async def test_generated_uvs_take_the_existing_name(self):
        """UVs generated for meshes without them take the existing UV set's name, however many meshes lack them."""
        stage, a, b = _two_quads()
        c = _define_quad(stage, "/World/c", 4)
        _add_uvs(a, "st0", UVS_A)
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        for mesh in (a, b, c):
            self.assertTrue(UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st0").HasAuthoredValue())
            self.assertFalse(UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st").IsDefined())

    async def test_uv_name_a_material_reads_is_kept(self):
        """A mesh whose material reads st0 keeps it, and the other mesh is renamed to st0 although the counts tie."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st", UVS_A)
        _add_uvs(b, "st0", UVS_B)
        _bind_uv_material(stage, "/World/Looks/ReadsSt0", b.GetPrim(), "st0", through_material_input=True)
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        for mesh in (a, b):
            self.assertTrue(UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st0").HasAuthoredValue())
            self.assertFalse(UsdGeom.PrimvarsAPI(mesh).GetPrimvar("st").IsDefined())

    async def test_uv_names_left_alone_when_materials_read_both(self):
        """When one mesh's material reads st and another's reads st0, neither mesh is renamed."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st", UVS_A)
        _add_uvs(b, "st0", UVS_B)
        subset = UsdGeom.Subset.CreateGeomSubset(
            a, "faces", UsdGeom.Tokens.face, Vt.IntArray([0]), UsdShade.Tokens.materialBind
        )
        _bind_uv_material(stage, "/World/Looks/ReadsSt", subset.GetPrim(), "st")
        _bind_uv_material(stage, "/World/Looks/ReadsSt0", b.GetPrim(), "st0")
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        self.assertTrue(UsdGeom.PrimvarsAPI(a).GetPrimvar("st").HasAuthoredValue())
        self.assertTrue(UsdGeom.PrimvarsAPI(b).GetPrimvar("st0").HasAuthoredValue())

    async def test_uv_set_with_animated_indices_is_not_renamed(self):
        """A UV set whose indices have time samples is not renamed, so the samples are kept."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st", UVS_A)
        _add_uvs(b, "st0", UVS_B)
        indices = UsdGeom.PrimvarsAPI(b).GetPrimvar("st0").CreateIndicesAttr()
        indices.Set(Vt.IntArray([0, 1, 2, 3]))
        indices.Set(Vt.IntArray([3, 2, 1, 0]), 1)
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("st").IsDefined())
        self.assertEqual(UsdGeom.PrimvarsAPI(b).GetPrimvar("st0").GetIndicesAttr().GetTimeSamples(), [1.0])

    async def test_missing_primvar_gets_the_supplied_default(self):
        """A primvar one mesh lacks is added with the value the caller gave."""
        stage, a, _ = _two_quads()
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.uniform).Set(
            Vt.FloatArray([2.5])
        )
        self._execute_json_string(
            stage, '[{"operation": "conformMeshes", "primvarDefaults": ["weight=7"]}, {"operation": "merge"}]'
        )

        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 1)
        self.assertEqual(sorted(_flat_values(meshes[0], "weight")), [2.5, 7.0])

    async def test_missing_primvar_without_default_gets_zero(self):
        """A primvar one mesh lacks is added as zero when the caller gave no default."""
        stage, a, _ = _two_quads()
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.uniform).Set(
            Vt.FloatArray([2.5])
        )
        self._execute_json_string(stage, CONFORM_THEN_MERGE)

        self.assertEqual(sorted(_flat_values(_get_meshes(stage)[0], "weight")), [0.0, 2.5])

    async def test_default_out_of_range_is_not_used(self):
        """A supplied default the primvar's type cannot hold is not used, and the primvar is not added."""
        stage, a, b = _two_quads()
        primvars = UsdGeom.PrimvarsAPI(a)
        primvars.CreatePrimvar("count", Sdf.ValueTypeNames.IntArray, UsdGeom.Tokens.constant).Set(Vt.IntArray([5]))
        primvars.CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.constant).Set(
            Vt.FloatArray([2.5])
        )
        self._execute_json_string(
            stage, '[{"operation": "conformMeshes", "primvarDefaults": ["count=2147483648", "weight=inf"]}]'
        )

        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("count").IsDefined())
        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("weight").IsDefined())

    async def test_primvar_with_element_size_is_left_alone(self):
        """A primvar with an element size above 1 is not added to the mesh that lacks it."""
        stage, a, b = _two_quads()
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("w", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.constant, 3).Set(
            Vt.FloatArray([1, 2, 3])
        )
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("w").IsDefined())

    async def test_primvar_with_different_types_is_left_alone(self):
        """A primvar with the same name but different types on two meshes is not changed."""
        stage, a, b = _two_quads()
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.uniform).Set(
            Vt.FloatArray([2.5])
        )
        UsdGeom.PrimvarsAPI(b).CreatePrimvar("weight", Sdf.ValueTypeNames.IntArray, UsdGeom.Tokens.uniform).Set(
            Vt.IntArray([3])
        )
        _, result = self._execute_command({"meshPrimPaths": []}, context=_get_context(stage))

        self.assertTrue(result[0])
        self.assertEqual(list(UsdGeom.PrimvarsAPI(a).GetPrimvar("weight").Get()), [2.5])
        self.assertEqual(list(UsdGeom.PrimvarsAPI(b).GetPrimvar("weight").Get()), [3])

    async def test_double_sided_made_the_same(self):
        """Where doubleSided differs it is set to true on every mesh, and the meshes merge."""
        stage, a, b = _two_quads()
        a.CreateDoubleSidedAttr(True)
        b.CreateDoubleSidedAttr(False)
        self._execute_json_string(stage, CONFORM_THEN_MERGE)

        meshes = _get_meshes(stage)
        self.assertEqual(len(meshes), 1)
        self.assertTrue(UsdGeom.Mesh(meshes[0]).GetDoubleSidedAttr().Get())

    async def test_orientation_and_subdivision_differences_are_left_alone(self):
        """Orientation and subdivision scheme cannot be reconciled, so the meshes stay apart and unchanged."""
        stage, a, b = _two_quads()
        a.CreateOrientationAttr(UsdGeom.Tokens.leftHanded)
        b.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        self._execute_json_string(stage, CONFORM_THEN_MERGE)

        self.assertEqual(len(_get_meshes(stage)), 2)
        self.assertEqual(a.GetOrientationAttr().Get(), UsdGeom.Tokens.leftHanded)
        self.assertEqual(b.GetSubdivisionSchemeAttr().Get(), UsdGeom.Tokens.none)

    async def test_meshes_kept_apart_are_not_changed_to_match(self):
        """A mesh that merge keeps apart for another reason does not make the other meshes take its primvars."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st", UVS_A)
        apart = _define_quad(stage, "/World/c", 4)
        apart.CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)
        apart.CreateDisplayColorPrimvar().Set([Gf.Vec3f(1, 0, 0)])
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        # b still gets UVs to match a, its merge partner, but neither takes displayColor from c.
        self.assertTrue(UsdGeom.PrimvarsAPI(b).GetPrimvar("st").HasAuthoredValue())
        self.assertFalse(a.GetDisplayColorPrimvar().HasAuthoredValue())
        self.assertFalse(b.GetDisplayColorPrimvar().HasAuthoredValue())
        self.assertFalse(UsdGeom.PrimvarsAPI(apart).GetPrimvar("st").HasAuthoredValue())

    async def test_partial_second_uv_set_keeps_meshes_apart(self):
        """st1 on only some meshes cannot be reconciled, so those meshes are not changed to match each other."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st1", UVS_A)
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.uniform).Set(
            Vt.FloatArray([2.5])
        )
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')

        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("weight").HasAuthoredValue())
        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("st1").HasAuthoredValue())

    async def test_normals_keep_meshes_apart_when_not_generated(self):
        """With generateNormals off, normals on only some meshes keep them apart, so nothing is filled across."""
        stage, a, b = _two_quads()
        a.CreateNormalsAttr([(0, 0, 1)] * 4)
        a.SetNormalsInterpolation(UsdGeom.Tokens.vertex)
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.uniform).Set(
            Vt.FloatArray([2.5])
        )
        self._execute_json_string(stage, '[{"operation": "conformMeshes", "generateNormals": false}]')

        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("weight").HasAuthoredValue())
        self.assertFalse(b.GetNormalsAttr().HasAuthoredValue())
        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("normals").HasAuthoredValue())

    async def test_generation_can_be_turned_off(self):
        """With generateUVs off, a mesh without UVs is not given any, nor other primvars, as merge keeps it apart."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st", UVS_A)
        UsdGeom.PrimvarsAPI(a).CreatePrimvar("weight", Sdf.ValueTypeNames.FloatArray, UsdGeom.Tokens.uniform).Set(
            Vt.FloatArray([2.5])
        )
        self._execute_json_string(stage, '[{"operation": "conformMeshes", "generateUVs": false}]')

        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("st").IsDefined())
        self.assertFalse(UsdGeom.PrimvarsAPI(b).GetPrimvar("weight").IsDefined())

    async def test_nothing_changes_when_meshes_already_agree(self):
        """Meshes that already agree are left untouched, and a second run changes nothing."""
        stage, a, b = _two_quads()
        _add_uvs(a, "st", UVS_A)
        _add_uvs(b, "st", UVS_B)
        before = stage.GetRootLayer().ExportToString()
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)

    async def test_second_run_changes_nothing(self):
        """Running conformMeshes on its own output changes nothing."""
        stage, a, _ = _two_quads()
        _add_uvs(a, "st", UVS_A)
        a.CreateDoubleSidedAttr(True)
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')
        after_first = stage.GetRootLayer().ExportToString()
        self._execute_json_string(stage, '[{"operation": "conformMeshes"}]')
        self.assertEqual(stage.GetRootLayer().ExportToString(), after_first)

    async def test_report_names_each_change_and_each_difference(self):
        """The log names every change, every difference left alone, and every group needing nothing."""
        self.assertIsNotNone(_CLI, "usdOptimize binary not found")
        stage = Usd.Stage.CreateInMemory()
        _define_quad(stage, "/World/Solo/m", 0)
        _define_quad(stage, "/World/Same/a", 0)
        _define_quad(stage, "/World/Same/b", 2)
        _add_uvs(_define_quad(stage, "/World/Fix/a", 0), "st", UVS_A)
        _define_quad(stage, "/World/Fix/b", 2)
        _define_quad(stage, "/World/Odd/a", 0).CreateOrientationAttr(UsdGeom.Tokens.leftHanded)
        _define_quad(stage, "/World/Odd/b", 2)
        for group in ("Bad", "Smooth"):
            with_normals = _define_quad(stage, f"/World/{group}/a", 0)
            with_normals.CreateNormalsAttr([(0, 0, 1)] * 4)
            with_normals.SetNormalsInterpolation(UsdGeom.Tokens.vertex)
            _define_quad(stage, f"/World/{group}/b", 2)
        # generateNormals fails on a face index out of range.
        stage.GetPrimAtPath("/World/Bad/b").GetAttribute("faceVertexIndices").Set(Vt.IntArray([0, 1, 2, 7]))
        for path in ("/World/Bad/a", "/World/Bad/b"):
            UsdGeom.Mesh.Get(stage, path).CreateSubdivisionSchemeAttr(UsdGeom.Tokens.none)

        with tempfile.TemporaryDirectory() as tmp:
            input_path = os.path.join(tmp, "input.usda")
            stage.GetRootLayer().Export(input_path)
            result = subprocess.run(
                [_CLI, "-i", input_path, "-o", "conformMeshes", "-a", "mergePoint=8", "-v"],
                stdout=subprocess.PIPE,
                stderr=subprocess.STDOUT,
                text=True,
            )

        self.assertEqual(result.returncode, 0, result.stdout)
        for expected in (
            "Left alone (only mesh in its merge boundary): /World/Solo/m",
            "Left alone (meshes already compatible): merge boundary /World/Same",
            "/World/Fix/b: generated UVs",
            "merge boundary /World/Odd: not reconciled: meshes kept apart by orientation, in 2 groups",
            "Left alone (no mesh in its merge boundary it can merge with): /World/Odd/a",
            "/World/Bad/b: not reconciled: could not generate normals",
            "merge boundary /World/Smooth: not reconciled: normals missing from 1 mesh, "
            "not generated for subdivisionScheme catmullClark",
            "Conformed 4 groups (1 already compatible, 3 meshes left alone with no mesh to merge with): "
            "1 normals or UV sets generated, 0 primvars renamed, 0 primvars added, 0 schema values changed, "
            "3 differences not reconciled",
        ):
            self.assertIn(expected, result.stdout)

    async def test_original_prim_leaves_point_instancer_mesh_prototype(self):
        """Conforming the whole stage at Original Prim leaves a point instancer's mesh prototype in place."""
        stage = self._open_stage("pointInstancerMeshPrototype.usda")
        status = self._execute_json_string(stage, '[{"operation": "conformMeshes", "mergePoint": 9}]')
        self.assertTrue(status[0][0], status[0][1])
        self.assertTrue(stage.GetPrimAtPath("/PointInstancer/Mesh"))
