# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Gf, UsdGeom

from .test_utils import Test_Operation, _get_meshes

# SubdivideOperation::Method values
eCATMULL_CLARK = 0
eLOOP = 1

quad_cube_counts = (6, 24)  # (faces, sides)

# Default arguments for the command
DEFAULT_ARGS = {
    "paths": [],
    "gpuFaceCountThreshold": 4000,
    "faceCountLimit": 2000000,
    "method": eCATMULL_CLARK,
    "iterationCount": 1,
}


# The number of sides per face after subdivision
def _subdiv_face_side_count(method):
    if method == eCATMULL_CLARK:
        return 4

    if method == eLOOP:
        return 3

    return 0  # unknown method


# Calculates subdivided face count.  Takes into account triangulation of initial
# mesh for Loop method.
def _subdiv_face_count(method, iteration_count, initial_counts):
    initial_face_count = initial_counts[0]
    initial_side_count = initial_counts[1]

    if iteration_count <= 0:  # negative value nonsensical, just consider it 0
        return initial_face_count

    if method == eCATMULL_CLARK:
        return 4 ** (iteration_count - 1) * initial_side_count

    if method == eLOOP:
        return 4**iteration_count * (initial_side_count - 2 * initial_face_count)

    return 0  # unknown method


class Test_Operation_SubdivideMeshes(Test_Operation):

    OPERATION = "subdivideMeshes"

    async def test_subdivide_mesh(self):
        """Test subdivide meshes on a quad mesh"""

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                stage = self._open_stage("simpleCube.usda")
                before_meshes = _get_meshes(stage)
                self.assertEqual(len(before_meshes), 1)
                before_mesh = UsdGeom.Mesh(before_meshes[0])
                self.assertTrue(before_mesh)
                before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
                self.assertEqual(len(before_face_sizes), 6)
                for face_size in before_face_sizes:
                    self.assertEqual(face_size, 4)

                args = DEFAULT_ARGS.copy()
                args["method"] = method
                args["iterationCount"] = iteration_count
                success, result = self._execute_command(args)

                # The operation should execute successfully.
                self.assertTrue(success)

                after_meshes = _get_meshes(stage)
                self.assertEqual(len(after_meshes), 1)

                # Assert that the quads have been subdivided
                after_mesh = UsdGeom.Mesh(after_meshes[0])
                self.assertTrue(after_mesh)
                after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
                self.assertEqual(len(after_face_sizes), _subdiv_face_count(method, iteration_count, quad_cube_counts))
                for face_size in after_face_sizes:
                    self.assertEqual(face_size, _subdiv_face_side_count(method))

    async def test_subdivide_mesh_gpu(self):
        """Test subdivide meshes on a quad mesh with gpu"""

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                stage = self._open_stage("simpleCube.usda")
                before_meshes = _get_meshes(stage)
                self.assertEqual(len(before_meshes), 1)
                before_mesh = UsdGeom.Mesh(before_meshes[0])
                self.assertTrue(before_mesh)
                before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
                self.assertEqual(len(before_face_sizes), 6)
                for face_size in before_face_sizes:
                    self.assertEqual(face_size, 4)
                args = DEFAULT_ARGS.copy()
                args["gpuFaceCountThreshold"] = 1  # forces the gpu algo to be chosen
                args["method"] = method
                args["iterationCount"] = iteration_count
                success, result = self._execute_command(args)
                # The operation should execute successfully.
                self.assertTrue(success)
                after_meshes = _get_meshes(stage)
                self.assertEqual(len(after_meshes), 1)
                # Assert that the quads have been subdivided
                after_mesh = UsdGeom.Mesh(after_meshes[0])
                self.assertTrue(after_mesh)
                after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
                self.assertEqual(len(after_face_sizes), _subdiv_face_count(method, iteration_count, quad_cube_counts))
                for face_size in after_face_sizes:
                    self.assertEqual(face_size, _subdiv_face_side_count(method))

    async def test_subdivide_sharp_features(self):
        """Test subdivide meshes on a quad mesh"""

        subd_corner_indices = [
            # Catmull-Clark, 1 iteration and 2 iterations
            [[17], [53]],
            # Loop, 1 iteration and 2 iterations
            [[17], [53]],
        ]

        subd_corner_sharpnesses = [
            # Catmull-Clark, 1 iteration and 2 iterations
            [[3.0], [2.0]],
            # Loop, 1 iteration and 2 iterations
            [[3.0], [2.0]],
        ]

        subd_crease_indices = [
            # Catmull-Clark, 1 iteration and 2 iterations
            [[0, 1, 0, 3, 1, 4, 3, 8, 4, 5, 5, 6, 6, 7, 7, 8], []],
            # Loop, 1 iteration and 2 iterations
            [[0, 1, 0, 8, 1, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8], []],
        ]

        subd_crease_lengths = [
            # Catmull-Clark, 1 iteration and 2 iterations
            [[2, 2, 2, 2, 2, 2, 2, 2], []],
            # Loop, 1 iteration and 2 iterations
            [[2, 2, 2, 2, 2, 2, 2, 2], []],
        ]

        subd_crease_sharpnesses = [
            # Catmull-Clark, 1 iteration and 2 iterations
            [[0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3], []],
            # Loop, 1 iteration and 2 iterations
            [[0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3, 0.3], []],
        ]

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                stage = self._open_stage("simpleCubeSharpSubd.usda")
                before_meshes = _get_meshes(stage)
                self.assertEqual(len(before_meshes), 1)
                before_mesh = UsdGeom.Mesh(before_meshes[0])
                self.assertTrue(before_mesh)
                before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
                self.assertEqual(len(before_face_sizes), 6)
                for face_size in before_face_sizes:
                    self.assertEqual(face_size, 4)

                args = DEFAULT_ARGS.copy()
                args["method"] = method
                args["iterationCount"] = iteration_count
                success, result = self._execute_command(args)

                # The operation should execute successfully.
                self.assertTrue(success)

                after_meshes = _get_meshes(stage)
                self.assertEqual(len(after_meshes), 1)

                # Assert that the quads have been subdivided
                after_mesh = UsdGeom.Mesh(after_meshes[0])
                self.assertTrue(after_mesh)

                corner_indices = after_mesh.GetCornerIndicesAttr().Get()
                if not corner_indices:
                    corner_indices = []
                corner_sharpnesses = after_mesh.GetCornerSharpnessesAttr().Get()
                if not corner_sharpnesses:
                    corner_sharpnesses = []
                crease_indices = after_mesh.GetCreaseIndicesAttr().Get()
                if not crease_indices:
                    crease_indices = []
                crease_lengths = after_mesh.GetCreaseLengthsAttr().Get()
                if not crease_lengths:
                    crease_lengths = []
                crease_sharpnesses = after_mesh.GetCreaseSharpnessesAttr().Get()
                if not crease_sharpnesses:
                    crease_sharpnesses = []

                subd_values = subd_corner_indices[method][iteration_count - 1]
                self.assertEqual(len(corner_indices), len(subd_values))
                for i in range(len(corner_indices)):
                    self.assertEqual(corner_indices[i], subd_values[i])

                subd_values = subd_corner_sharpnesses[method][iteration_count - 1]
                self.assertEqual(len(corner_sharpnesses), len(subd_values))
                for i in range(len(corner_sharpnesses)):
                    self.assertTrue(Gf.IsClose(corner_sharpnesses[i], subd_values[i], 1.2e-7))

                subd_values = subd_crease_indices[method][iteration_count - 1]
                self.assertEqual(len(crease_indices), len(subd_values))
                for i in range(len(crease_indices)):
                    self.assertEqual(crease_indices[i], subd_values[i])

                subd_values = subd_crease_lengths[method][iteration_count - 1]
                self.assertEqual(len(crease_lengths), len(subd_values))
                for i in range(len(crease_lengths)):
                    self.assertEqual(crease_lengths[i], subd_values[i])

                subd_values = subd_crease_sharpnesses[method][iteration_count - 1]
                self.assertEqual(len(crease_sharpnesses), len(subd_values))
                for i in range(len(crease_sharpnesses)):
                    self.assertTrue(Gf.IsClose(crease_sharpnesses[i], subd_values[i], 1.2e-7))

    async def test_subdivide_mesh_over_face_limit(self):
        """Test subdivide meshes on a quad mesh"""

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                stage = self._open_stage("simpleCube.usda")
                before_meshes = _get_meshes(stage)
                self.assertEqual(len(before_meshes), 1)
                before_mesh = UsdGeom.Mesh(before_meshes[0])
                self.assertTrue(before_mesh)
                before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
                self.assertEqual(len(before_face_sizes), 6)
                for face_size in before_face_sizes:
                    self.assertEqual(face_size, 4)

                args = DEFAULT_ARGS.copy()
                args["faceCountLimit"] = 50  # will be exceeded by both C-C and Loop for iteration_count > 1
                args["method"] = method
                args["iterationCount"] = iteration_count
                success, result = self._execute_command(args)

                # The operation should execute successfully.
                self.assertTrue(success)

                after_meshes = _get_meshes(stage)
                self.assertEqual(len(after_meshes), 1)

                # Assert that the quads have been subdivided iff iteration_count = 1
                after_mesh = UsdGeom.Mesh(after_meshes[0])
                self.assertTrue(after_mesh)
                after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
                if iteration_count == 1:
                    self.assertEqual(
                        len(after_face_sizes), _subdiv_face_count(method, iteration_count, quad_cube_counts)
                    )
                else:
                    self.assertEqual(len(after_face_sizes), quad_cube_counts[0])

    async def test_subdivide_all_mesh_prims(self):
        """Test subdivide all mesh prims"""

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                stage = self._open_stage("simpleFourCubes.usda")
                before_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
                self.assertEqual(len(before_meshes), 4)
                for before_mesh in before_meshes:
                    self.assertTrue(before_mesh)
                    before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
                    self.assertEqual(len(before_face_sizes), 6)
                    for face_size in before_face_sizes:
                        self.assertEqual(face_size, 4)

                args = DEFAULT_ARGS.copy()
                args["method"] = method
                args["iterationCount"] = iteration_count
                success, result = self._execute_command(args)

                # The operation should execute successfully.
                self.assertTrue(success)

                after_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
                self.assertEqual(len(after_meshes), 4)

                # Assert that the quads have been subdivided
                for after_mesh in after_meshes:
                    self.assertTrue(after_mesh)
                    after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
                    self.assertEqual(
                        len(after_face_sizes), _subdiv_face_count(method, iteration_count, quad_cube_counts)
                    )
                    for face_size in after_face_sizes:
                        self.assertEqual(face_size, _subdiv_face_side_count(method))

    async def test_subdivide_one_mesh_prim(self):
        """Test subdivide specific mesh prims"""

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                stage = self._open_stage("simpleFourCubes.usda")
                before_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
                self.assertEqual(len(before_meshes), 4)
                for before_mesh in before_meshes:
                    self.assertTrue(before_mesh)
                    before_face_sizes = before_mesh.GetFaceVertexCountsAttr().Get()
                    self.assertEqual(len(before_face_sizes), 6)
                    for face_size in before_face_sizes:
                        self.assertEqual(face_size, 4)

                args = DEFAULT_ARGS.copy()
                args["paths"] = [str(before_meshes[2].GetPath())]
                args["method"] = method
                args["iterationCount"] = iteration_count
                success, result = self._execute_command(args)

                # The operation should execute successfully.
                self.assertTrue(success)

                after_meshes = [UsdGeom.Mesh(x) for x in _get_meshes(stage)]
                self.assertEqual(len(after_meshes), 4)

                # Assert that only the specified mesh has been subdivided
                for after_mesh in after_meshes:
                    self.assertTrue(after_mesh)
                    after_face_sizes = after_mesh.GetFaceVertexCountsAttr().Get()
                    if str(after_mesh.GetPath()) == args["paths"][0]:
                        self.assertEqual(
                            len(after_face_sizes), _subdiv_face_count(method, iteration_count, quad_cube_counts)
                        )
                        for face_size in after_face_sizes:
                            self.assertEqual(face_size, _subdiv_face_side_count(method))
                    else:
                        self.assertEqual(len(after_face_sizes), 6)
                        for face_size in after_face_sizes:
                            self.assertEqual(face_size, 4)

    async def test_subdivide_time_varying_meshes(self):
        """Test subdivide meshes operation on meshes with authored time varying attributes"""

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                # Get a copy of the default arguments for this command
                args = DEFAULT_ARGS.copy()
                args["method"] = method
                args["iterationCount"] = iteration_count
                # Open the stage
                stage = self._open_stage("time_varying_meshes.usd")
                # run command
                success, result = self._execute_command(args)

                # asserts success of execution
                self.assertTrue(success)

    async def test_subdivide_time_varying_meshes_paths(self):
        """Test subdivide meshes operation on meshes with authored time varying attributes using paths"""

        for method in [eCATMULL_CLARK, eLOOP]:
            for iteration_count in [1, 2]:
                # Get a copy of the default arguments for this command
                args = DEFAULT_ARGS.copy()
                args["method"] = method
                args["iterationCount"] = iteration_count
                # settings paths so that _removePrimsWithAuthoredTimeSamples is invoked
                args["paths"] = [
                    "/Additional_Assets/sm_warehousecomposition_n32_01/sm_palletcomposition_a52_04/SM_LongBox_A10_58/SM_LongBox_A10_Body_01",
                    "/Additional_Assets/sm_warehousecomposition_n32_01/sm_palletcomposition_a52_04/SM_LongBox_A10_58/SM_LongBox_A10_Decal_01",
                    "/Additional_Assets/sm_warehousecomposition_n32_01/sm_palletcomposition_a52_04/SM_LongBox_A10_58/SM_LongBox_A10_Scotch_01",
                ]
                # Open the stage
                stage = self._open_stage("time_varying_meshes.usd")
                # run command
                success, result = self._execute_command(args)

                # asserts success of execution
                self.assertTrue(success)
