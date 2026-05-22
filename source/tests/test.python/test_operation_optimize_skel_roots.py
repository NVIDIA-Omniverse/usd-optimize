# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from omni.scene.optimizer.core.scripts import standalone
from pxr import Gf, Sdf, Usd, UsdGeom, UsdSkel

from .test_utils import Test_Operation


def _get_worldspace_baked_skinning_points(usdSkelRoot, time):
    """Returns a map of the skinned point values of meshes in worldspace keyed by prim path"""
    result = dict()

    # Bake skinning of all skinned meshes below the UsdSkel Root at the given time.
    UsdSkel.BakeSkinning(usdSkelRoot, Gf.Interval(time.GetValue()))

    # Construct an xform cache for the given time to speed up local to world calculations.
    xformCache = UsdGeom.XformCache(time)

    # Iterate over the Mesh prims below the UsdSkel Root.
    for prim in Usd.PrimRange(usdSkelRoot.GetPrim()):
        if prim.GetTypeName() != "Mesh":
            continue

        # Get the local to world transform matrix so we can transform the point values.
        localToWorld = xformCache.GetLocalToWorldTransform(prim)

        # Get the points then iterate over them applying the local to world transform.
        points = UsdGeom.Mesh(prim).GetPointsAttr().Get(time)
        for index in range(len(points)):
            points[index] = Gf.Vec3f(localToWorld.Transform(points[index]))

        # Add the point to the result map keyed on the prim path.
        result[prim.GetPath()] = points

    return result


