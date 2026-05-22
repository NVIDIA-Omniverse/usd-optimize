# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Sdf

from .test_utils import Test_Operation, _get_context

# Default arguments for the command
DEFAULT_ARGS = {
    "meshPrimPaths": [],
    "tolerance": 0.001,
    "offset": 0.0,
    "fuzzy": False,
}


class Test_Command_Find_Coinciding_Geometry(Test_Operation):

    # Note: this is intentionally the "old" name of the operation,
    #       meaning we also test remapping operation names. Likewise,
    #       meshPrimPaths above is also old (now primPaths).
    OPERATION = "findCoincidingMeshes"

    async def test_basic_result(self):
        """Check that the return result matches expectation"""
        # Open the stage and get all the prims
        stage = self._open_stage("coinciding_meshes.usda")

        context = _get_context(stage, report=True)

        # Execute the command and assert success
        args = DEFAULT_ARGS.copy()
        args["tolerance"] = 0.5
        success, opResult = self._execute_command(args, context)

        self.assertTrue(success)

        result = opResult[2]

        # With we should have a single set of coinciding prims
        self.assertEqual(len(result), 1)

        # Ensure that the currently expected cases are covered.
        expected = [
            Sdf.Path("/World/CoincidingMesh1"),
            Sdf.Path("/World/CoincidingMesh2"),
            Sdf.Path("/World/CoincidingMesh3DiffNormals"),
        ]
        returned = result[0]
        self.assertEqual(expected, returned)

        # We expect Meshes that have coinciding points but different normals to be considered coincidental.
        self.assertIn(Sdf.Path("/World/CoincidingMesh3DiffNormals"), returned)

        # Increasing the tolerance value should result in meshes whos points are within that distance being considered
        # conincidental. In this case an identical Mesh with a transform of 1.0 is returned that was not before.
        args["tolerance"] = 1.0
        success, opResult = self._execute_command(args)
        self.assertTrue(success)

        result = opResult[2]

        # Ensure that the currently expected cases are covered.
        expected = [
            Sdf.Path("/World/CoincidingMesh1"),
            Sdf.Path("/World/CoincidingMesh2"),
            Sdf.Path("/World/CoincidingMesh3DiffNormals"),
            Sdf.Path("/World/NotCoinciding"),
        ]
        returned = result[0]
        self.assertEqual(expected, returned)

        # Test with a recursive path
        args["meshPrimPaths"] = ["/World//"]

        success, opResult = self._execute_command(args)
        self.assertTrue(success)

        result = opResult[2]

        # Ensure that the currently expected cases are covered.
        expected = [
            Sdf.Path("/World/CoincidingMesh1"),
            Sdf.Path("/World/CoincidingMesh2"),
            Sdf.Path("/World/CoincidingMesh3DiffNormals"),
            Sdf.Path("/World/NotCoinciding"),
        ]
        returned = result[0]
        self.assertEqual(expected, returned)

    async def test_time_varying_meshes(self):
        """Test coincident meshes operation on meshes with authored time varying attributes"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

    async def test_basis_curves(self):
        """Test finding simple coinciding basis curves"""

        stage = self._open_stage("coincidingCurves.usda")

        args = DEFAULT_ARGS.copy()
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert one set of coinciding curves
        self.assertEqual(len(result), 1)
        curves = result[0]

        # Assert it contains 2 coinciding curves, and assert the
        # specific ones we expect
        self.assertEqual(len(curves), 2)
        self.assertIn("/Linear/Coinciding1", curves)
        self.assertIn("/Linear/Coinciding2", curves)

    async def test_empty_meshes(self):
        """Test that empty meshes are not considered coinciding"""

        stage = self._open_stage("coincidingEmpty.usda")

        args = DEFAULT_ARGS.copy()
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # One set of coinciding meshes
        self.assertEqual(len(result), 1)
        meshes = result[0]
        self.assertIn("/World/Cube", meshes)
        self.assertIn("/World/Cube_01", meshes)

        # Meshes with no points not present
        self.assertNotIn("/World/Cube_02", meshes)
        self.assertNotIn("/World/Cube_03", meshes)

    async def test_filtering_paths(self):
        """Test path expression logic"""

        stage = self._open_stage("coincidingInstances.usda")

        # Test that specifying a path resolves something point-based that
        # is not a Mesh (e.g. a BasisCurve)
        # NB: this used to not work, hence the specific test
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = ["/World/CurveCoinciding*"]
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # One set of coinciding meshes
        self.assertEqual(len(result), 1)
        meshes = result[0]
        self.assertIn("/World/CurveCoinciding1", meshes)
        self.assertIn("/World/CurveCoinciding2", meshes)

    def _find_results(self, results, value):
        """Find a result containing *value*; order is non-deterministic due to threading."""

        for result in results:
            if value in result:
                return result
        return None

    async def test_instances(self):
        """Test various situations containing coinciding instances/prototypes"""

        stage = self._open_stage("coincidingInstances.usda")

        args = DEFAULT_ARGS.copy()
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 6)

        # Two simple non-instanced curves
        resultSet = self._find_results(result, "/World/CurveCoinciding1")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/CurveCoinciding1", resultSet)
        self.assertIn("/World/CurveCoinciding2", resultSet)

        # Two instanced curves
        resultSet = self._find_results(result, "/World/CurveInstanceCoinciding1/Curve")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/CurveInstanceCoinciding1/Curve", resultSet)
        self.assertIn("/World/CurveInstanceCoinciding2/Curve", resultSet)

        # Two simple coinciding meshes
        resultSet = self._find_results(result, "/World/Cube")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/Cube", resultSet)
        self.assertIn("/World/CubeCoinciding", resultSet)

        # Two instanced meshes
        resultSet = self._find_results(result, "/World/CubeInstance/Cube")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/CubeInstance/Cube", resultSet)
        self.assertIn("/World/CubeInstanceCoinciding/Cube", resultSet)

        # An instance that coincides with a normal mesh
        resultSet = self._find_results(result, "/World/CubeCoincidingWithInstance")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/CubeCoincidingWithInstance", resultSet)
        self.assertIn("/World/CubeInstanceThatCoincides/Cube", resultSet)

        # Two prototypes
        resultSet = self._find_results(result, "/Geometry/CubePrototype1/Cube")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/Geometry/CubePrototype1/Cube", resultSet)
        self.assertIn("/Geometry/CubePrototype2/Cube", resultSet)

    async def test_offset(self):
        """Test "offset" mode"""

        stage = self._open_stage("overlappingDuplicates.usda")

        # Run with no offset
        args = DEFAULT_ARGS.copy()
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 2)

        resultSet = self._find_results(result, "/World/Box/Box")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/Box/Box", resultSet)
        self.assertIn("/World/Box2/Box", resultSet)

        resultSet = self._find_results(result, "/World/BoxScaleTest1/Box")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/BoxScaleTest1/Box", resultSet)
        self.assertIn("/World/BoxScaleTest2/Box", resultSet)

        # Now enable offset and test
        args["offset"] = 50.0
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 3)

        resultSet = self._find_results(result, "/World/Box/Box")
        self.assertEqual(len(resultSet), 3)
        self.assertIn("/World/Box/Box", resultSet)
        self.assertIn("/World/Box2/Box", resultSet)
        self.assertIn("/World/Box4/Box", resultSet)

        resultSet = self._find_results(result, "/World/BoxScaleTest1/Box")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/BoxScaleTest1/Box", resultSet)
        self.assertIn("/World/BoxScaleTest2/Box", resultSet)

        resultSet = self._find_results(result, "/World/BoxSmall1/Box")
        self.assertEqual(len(resultSet), 3)
        self.assertIn("/World/BoxSmall1/Box", resultSet)
        self.assertIn("/World/BoxSmall2/Box", resultSet)
        self.assertIn("/World/BoxSmall4/Box", resultSet)

        # Test again with a larger offset
        args["offset"] = 150.0
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 3)

        # One extra result in this set
        resultSet = self._find_results(result, "/World/Box/Box")
        self.assertEqual(len(resultSet), 4)
        self.assertIn("/World/Box/Box", resultSet)
        self.assertIn("/World/Box2/Box", resultSet)
        self.assertIn("/World/Box4/Box", resultSet)
        self.assertIn("/World/Box6/Box", resultSet)

        # Same result as before
        resultSet = self._find_results(result, "/World/BoxScaleTest1/Box")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/BoxScaleTest1/Box", resultSet)
        self.assertIn("/World/BoxScaleTest2/Box", resultSet)

        # One extra result in this set
        resultSet = self._find_results(result, "/World/BoxSmall1/Box")
        self.assertEqual(len(resultSet), 4)
        self.assertIn("/World/BoxSmall1/Box", resultSet)
        self.assertIn("/World/BoxSmall2/Box", resultSet)
        self.assertIn("/World/BoxSmall3/Box", resultSet)
        self.assertIn("/World/BoxSmall4/Box", resultSet)

    async def test_fuzzy_coincide(self):
        """Test fuzzy mode"""

        stage = self._open_stage("fuzzyCoincident.usda")

        # Run with fuzzy disabled
        args = DEFAULT_ARGS.copy()
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 1)

        resultSet = self._find_results(result, "/World/Box/Box")
        self.assertEqual(len(resultSet), 2)
        self.assertIn("/World/Box/Box", resultSet)
        self.assertIn("/World/Box2/Box", resultSet)

        # Run again, fuzzy enabled
        args["fuzzy"] = True
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 1)

        resultSet = self._find_results(result, "/World/Box/Box")
        self.assertEqual(len(resultSet), 3)
        self.assertIn("/World/Box/Box", resultSet)
        self.assertIn("/World/Box2/Box", resultSet)
        self.assertIn("/World/Box3/Box", resultSet)

        # Run again, fuzzy enabled and also offset
        args["fuzzy"] = True
        args["offset"] = 50.0
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 1)

        resultSet = self._find_results(result, "/World/Box/Box")
        self.assertEqual(len(resultSet), 5)
        self.assertIn("/World/Box/Box", resultSet)
        self.assertIn("/World/Box2/Box", resultSet)
        self.assertIn("/World/Box3/Box", resultSet)
        self.assertIn("/World/Box4/Box", resultSet)
        self.assertIn("/World/Box5/Box", resultSet)

    async def test_coinciding_abstract(self):
        """Test abstract vs non-abstract"""

        stage = self._open_stage("coincidingAbstract.usda")

        args = DEFAULT_ARGS.copy()
        success, opResult = self._execute_command(args)

        # Assert operation worked
        self.assertTrue(success)
        self.assertTrue(opResult[0])

        result = opResult[2]

        # Assert expected total result length
        self.assertEqual(len(result), 1)

        # Assert the two meshes that coincide are the only two results,
        # not the abstract definition
        self.assertEqual(len(result[0]), 2)
        self.assertIn("/World/Coincides1", result[0])
        self.assertIn("/World/Coincides2", result[0])
