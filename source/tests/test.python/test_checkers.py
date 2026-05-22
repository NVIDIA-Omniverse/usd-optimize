# SPDX-FileCopyrightText: Copyright (c) 2025-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from unittest import TestCase

from omni.asset_validator import IssuePredicates
from omni.asset_validator.tests import IsAnError, IsAnInfo, IsAWarning, ValidationTestCaseMixin
from omni.scene.optimizer.validators import (
    CoincidingGeometryChecker,
    ColocatedVerticesChecker,
    DuplicateFaceChecker,
    DuplicateGeometryChecker,
    DuplicateMaterialsChecker,
    EmptyLeafChecker,
    FlatHierarchiesChecker,
    FuzzyDuplicateGeometryChecker,
    HighVertexCountChecker,
    IndexedPrimvarChecker,
    InvisiblePrimsChecker,
    IsolatedVerticesChecker,
    NonManifoldChecker,
    NormalsChecker,
    OccludedMeshesChecker,
    PrimitiveFitChecker,
    RedundantTimeSamplesChecker,
    RtxMeshCountChecker,
    SmallMeshChecker,
    SparseMeshChecker,
    UnusedUVsChecker,
    WindingsChecker,
    ZeroAreaFacesChecker,
    ZeroExtentChecker,
)

from .test_utils import _get_test_data_file_path


