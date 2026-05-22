# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Usd, UsdGeom

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


class Test_Operation_Atlas_UVs(Test_Operation):

    OPERATION = "generateAtlasUVs"

    async def test_AtlasUVs(self):
        """Test atlas UVs"""
        stage = self._open_stage("meshNoUVs.usd")

        # Test original number of UVs
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 0)
        self.assertEqual(num_st_indices, 0)

        # Compute atlas UVs
        self._execute_json(stage, "generateAtlasUVs.json")

        # Test new number of UVs. Expect 14 unique values and 24 indices on a cube.
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 14)
        self.assertEqual(num_st_indices, 24)

    async def test_AtlasUVs_command(self):
        """Test atlas UVs"""
        stage = self._open_stage("meshNoUVs.usd")

        # Test original number of UVs
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 0)
        self.assertEqual(num_st_indices, 0)

        # Compute atlas UVs via command
        args = {
            "paths": [],
            "distortionThreshold": 3.0,
            "enableAtlasPacking": True,
            "useWorldSpaceScales": True,
            "scaleFactor": 0.01,
            "scaleUnits": 0.0,
            "overwriteExisting": False,
        }
        self._execute_command(args)

        # Test new number of UVs. Expect 14 unique values and 24 indices on a cube.
        num_st, num_st_indices, st_vals = _tot_st_count(stage)
        self.assertEqual(num_st, 14)
        self.assertEqual(num_st_indices, 24)

        # reopen the stage
        stage = self._open_stage("meshNoUVs.usd")

        # Compute atlas UVs again using a scaleUnit this time
        args = {
            "paths": [],
            "distortionThreshold": 3.0,
            "enableAtlasPacking": True,
            "useWorldSpaceScales": True,
            "scaleFactor": 0.01,
            "scaleUnits": 1.0,
            "overwriteExisting": False,
        }
        self._execute_command(args)

        # Test new number of UVs. Expect 14 unique values and 24 indices on a cube but expect them to be different to
        # previously generated uvs
        num_st, num_st_indices, st_vals_with_unit = _tot_st_count(stage)
        self.assertEqual(num_st, 14)
        self.assertEqual(num_st_indices, 24)

        self.assertEqual(len(st_vals_with_unit), len(st_vals))
        self.assertNotEqual(st_vals_with_unit, st_vals)

    async def test_time_varying_mesh(self):
        """Test atlas uvs operation on a mesh with time varying attributes"""
        stage = self._open_stage("time_varying_mesh.usd")

        args = {
            "paths": [],
            "distortionThreshold": 3.0,
            "enableAtlasPacking": True,
            "useWorldSpaceScales": True,
            "scaleFactor": 0.01,
            "scaleUnits": 0.0,
            "overwriteExisting": True,
        }

        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)