class Test_Operation_Optimize_Skel_Roots(Test_Operation):

    OPERATION = "optimizeSkelRoots"

    async def test_command(self):
        """"""
        # Default arguments for the command are currently empty but we need to pass them in anyway.
        args = dict()

        # Open the stage and get the point values for the meshes after the skinning has been baked and worldspace
        # transform has been applied.
        stage = self._open_stage("pixarUsdSkelExample.usdc")
        usdSkelRoot = UsdSkel.Root.Get(stage, "/HumanFemale_Group")
        point_by_path_before = _get_worldspace_baked_skinning_points(usdSkelRoot, Usd.TimeCode(120))

        # Re-open the stage (to revert the bake skinning changes) and optimize skeletons.
        stage = self._open_stage("pixarUsdSkelExample.usdc")
        self._execute_command(args)

        # There should be 13 meshes, 12 merged meshes and 1 original mesh that we skipped because it has blend shapes.
        meshes = [x for x in stage.Traverse() if x.GetTypeName() == "Mesh"]
        self.assertEqual(len(meshes), 13)

        # Get the point values for the merged meshes after the skinning has been baked and worldspace
        # transform has been applied.
        usdSkelRoot = UsdSkel.Root.Get(stage, "/HumanFemale_Group")
        point_by_path_after = _get_worldspace_baked_skinning_points(usdSkelRoot, Usd.TimeCode(120))

        # Given a hardcoded lookup of input prims to output prim with the expected order.
        lookup_table = {
            "/HumanFemale_Group/merged": [
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/LEye/Pupil_sbdv",
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/LEye/Iris_sbdv",
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/LEye/Sclera_sbdv",
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/REye/Pupil_sbdv",
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/REye/Iris_sbdv",
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/REye/Sclera_sbdv",
            ],
            "/HumanFemale_Group/merged_1": [
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/LEye/Cornea_sbdv",
                "/HumanFemale_Group/HumanFemale/Geom/Face/Eyes/REye/Cornea_sbdv",
            ],
            "/HumanFemale_Group/merged_2": [
                "/HumanFemale_Group/HumanFemaleHair/Geom/Hair/Layers/HeadHair/BetaLeft_HairLayer/Standin/Shell_sbdv",
                "/HumanFemale_Group/HumanFemaleHair/Geom/Hair/Layers/HeadHair/BetaRight_HairLayer/Standin/Shell_sbdv",
                "/HumanFemale_Group/HumanFemaleHair/Geom/Hair/Layers/EyeHair/BrowL_HairLayer/Standin/Shell_sbdv",
                "/HumanFemale_Group/HumanFemaleHair/Geom/Hair/Layers/EyeHair/BrowR_HairLayer/Standin/Shell_sbdv",
            ],
            "/HumanFemale_Group/merged_3": [
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/ButtonDownRenderMesh_sbdv",
                "/HumanFemale_Group/KidThinLeggings/Geom/Render/LeggingsRenderMesh_sbdv",
                "/HumanFemale_Group/KidThinSkirt/Geom/Render/SkirtRenderMesh_sbdv",
            ],
            "/HumanFemale_Group/merged_4": [
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button1/Button1_sbdv/Button1_Button_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button1/Button1_sbdv/Button1_Thread1_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button1/Button1_sbdv/Button1_Thread2_sbdv",
            ],
            "/HumanFemale_Group/merged_5": [
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button2/Button2_sbdv/Button2_Button_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button2/Button2_sbdv/Button2_Thread1_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button2/Button2_sbdv/Button2_Thread2_sbdv",
            ],
            "/HumanFemale_Group/merged_6": [
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button3/Button3_sbdv/Button3_Button_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button3/Button3_sbdv/Button3_Thread1_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button3/Button3_sbdv/Button3_Thread2_sbdv",
            ],
            "/HumanFemale_Group/merged_7": [
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button4/Button4_sbdv/Button4_Button_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button4/Button4_sbdv/Button4_Thread1_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button4/Button4_sbdv/Button4_Thread2_sbdv",
            ],
            "/HumanFemale_Group/merged_8": [
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button5/Button5_sbdv/Button5_Button_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button5/Button5_sbdv/Button5_Thread1_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button5/Button5_sbdv/Button5_Thread2_sbdv",
            ],
            "/HumanFemale_Group/merged_9": [
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button6/Button6_sbdv/Button6_Button_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button6/Button6_sbdv/Button6_Thread1_sbdv",
                "/HumanFemale_Group/KidThinButtonDown/Geom/Render/Buttons/Button6/Button6_sbdv/Button6_Thread2_sbdv",
            ],
            "/HumanFemale_Group/merged_10": [
                "/HumanFemale_Group/ShoesHumanFlats/Geom/LShoe/Body/ShoeBody_sbdv",
                "/HumanFemale_Group/ShoesHumanFlats/Geom/LShoe/Body/HeelSeam_sbdv",
                "/HumanFemale_Group/ShoesHumanFlats/Geom/LShoe/Sole/Sole_sbdv",
                "/HumanFemale_Group/ShoesHumanFlats/Geom/RShoe/Body/ShoeBody_sbdv",
                "/HumanFemale_Group/ShoesHumanFlats/Geom/RShoe/Body/HeelSeam_sbdv",
                "/HumanFemale_Group/ShoesHumanFlats/Geom/RShoe/Sole/Sole_sbdv",
            ],
            "/HumanFemale_Group/merged_11": [
                "/HumanFemale_Group/SocksHuman/Geom/LSock/AnkleSock_sbdv",
                "/HumanFemale_Group/SocksHuman/Geom/RSock/AnkleSock_sbdv",
            ],
        }

        # Iterate over the lookup table asserting that the merge meshes produce the same baked result.
        for after_path, before_paths in lookup_table.items():

            # Get the baked points for the output mesh.
            after_points = point_by_path_after[Sdf.Path(after_path)]

            # Track a rolling start index for slicing the data.
            start_index = 0

            # Iterate over the input prim paths comparing their baked points with the slice that the represent in the
            # output mesh.
            for before_path in before_paths:

                # Get before and after points.
                expected = point_by_path_before[Sdf.Path(before_path)]
                returned = after_points[start_index : start_index + len(expected)]

                # Do a value by value compare of the points so that we can support a margin of error.
                # The precision difference seems to be coming from un-normalized input data.
                are_equal = True
                digits = 3
                for expected_point, returned_point in zip(expected, returned):
                    for index in range(3):
                        if round(expected_point[index] - returned_point[index], digits) != 0:  # pragma: no cover
                            # Shouldn't happen!
                            are_equal = False
                            break

                # Assert that the points values are equal.
                msg = 'Skinned points differ "{}"'.format(before_path)
                self.assertTrue(are_equal, msg)

                # Update the rolling start index.
                start_index += len(expected)

    async def test_json(self):
        """Test optimize skel roots via JSON"""

        stage = self._open_stage("pixarUsdSkelExample.usdc")

        # Execute
        json = """[{"operation": "optimizeSkelRoots"}]"""
        status = standalone.execute_commands_from_json(stage, json)
        self.assertTrue(status)

        # Basic test that things roughly worked (tested properly in test_command)
        meshes = [x for x in stage.Traverse() if x.GetTypeName() == "Mesh"]
        self.assertEqual(len(meshes), 13)