class Test_Checkers(TestCase, ValidationTestCaseMixin):

    def test_sparse_mesh_checker(self):
        self.assertRule(
            asset=_get_test_data_file_path("sparseMeshes.usda"),
            rule=SparseMeshChecker,
            asserts=[
                IsAWarning("Stage contains sparse meshes", at="Prim </>"),
                IsAWarning("Large sparse mesh that can be diced detected with density of *", at="Prim </World/Cube>"),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshA>",
                ),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshB>",
                ),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshC>",
                ),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshD>",
                ),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshE>",
                ),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshF>",
                ),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshG>",
                ),
                IsAWarning(
                    "Disjoint sparse mesh that can be split and clustered detected with density of *",
                    at="Prim </World/meshH>",
                ),
            ],
        )

    def test_duplicate_materials_checker(self):
        """Test the DuplicateMaterialsChecker"""
        self.assertRule(
            asset=_get_test_data_file_path("optimizeMaterials.usda"),
            rule=DuplicateMaterialsChecker,
            asserts=[
                IsAWarning("There is 1 duplicate of /Cubes/CubeYellow1/Material3"),
                IsAWarning("There is 1 duplicate of /Cubes/CubeRed1/Material4"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("optimizeMaterials.usda"),
            rule=DuplicateMaterialsChecker,
            predicate=None,
        )

    def test_indexed_primvars_checker(self):
        """Test the IndexedPrimvarsChecker"""
        self.assertRule(
            asset=_get_test_data_file_path("validatePrimvars.usda"),
            rule=IndexedPrimvarChecker,
            asserts=[
                # Indexable
                IsAWarning("primvars:st contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableColor3d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableColor3f contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableColor3h contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableColor4d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableColor4f contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableColor4h contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableDouble contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableDouble2 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableDouble3 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableDouble4 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableFloat contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableFloat2 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableFloat3 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableFloat4 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableHalf contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableHalf2 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableHalf3 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableHalf4 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableInt2 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableInt3 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableInt4 contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableMatrix3d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableMatrix4d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableNormal3d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableNormal3f contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableNormal3h contains repeated values that can be indexed."),
                IsAWarning("primvars:indexablePoint3d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexablePoint3f contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableString contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableTexCoord2d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableTexCoord2f contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableTexCoord2h contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableTexCoord3d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableTexCoord3f contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableTexCoord3h contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableVector3d contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableVector3f contains repeated values that can be indexed."),
                IsAWarning("primvars:indexableVector3h contains repeated values that can be indexed."),
                # Out of bounds
                IsAnError("Primvar indices out of bounds"),
                # Non-array
                IsAnError("Primvar is not of array type."),
            ],
        )

        # Assert the suggested fixes work (note: we use a predicate to filter the ones we support fixing)
        self.assertSuggestion(
            asset=_get_test_data_file_path("validatePrimvars.usda"),
            rule=IndexedPrimvarChecker,
            predicate=IssuePredicates.ContainsMessage("contains repeated values"),
        )

    def test_invisible_prims_checker(self):
        """Test the InvisiblePrimsChecker"""
        self.assertRule(
            asset=_get_test_data_file_path("invisiblePrims.usda"),
            rule=InvisiblePrimsChecker,
            asserts=[
                IsAWarning("Stage contains invisible prims", at="Prim </>"),
                IsAWarning("Invisible prim found", at="Prim </World/Cone>"),
                IsAWarning("Invisible prim found", at="Prim </World/Torus>"),
                IsAWarning("Invisible prim found", at="Prim </World/Xform/Cube>"),
                IsAWarning("Invisible prim found", at="Prim </World/Xform_01>"),
                IsAWarning("Invisible prim found", at="Prim </World/Xform_02>"),
                IsAWarning("Invisible prim found", at="Prim </World/Xform_02/Cube>"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("invisiblePrims.usda"),
            rule=InvisiblePrimsChecker,
            predicate=IssuePredicates.ContainsMessage("Stage contains invisible prims"),
        )

    def test_redundant_timesamples_checker(self):
        """Test the RedundantTimeSamplesChecker"""

        self.assertRule(
            asset=_get_test_data_file_path("optimizeTimeSamples.usda"),
            rule=RedundantTimeSamplesChecker,
            asserts=[
                IsAWarning("Attribute /CubeDuplicateScale.xformOp:scale has 257/257 redundant time samples"),
                IsAWarning("Attribute /CubeFloatHold.testAttr has 5/8 redundant time samples"),
                IsAWarning("Attribute /CubeInstanceSource/CubeInstance.testAttr has 2/7 redundant time samples"),
                IsAWarning("Attribute /CubeLinear.xformOp:transform has 72/120 redundant time samples"),
                IsAWarning("Attribute /CubeLinearHold.xformOp:transform has 83/120 redundant time samples"),
                IsAWarning("Attribute /CubeLinearStep.xformOp:transform has 72/120 redundant time samples"),
                IsAWarning("Attribute /CubeQuatd.testAttr has 5/5 redundant time samples"),
                IsAWarning("Attribute /CubeQuatdArray.testAttr has 5/5 redundant time samples"),
                IsAWarning("Attribute /CubeQuatf.testAttr has 5/5 redundant time samples"),
                IsAWarning("Attribute /CubeQuatfArray.testAttr has 5/5 redundant time samples"),
                IsAWarning("Attribute /CubeSingle.xformOp:transform has 1/1 redundant time sample"),
                IsAWarning("Attribute /CubeStatic.xformOp:transform has 2/2 redundant time samples"),
                IsAWarning("Attribute /CubeStaticDuplicates.xformOp:transform has 12/12 redundant time samples"),
                IsAWarning("Attribute /CubeStrings.testAttr has 3/6 redundant time samples"),
                IsAWarning("Attribute /CubeStringsDuplicate.testAttr has 6/6 redundant time samples"),
                IsAWarning("Attribute /CubeVec2d.testAttr has 2/8 redundant time samples"),
                IsAWarning("Attribute /CubeVec2f.testAttr has 2/7 redundant time samples"),
                IsAWarning("Attribute /CubeVec3d.testAttr has 2/7 redundant time samples"),
                IsAWarning("Attribute /CubeVec3f.testAttr has 2/8 redundant time samples"),
                IsAWarning("Attribute /CubeVec4d.testAttr has 2/7 redundant time samples"),
                IsAWarning("Attribute /CubeVec4f.testAttr has 2/8 redundant time samples"),
            ],
        )

        # Assert the suggested fixes work (note: we use a predicate to filter the ones we support fixing)
        self.assertSuggestion(
            asset=_get_test_data_file_path("optimizeTimeSamples.usda"),
            rule=RedundantTimeSamplesChecker,
            predicate=None,
        )

    def test_coinciding_geometry_checker(self):
        self.assertRule(
            asset=_get_test_data_file_path("coincidingMeshes.usda"),
            rule=CoincidingGeometryChecker,
            asserts=[
                IsAWarning(
                    "2 coinciding geometries found at paths: [/World/Cube, /World/Cube_01]", at="Prim </World/Cube>"
                ),
                IsAWarning(
                    "3 coinciding geometries found at paths: [/World/Sphere_02, /World/Sphere_03, /World/Sphere_04]",
                    at="Prim </World/Sphere_02>",
                ),
            ],
        )

    # TODO: fix me
    # def test_primitive_fit_checker(self):
    #     self.assertRule(
    #         asset=_get_test_data_file_path("primitiveFit.usda"),
    #         rule=PrimitiveFitChecker,
    #         asserts=[
    #             IsAnInfo("Stage contains 10 non-composed meshes *"),
    #             IsAWarning(
    #                 "Found 1 mesh w/o non-const primvars that can be replaced by a cone GPrim, eliminating 320 faces and 258 vertices."
    #             ),
    #             IsAWarning(
    #                 "Found 1 mesh WITH non-const primvars that can be replaced by a cone GPrim, losing texture mapping but eliminating 320 faces and 258 vertices."
    #             ),
    #             IsAWarning(
    #                 "Found 1 mesh w/o non-const primvars that can be replaced by a cube GPrim, eliminating 6 faces and 8 vertices."
    #             ),
    #             IsAWarning(
    #                 "Found 1 mesh WITH non-const primvars that can be replaced by a cube GPrim, losing texture mapping but eliminating 6 faces and 8 vertices."
    #             ),
    #             IsAWarning(
    #                 "Found 1 mesh w/o non-const primvars that can be replaced by a cylinder GPrim, eliminating 96 faces and 66 vertices."
    #             ),
    #             IsAWarning(
    #                 "Found 1 mesh WITH non-const primvars that can be replaced by a cylinder GPrim, losing texture mapping but eliminating 96 faces and 66 vertices."
    #             ),
    #             IsAWarning(
    #                 "Found 1 mesh w/o non-const primvars that can be replaced by a sphere GPrim, eliminating 512 faces and 482 vertices."
    #             ),
    #             IsAWarning(
    #                 "Found 1 mesh WITH non-const primvars that can be replaced by a sphere GPrim, losing texture mapping but eliminating 512 faces and 482 vertices."
    #             ),
    #         ],
    #     )

    def test_merge_vertices_checker(self):
        """Test for merged vertices"""

        self.assertRule(
            asset=_get_test_data_file_path("mergeColocatedVertices_input.usd"),
            rule=ColocatedVerticesChecker,
            asserts=[
                IsAWarning("Found 2 meshes with mergeable vertices to fix"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("mergeColocatedVertices_input.usd"),
            rule=ColocatedVerticesChecker,
            predicate=None,
        )

    def test_nonmanifold_checker(self):
        """Test for nonmanifold geometry"""

        self.assertRule(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=NonManifoldChecker,
            asserts=[
                IsAWarning("Found 2 nonManifold meshes to fix"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=NonManifoldChecker,
            predicate=None,
        )

    def test_zero_area_faces_checker(self):
        """Test for zero area faces"""

        self.assertRule(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=ZeroAreaFacesChecker,
            asserts=[
                IsAWarning("Found 1 zero area faces mesh to fix"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=ZeroAreaFacesChecker,
            predicate=None,
        )

    def test_isolated_vertices_checker(self):
        """Test for isolated vertices"""

        self.assertRule(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=IsolatedVerticesChecker,
            asserts=[
                IsAWarning("Found 1 mesh with isolated vertices to fix"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=IsolatedVerticesChecker,
            predicate=None,
        )

    def test_duplicate_face_checker(self):
        """Test for duplicate faces"""

        self.assertRule(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=DuplicateFaceChecker,
            asserts=[
                IsAWarning("Found 1 mesh with duplicate faces to fix"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("cubeDegenerateFaces.usda"),
            rule=DuplicateFaceChecker,
            predicate=None,
        )

    def test_flat_hierarchies_checker(self):
        """Test the FlatHierarchiesChecker"""

        # override the max children to only 5 so we don't need a large test scene
        FlatHierarchiesChecker.MAX_CHILDREN = 5

        self.assertRule(
            asset=_get_test_data_file_path("flatHierarchies.usd"),
            rule=FlatHierarchiesChecker,
            asserts=[
                IsAWarning("Found 6 children under prim '/World/Xform_01'"),
            ],
        )

    def test_normals_checker(self):
        """Test for normals checker"""

        self.assertRule(
            asset=_get_test_data_file_path("invalidNormals.usda"),
            rule=NormalsChecker,
            asserts=[
                IsAWarning("Found 7 meshes with normals that are not of length 1 within a tolerance of 1e-4"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("invalidNormals.usda"),
            rule=NormalsChecker,
            predicate=None,
        )

    def test_windings_checker(self):
        """Test for windings checker"""

        self.assertRule(
            asset=_get_test_data_file_path("geometryWindings.usda"),
            rule=WindingsChecker,
            asserts=[
                IsAnError("Mesh has inconsistent windings", at="Prim </World/perVertexNormalsLeftHanded>"),
                IsAnError("Mesh has inconsistent windings", at="Prim </World/reversedDefaultOrientation>"),
                IsAnError("Mesh has inconsistent windings", at="Prim </World/reversedIndexedDefaultOrientation>"),
                IsAnError("Mesh has inconsistent windings", at="Prim </World/reversedPerVertex>"),
                IsAnError("Mesh has inconsistent windings", at="Prim </World/reversedRightHandOrientation>"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("geometryWindings.usda"),
            rule=WindingsChecker,
            predicate=None,
        )

    def test_occluded_meshes_checker(self):
        """Test the OccludedMeshesChecker"""

        self.assertRule(
            asset=_get_test_data_file_path("hidden_cubes.usda"),
            rule=OccludedMeshesChecker,
            asserts=[
                IsAWarning("Found 3 occluded meshes"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("hidden_cubes.usda"),
            rule=OccludedMeshesChecker,
            predicate=None,
        )

    def test_duplicate_geometry_checker(self):
        """Test the DuplicateGeometryChecker"""

        self.assertRule(
            asset=_get_test_data_file_path("fuzzyDedupTest.usda"),
            rule=DuplicateGeometryChecker,
            asserts=[
                IsAWarning("/box3/box3 has 1 duplicate"),
                IsAWarning("/torus2/torus2 has 1 duplicate"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("fuzzyDedupTest.usda"),
            rule=DuplicateGeometryChecker,
            predicate=None,
        )

    def test_duplicate_geometry_fuzzy_checker(self):
        """Test the FuzzyDuplicateGeometryChecker"""

        test_file = "fuzzyDedupCubes.usda"

        self.assertRule(
            asset=_get_test_data_file_path(test_file),
            rule=FuzzyDuplicateGeometryChecker,
            asserts=[
                IsAWarning("/World/box3/box3 has 2 duplicates"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path(test_file),
            rule=FuzzyDuplicateGeometryChecker,
            predicate=None,
        )

        # Re-test with a couple of specific paths.
        # These have identical topology so no warnings should be flagged.
        FuzzyDuplicateGeometryChecker.PATHS = ["/World/box3/box3", "/World/box1/box1"]
        self.assertRule(
            asset=_get_test_data_file_path(test_file),
            rule=FuzzyDuplicateGeometryChecker,
            asserts=[],
        )

        FuzzyDuplicateGeometryChecker.PATHS = []

    def test_zero_extent_checker(self):
        self.assertRule(
            asset=_get_test_data_file_path("smallMeshes.usda"),
            rule=ZeroExtentChecker,
            asserts=[
                IsAWarning("Stage contains geometry with zero sized extents", at="Prim </>"),
                IsAWarning("Zero extent geometry found", at="Prim </World/Degenerate>"),
            ],
        )

        # Assert the suggested fixes work
        self.assertSuggestion(
            asset=_get_test_data_file_path("smallMeshes.usda"),
            rule=ZeroExtentChecker,
            predicate=IssuePredicates.ContainsMessage("Stage contains geometry with zero sized extents"),
        )

    # This only tests the checker logic itself, not the parameter override pipeline.
    async def test_small_mesh_checker_default_threshold(self):
        """Default SIZE_THRESHOLD of 0.001 catches only the smallest cube (size 0.0005)"""
        self.assertRule(
            asset=_get_test_data_file_path("smallMeshCubes.usda"),
            rule=SmallMeshChecker,
            asserts=[
                IsAWarning("Stage contains 1 mesh below size threshold 0.001", at="Prim </>"),
                IsAWarning("Small mesh found", at="Prim </World/Cube_0_0005>"),
            ],
        )

        self.assertSuggestion(
            asset=_get_test_data_file_path("smallMeshCubes.usda"),
            rule=SmallMeshChecker,
            predicate=IssuePredicates.ContainsMessage("Stage contains 1 mesh below size threshold"),
        )

    def test_empty_leaf_checker(self):
        """Test the EmptyLeafChecker"""

        self.assertRule(
            asset=_get_test_data_file_path("pruneLeaves.usda"),
            rule=EmptyLeafChecker,
            asserts=[IsAWarning("Stage contains 7 empty leaf primitives")],
        )

        self.assertSuggestion(asset=_get_test_data_file_path("pruneLeaves.usda"), rule=EmptyLeafChecker, predicate=None)

    def test_high_vertex_count_checker(self):
        """Test the HighVertexCountChecker"""

        # Override the levels for smaller numbers based on the
        # test data. This is just to avoid having to check in
        # a much larger USD file.
        HighVertexCountChecker.LEVEL_HIGH = 40000
        HighVertexCountChecker.LEVEL_VERY_HIGH = 60000
        HighVertexCountChecker.LEVEL_EXTREME = 150000

        self.assertRule(
            asset=_get_test_data_file_path("countVerts.usd"),
            rule=HighVertexCountChecker,
            asserts=[
                IsAWarning("Mesh has high vertex count*", at="Prim </Geometry/MeshHigh/mesh_0>"),
                IsAWarning("Mesh has very high vertex count*", at="Prim </Geometry/MeshVeryHigh/mesh_0>"),
                IsAWarning("Mesh has extreme vertex count*", at="Prim </Geometry/MeshExtreme/mesh_0>"),
            ],
        )

    def test_unused_uvs_checker(self):
        """Test the Unused UVs checker"""

        self.assertRule(
            asset=_get_test_data_file_path("unusedUVs.usda"),
            rule=UnusedUVsChecker,
            asserts=[
                IsAWarning("Unused UV attribute", at="Attribute (primvars:st) Prim </World/CubeMaterialNoUse>"),
                IsAWarning("Unused UV attribute", at="Attribute (primvars:st) Prim </World/CubeUnusedUVs>"),
                IsAWarning(
                    "Unused UV attribute", at="Attribute (primvars:st) Prim </World/Geometry/CubePrototype/Cube>"
                ),
                IsAWarning(
                    "Unused UV attribute",
                    at="Attribute (primvars:st:indices) Prim </World/Geometry/CubePrototype/Cube>",
                ),
            ],
        )

        self.assertSuggestion(asset=_get_test_data_file_path("unusedUVs.usda"), rule=UnusedUVsChecker, predicate=None)

    def test_rtx_mesh_count_checker(self):
        """Test the RtxMeshCountChecker"""

        RtxMeshCountChecker.RTX_UNIQUE_MESH_COUNT_LIMIT = 6

        self.assertRule(
            asset=_get_test_data_file_path("validate_rtxMeshCount.usda"),
            rule=RtxMeshCountChecker,
            asserts=[
                IsAWarning("Number of unique RTX meshes (7) exceeds the recommended limit of 6."),
            ],
        )
