# SPDX-FileCopyrightText: Copyright (c) 2022-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from collections import defaultdict

from pxr import Gf, Sdf, Usd, UsdGeom, UsdShade

from .test_utils import Test_Operation, _get_context, _get_instances, _get_meshes, _get_test_data_file_path

# Duplicate Method values
DUPLICATE_METHOD_COPYVALUES = 0  # Copy the points and normals values
DUPLICATE_METHOD_REFERENCE = 1  # Reference composition arc
DUPLICATE_METHOD_INSTANCEABLEREFERENCE = 2  # Reference composition arc with instanceable true
DUPLICATE_METHOD_SET_ATTRIBUTE = 3  # Set duplication set attribute
DUPLICATE_METHOD_POINT_INSTANCER = 4  # Replace duplicates with a PointInstancer per duplicate set

# PointInstancer parent mode values
POINT_INSTANCER_LOCATION_COMMON_ROOT = 0
POINT_INSTANCER_LOCATION_CUSTOM_PATH = 1


# Default arguments for the command
DEFAULT_ARGS = {
    "meshPrimPaths": [],
    "considerDeepTransforms": True,
    "tolerance": 0.05,
    "duplicateMethod": DUPLICATE_METHOD_INSTANCEABLEREFERENCE,
    "fuzzy": False,
    "useGpu": False,
    "allowScaling": False,
}


def _compute_hash_value_vec3f_array(value):
    """Generate a hash for a vec3f array based on values"""
    if value is None:
        return None
    # Hash each value individually, then array of hashes.
    values_hashes = tuple([hash(x) for x in value])
    result = hash(values_hashes)
    return result


def _get_unique_mesh_paths(stage):
    """Returns a list of lists of containing prim paths for meshes that RTX would consider duplicates"""
    # This is intended to mirror the de-dupe logic of RTX so that we can assert the desired result.
    result = defaultdict(list)

    # Iterate prims using stage traversal so that only the mesh prims visible to the renderer are encountered.
    for prim in stage.Traverse():

        # Skip prims that are not meshes
        mesh = UsdGeom.Mesh(prim)
        if not mesh:
            continue

        points = mesh.GetPointsAttr().Get()
        extent = mesh.GetExtentAttr().Get()

        # Use the attr normals unless primvar normals are authored, in which case use the flattened values
        normals = mesh.GetNormalsAttr().Get()
        primvar = UsdGeom.Primvar(prim.GetAttribute("primvars:normals"))
        if primvar:
            normals = primvar.ComputeFlattened()

        # Get points and normals hashes then add the prim path to the result for that key.
        p_hash = _compute_hash_value_vec3f_array(points)
        n_hash = _compute_hash_value_vec3f_array(normals)
        e_hash = _compute_hash_value_vec3f_array(extent)
        key = (p_hash, n_hash, e_hash)

        result[key].append(prim.GetPath())

    return [x for x in result.values()]


def _get_mesh_paths(stage):
    """Returns a pair of mesh paths and instance proxy mesh paths"""
    # Declare return variables.
    mesh_paths = list()
    instance_proxy_mesh_paths = list()

    # Iterate the stage and populate lists with mesh paths.
    for prim in Usd.PrimRange.Stage(stage, Usd.TraverseInstanceProxies()):
        if UsdGeom.Mesh(prim):

            # Put the path in the appropriate list.
            if prim.IsInstanceProxy():
                instance_proxy_mesh_paths.append(prim.GetPath())
            else:
                mesh_paths.append(prim.GetPath())

    # Return result.
    return (mesh_paths, instance_proxy_mesh_paths)


def _get_all_mesh_paths(stage):
    """Returns a list of paths of all prims representing meshes"""
    # Declare return variables.
    result = list()
    for x in _get_mesh_paths(stage):
        for path in x:
            result.append(path)
    return result


def _get_worldspace_points(prim, xformCache):
    """Returns points in worldspace"""
    result = []
    # Traverse from the given prim down collecting all the Mesh prims
    for x in Usd.PrimRange(prim, Usd.TraverseInstanceProxies()):
        # Get the points and local to world transform matrix.
        mesh = UsdGeom.Mesh(x)
        if mesh:
            points = mesh.GetPointsAttr().Get()
            matrix = xformCache.GetLocalToWorldTransform(x)
            # Add points with the matrix applied to the result
            for point in points:
                result.append(matrix.Transform(point))
    return result


def _get_subsets_and_bound_materials(prim):
    """Yields pairs of UsdGeom.Subset and the UsdShade.Material bound to it"""
    # Yield nothing if the prim is not Imageable
    imageable = UsdGeom.Imageable(prim)
    if imageable:

        # Iterate over all subsets incase the family name is not set correctly.
        for subset in UsdGeom.Subset.GetAllGeomSubsets(imageable):

            # Only yield if there is a bound material.
            material, _ = UsdShade.MaterialBindingAPI(subset.GetPrim()).ComputeBoundMaterial()
            if material:
                yield (subset, material)


def _get_per_face_bound_materials(prim):
    """Returns a map of material paths and the faces they are bound to"""
    result = defaultdict(list)
    subsets = []

    # Iterate over all the material bound subsets collecting the face indices the material is bound to.
    for subset, material in _get_subsets_and_bound_materials(prim):

        # Add the faces bound to the material to the result.
        faces_indices = subset.GetIndicesAttr().Get()
        result[material.GetPath()].extend(faces_indices)

        # Add the subset to the list of subsets so we can calculate unassigned faces.
        subsets.append(subset)

    # Add any materials bound directly to the prim.
    material, _ = UsdShade.MaterialBindingAPI(prim).ComputeBoundMaterial()
    if material:

        # Build an accurate face indices list so that we are asserting the effective shaded faces.
        face_count = len(UsdGeom.Mesh(prim).GetFaceVertexCountsAttr().Get())
        if subsets:  # pragma: no cover
            faces_indices = UsdGeom.Subset.GetUnassignedIndices(subsets, face_count)
        else:
            faces_indices = range(face_count)

        # Add the faces bound to the material to the result.
        result[material.GetPath()].extend(faces_indices)

    return result


class Test_Operation_Deduplicate_Geometry(Test_Operation):

    OPERATION = "deduplicateGeometry"

    def assertVec3ArrayAlmostEqual(self, expected, returned, tolerance, msg):
        """Assert that the individual values that make up two Vec3 Arrays are almost equal"""
        # Track equality.
        are_equal = True

        # Compare each value with a length difference tolerance.
        for expected_point, returned_point in zip(expected, returned):
            # Compute the length of the distance vector between the two points.
            box = Gf.Range3d(expected_point, returned_point)
            delta = box.GetSize().GetLength()
            # Early out if two points are further apart than the tolerance allows for.
            if delta > tolerance:  # pragma: no cover
                are_equal = False
                break

        # Assert the resulting equality.
        self.assertTrue(are_equal, msg)

    def assertWorldspacePointsEqual(self, prim_before, prim_after, xformCache, tolerance=None):
        """Assert that world space points values of two prims match"""
        # Get the before and after point in worldspace.
        expected = _get_worldspace_points(prim_before, xformCache)
        returned = _get_worldspace_points(prim_after, xformCache)

        # Assert that the values match exactly or with a tolerance.
        msg = 'World space points differ for "{}" and "{}"'.format(prim_before.GetPath(), prim_after.GetPath())
        if tolerance is None:
            self.assertEqual(expected, returned, msg)
        else:
            self.assertVec3ArrayAlmostEqual(expected, returned, tolerance, msg)

    def assertWorldspaceScenePointsEqual(self, stage_before, stage_after, tolerance=None):
        """Assert that world space points values of all mesh prims in stage_before and stage_after match"""
        paths_before = _get_all_mesh_paths(stage_before)
        paths_after = _get_all_mesh_paths(stage_after)
        self.assertEqual(paths_before, paths_after)

        xformCache = UsdGeom.XformCache()

        for path_before, path_after in zip(paths_before, paths_after):
            prim_before = stage_before.GetPrimAtPath(path_before)
            prim_after = stage_after.GetPrimAtPath(path_after)
            self.assertWorldspacePointsEqual(prim_before, prim_after, xformCache, tolerance)

    def assertBoundMaterialsEqual(self, prim_before, prim_after):
        """Assert that the paths of materials bound to two prims match"""
        # Get bound materials and the faces they are bound to so that we can compare UsdGeomSubset material bindings.
        materials_before = _get_per_face_bound_materials(prim_before)
        materials_after = _get_per_face_bound_materials(prim_after)

        # Assert that the material paths before and after match.
        expected = set(materials_before.keys())
        returned = set(materials_after.keys())
        self.assertEqual(returned, expected)

        # Assert that the face indices for each material path match.
        material_paths = materials_before.keys()
        for material_path in material_paths:

            # Get the sorted face indices for the material before and after
            expected = sorted(materials_before[material_path])
            returned = sorted(materials_after[material_path])

            # Assert that they are equal.
            self.assertEqual(returned, expected)

    async def test_copy_values_improves_rtx_deduplicate(self):
        """Check that after the operation has run there are less unique meshes than there were before hand"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()

        # Open the stage and get the initial list of unique meshs.
        stage = self._open_stage("deduplicateGeometryExample.usd")
        unique_meshes_before = _get_unique_mesh_paths(stage)

        # RTX finds 8 unique meshes in this scene so assert that we find the same.
        self.assertEqual(len(unique_meshes_before), 8)

        # Enable deep transform checks, set method to copy values, then execute command.
        # This should produce a result that has less unique meshes from the point of view on RTX.
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.05
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        self._execute_command(args)

        # Assert that there are less unique meshes after execution.
        unique_meshes_after = _get_unique_mesh_paths(stage)
        self.assertTrue(len(unique_meshes_before) > len(unique_meshes_after))

        # The logic currently results in 4 unique meshes.
        self.assertEqual(len(unique_meshes_after), 4)

        # The input meshes have names that describe the changes that were made to them during creation.
        # Reduce the paths down to names so that we can assert the cases we currently cover.
        unique_cases = list()
        for paths in unique_meshes_after:
            names = sorted(list(set([path.name for path in paths])))
            unique_cases.append(names)
        unique_cases.sort()

        # Expected outcome.
        cases_0 = [
            "BaseMesh0",
            "BaseMesh1",
            "RotatedMesh0",
            "RotatedMesh1",
            "ScaledMesh0",
            "ScaledMesh1",
            "TranslatedMesh0",
            "TranslatedMesh1",
            "WithinToleranceMesh0",
            "WithinToleranceMesh1",
        ]
        cases_1 = [
            "DifferentMesh0",
            "DifferentMesh1",
        ]
        cases_2 = [
            "DifferentNormalsMesh0",
            "DifferentNormalsMesh1",
        ]
        cases_3 = [
            "NoNormalsMesh0",
            "NoNormalsMesh1",
        ]

        # Ensure that the currently expected cases are covered.
        self.assertEqual(unique_cases[0], cases_0)
        self.assertEqual(unique_cases[1], cases_1)
        self.assertEqual(unique_cases[2], cases_2)
        self.assertEqual(unique_cases[3], cases_3)

    async def test_copy_values(self):
        """Check that deduplicate geometry via json dedudplicates."""
        # Open the stage and get the initial list of unique meshs.
        stage = self._open_stage("twoWithinToleranceMeshes.usda")
        # RTX finds 2 unique meshes in this scene so assert that we find the same.
        self.assertEqual(len(_get_unique_mesh_paths(stage)), 2)

        # Enable deep transform checks, set method to copy values, then execute json.
        # This should produce a result that has less unique meshes from the point of view on RTX.
        self._execute_json(stage, "deduplicateGeometry_copyValues.json")
        # Expecting one unique mesh after deduplication.
        self.assertEqual(len(_get_unique_mesh_paths(stage)), 1)

    async def test_copy_values_no_dt(self):
        """Check that deduplicateGeometry_copyValuesNoDeepTransforms.json does reduce the number of unique meshes"""
        # Open the stage and get the initial list of unique meshs.
        stage = self._open_stage("twoWithinToleranceMeshes.usda")
        # Enable deep transform checks, set method to copy values, then execute command.
        # This should produce a result that has less unique meshes from the point of view on RTX.
        self._execute_json(stage, "deduplicateGeometry_copyValuesNoDeepTransforms.json")
        # Assert that there are less unique meshes after execution.
        unique_meshes_after = _get_unique_mesh_paths(stage)

        # The logic currently results in 4 unique meshes.
        self.assertEqual(len(unique_meshes_after), 2)

    async def test_copy_values_tight_tolerance(self):
        """Check that very tigth tolerance does not change the number of unique meshes"""
        # Open the stage and get the initial list of unique meshs.
        stage = self._open_stage("twoWithinToleranceMeshes.usda")
        # Enable deep transforms but tight tolerance.
        self._execute_json(stage, "deduplicateGeometry_copyValuesTightTolerance.json")
        # With a very tight tollerance there should still be two meshes.
        self.assertEqual(len(_get_unique_mesh_paths(stage)), 2)

    async def test_copy_values_no_dt_execute_command(self):
        """Check that no deep transforms is picked up by _execute_command"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage and get the initial list of unique meshs.
        stage = self._open_stage("twoWithinToleranceMeshes.usda")
        # Disable deep transforms.
        args["considerDeepTransforms"] = False
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        self._execute_command(args)

        # Without deep transforms there should still be two meshes.
        self.assertEqual(len(_get_unique_mesh_paths(stage)), 2)

    async def test_copy_values_tight_tolerance_execute_command(self):
        """Check that tight tolerance is picked up by _execute_command"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage and get the initial list of unique meshs.
        stage = self._open_stage("twoWithinToleranceMeshes.usda")
        # Enable deep transforms but tight tolerance.
        args["tolerance"] = 0.00000001
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        self._execute_command(args)
        # With tight tolerance there should still be two meshes.
        self.assertEqual(len(_get_unique_mesh_paths(stage)), 2)

    async def test_multiple_peer_meshes_to_instanced_references(self):
        """Check that world space points match before and after deduplicate"""
        # Example scene with 4 peer meshes, 3 of which are the same. We expect the 3 that are the same to be de-duped.
        # There should be an extra xform added to one mesh and the other 2 should be instanced references of that one.
        file_name = "deduplicatePeerMeshes.usda"
        file_path = _get_test_data_file_path(file_name)

        # Get a copy of the default arguments for this command then set overrides
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = ["/World//"]
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE

        # Given a lookup table of before and after paths we can assert that the worldspace points match.
        lookup_table = [
            ("/World/Asset/Part/Geom/mesh_0", "/World/Asset/Part/Geom/mesh_0"),
            ("/World/Asset/Part/Geom/mesh_1", "/World/Asset/Part/Geom/mesh_1/Geometry"),
            ("/World/Asset/Part/Geom/mesh_2", "/World/Asset/Part/Geom/mesh_2/Geometry"),
            ("/World/Asset/Part/Geom/mesh_3", "/World/Asset/Part/Geom/mesh_3/Geometry"),
        ]

        # Get a handle to the stage and run the command.
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        # Get a handle to the stage in its original form.
        layer = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer)

        # Construct an xform cache to speed up local to world calculations.
        xformCache = UsdGeom.XformCache()

        # Iterate over before and after pair makign assertions
        for path_before, path_after in lookup_table:

            # Get the matching prims from the before and after stages.
            prim_before = stage_before.GetPrimAtPath(path_before)
            prim_after = stage_after.GetPrimAtPath(path_after)

            # Assert that the expected prims exist in the stages.
            self.assertTrue(prim_before.IsValid())
            self.assertTrue(prim_after.IsValid())

            # Assert that the points values match before and after command execution.
            self.assertWorldspacePointsEqual(prim_before, prim_after, xformCache, tolerance=0.0000001)

        # Check that any prims which were Mesh before and are Xform after have had the schema attributes that no longer
        # apply removed.
        names = set(UsdGeom.Mesh.GetSchemaAttributeNames()) - set(UsdGeom.Xform.GetSchemaAttributeNames())
        # Remove primvars as they can be inherited.
        names.remove("primvars:displayColor")
        names.remove("primvars:displayOpacity")

        # Iterate over the paths of mesh prims before the command was run.
        for path, _ in lookup_table:
            prim = prim_after.GetPrimAtPath(path)

            # Assert that xform prims do not have any Mesh properties.
            if UsdGeom.Xform(prim):
                for name in names:
                    self.assertFalse(prim.HasAttribute(name))

    async def test_instance_proxies_as_input(self):
        """Check that we do not attempt to modify instance proxies during deduplicate"""
        # Example scene containing 6 meshes that are all the same mesh, but split over 3 materials, there is also an
        # instance of the whole asset in the scene. We expect the meshes with matching materials to be de-duped. The
        # instanced prims should pick up the de-dupe via composition, but there should be no direct edits.
        file_name = "deduplicateWithMaterials.usda"
        file_path = _get_test_data_file_path(file_name)

        # Get a copy of the default arguments for this command then set overrides
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.05

        # Given a lookup table of before and after paths we can assert that the worldspace points match.
        lookup_table = [
            ("/World/Asset/Geom/mesh_0", "/World/Asset/Geom/mesh_0/Geometry"),
            ("/World/Asset/Geom/mesh_1", "/World/Asset/Geom/mesh_1/Geometry"),
            ("/World/Asset/Geom/mesh_2", "/World/Asset/Geom/mesh_2/Geometry"),
            ("/World/Asset/Geom/mesh_3", "/World/Asset/Geom/mesh_3/Geometry"),
            ("/World/Asset/Geom/mesh_4", "/World/Asset/Geom/mesh_4/Geometry"),
            ("/World/Asset/Geom/mesh_5", "/World/Asset/Geom/mesh_5/Geometry"),
            # mesh_6 and mesh_7 have UsdGeomSubsets so do not get instanced ... yet.
            ("/World/Asset/Geom/mesh_6", "/World/Asset/Geom/mesh_6"),
            ("/World/Asset/Geom/mesh_7", "/World/Asset/Geom/mesh_7"),
            # The meshes below "AssetInstance" will not have been modified but because the prims that they reference
            # have been changed they will inherit the changes.
            ("/World/AssetInstance/Geom/mesh_0", "/World/AssetInstance/Geom/mesh_0/Geometry"),
            ("/World/AssetInstance/Geom/mesh_1", "/World/AssetInstance/Geom/mesh_1/Geometry"),
            ("/World/AssetInstance/Geom/mesh_2", "/World/AssetInstance/Geom/mesh_2/Geometry"),
            ("/World/AssetInstance/Geom/mesh_3", "/World/AssetInstance/Geom/mesh_3/Geometry"),
            ("/World/AssetInstance/Geom/mesh_4", "/World/AssetInstance/Geom/mesh_4/Geometry"),
            ("/World/AssetInstance/Geom/mesh_5", "/World/AssetInstance/Geom/mesh_5/Geometry"),
            ("/World/AssetInstance/Geom/mesh_6", "/World/AssetInstance/Geom/mesh_6"),
            ("/World/AssetInstance/Geom/mesh_7", "/World/AssetInstance/Geom/mesh_7"),
        ]

        # Get a handle to the stage and run the command.
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        # Get a handle to the stage in its original form.
        layer = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer)

        # Get paths of meshes in the stage before and after de-duplication.
        meshes_before, instanced_meshes_before = _get_mesh_paths(stage_before)
        meshes_after, instanced_meshes_after = _get_mesh_paths(stage_after)

        expected = len(meshes_before) + len(instanced_meshes_before)
        returned = len(meshes_after) + len(instanced_meshes_after)
        self.assertEqual(returned, expected)

        # Assert the expected number of meshes instanced after de-duplication.
        expected = 5
        returned = len(instanced_meshes_after) - len(instanced_meshes_before)
        self.assertEqual(returned, expected)

        # Assert that the lookup table covers all the meshes both before and after.
        expected = sorted(Sdf.Path(x) for x, _ in lookup_table)
        returned = sorted(meshes_before + instanced_meshes_before)
        self.assertEqual(returned, expected)

        expected = sorted(Sdf.Path(x) for _, x in lookup_table)
        returned = sorted(meshes_after + instanced_meshes_after)
        self.assertEqual(returned, expected)

        # Construct a cache to speed up local to world calculations.
        xform_cache = UsdGeom.XformCache()

        # Iterate over before and after pair makign assertions
        for path_before, path_after in lookup_table:

            # Get the matching prims from the before and after stages.
            prim_before = stage_before.GetPrimAtPath(path_before)
            prim_after = stage_after.GetPrimAtPath(path_after)

            # Assert that the expected prims exist in the stages.
            self.assertTrue(prim_before.IsValid())
            self.assertTrue(prim_after.IsValid())

            # Assert that the points values match before and after command execution.
            self.assertWorldspacePointsEqual(prim_before, prim_after, xform_cache, tolerance=args["tolerance"])

            # Assert that the materials bound before and after have the same paths.
            self.assertBoundMaterialsEqual(prim_before, prim_after)

            # If the original mesh prim had MaterialBindingAPI, the post-dedup
            # xform prim must also carry MaterialBindingAPI so the relationship is schema-valid.
            # note: the case where the schema is missing from the origin prim is tested in: test_material_binding_api_schema_added_to_xform
            if "MaterialBindingAPI" in prim_before.GetAppliedSchemas():
                # the xform prim is at the path of the before prim
                xform_prim = stage_after.GetPrimAtPath(Sdf.Path(path_before))
                if xform_prim.IsValid() and UsdGeom.Xform(xform_prim):
                    self.assertIn(
                        "MaterialBindingAPI",
                        xform_prim.GetAppliedSchemas(),
                        f"Xform {xform_prim.GetPath()} is missing MaterialBindingAPI schema",
                    )

    async def test_material_binding_api_schema_added_to_xform(self):
        """Check that MaterialBindingAPI is present on the new Xform prim even when the source mesh
        had a material:binding relationship but was missing the MaterialBindingAPI schema declaration."""
        # Load a scene where the mesh prims intentionally omit 'prepend apiSchemas = ["MaterialBindingAPI"]'
        # while still carrying a material:binding relationship.  After deduplication the resulting Xform
        # must have MaterialBindingAPI applied (added by the safety-check path).
        stage = self._open_stage("deduplicateMissingMaterialBindingAPI.usda")

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.05
        self._execute_command(args)

        # Both mesh prims should have been deduplicated into Xform + Geometry children.
        for prim_path in ("/World/mesh_a", "/World/mesh_b"):
            xform_prim = stage.GetPrimAtPath(prim_path)
            self.assertTrue(xform_prim.IsValid(), f"Prim {prim_path} not found after dedup")
            self.assertTrue(UsdGeom.Xform(xform_prim), f"{prim_path} should be an Xform after dedup")
            self.assertIn(
                "MaterialBindingAPI",
                xform_prim.GetAppliedSchemas(),
                f"{prim_path} is missing MaterialBindingAPI schema after dedup",
            )

    async def test_crash_with_empty_extent(self):
        """Check that we do not crash when deduplicate operations are run in series"""
        # The crash in this case was caused by meshes that do not have extent values defined being deduplicated multiple
        # times. During the copy values run the extent attribute changes from un-authored to authored, but the value is
        # still empty. Subsequently the second run crashes when it finds an authored extent that does not hold 2 values.

        # We assert this here simply because it was reported and we need a test to cover the case.

        # Load a simple scene that has 4 cubes that do not have extent values.
        self._open_stage("simpleFourCubes.usda")

        # Get a copy of the default arguments for this command then set overrides.
        args = DEFAULT_ARGS.copy()
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.05

        # Run once with copy values.
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        self._execute_command(args)

        # Run a second time with instanceable references.
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        self._execute_command(args)

        # Just getting this far proves the crash is gone.

    async def test_deduplicate_payload_with_transforms(self):
        """Check that before and after point positions are correct when duplicate prims are coming from a payload"""
        # Example scene with 2 peer meshes that both have transforms and come from a payload.
        # They need to have the xforms as well as be payloads so that we get mixed strength when the instanceable
        # reference arcs are added.

        # Get a copy of the default arguments for this command then set overrides
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = ["/World//"]
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE

        # Given a lookup table of before and after paths we can assert that the worldspace points match.
        lookup_table = [
            ("/World/mesh0", "/World/mesh0/Geometry"),
            ("/World/mesh1", "/World/mesh1/Geometry"),
        ]

        # Get a handle to the stage and run the command.
        file_name = "deduplicateWithTransforms_wrapper.usda"
        file_path = _get_test_data_file_path(file_name)
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        # Get a handle to the stage in its original form.
        # Due to an annoying bug in path resolution we need the same data in a different file rather than opening the
        # layer as anonymous.
        file_name = "deduplicateWithTransforms_wrapper2.usda"
        file_path = _get_test_data_file_path(file_name)
        layer = Sdf.Layer.FindOrOpen(file_path)
        stage_before = Usd.Stage.Open(layer)

        # Construct an xform cache to speed up local to world calculations.
        xformCache = UsdGeom.XformCache()

        # Iterate over before and after pair making assertions
        for path_before, path_after in lookup_table:

            # Get the matching prims from the before and after stages.
            prim_before = stage_before.GetPrimAtPath(path_before)
            prim_after = stage_after.GetPrimAtPath(path_after)

            # Assert that the expected prims exist in the stages.
            self.assertTrue(prim_before.IsValid())
            self.assertTrue(prim_after.IsValid())

            # Assert that the points values match within tolerance before and after command execution.
            self.assertWorldspacePointsEqual(prim_before, prim_after, xformCache)

    async def test_deduplicate_nearly_planar_meshes(self):
        """Check deduplication of planar and nearly planar meshes."""
        # Example scene with 2 planar meshes, one being a deep transformed version of the other.
        # The second, due to rounding errors during deep transform being only nearly planar.
        # Thus, the scene contains a planar and a nearly planar mesh that should be deduplicated.

        # Get a copy of the default arguments for this command then set overrides
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES

        # Get a handle to the stage and run the command.
        file_name = "deduplicate_planes.usda"
        file_path = _get_test_data_file_path(file_name)
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        # Check that nearly planar mesh has been deduplicated, and number of unique meshes is reduced to 1.
        self.assertEqual(len(_get_unique_mesh_paths(stage_after)), 1)

        # General check that objects remain at the place, before and after the operation.
        layer = Sdf.Layer.FindOrOpen(file_path)
        stage_before = Usd.Stage.Open(layer)
        self.assertWorldspaceScenePointsEqual(stage_before, stage_after, None)

    async def test_transform_of_near_planar_meshes(self):
        """Check that planar non-planar meshes can be deduplicated with "reasonable" transforms"""
        # Example scene with 2 peer meshes that both both planes, but one has been deep rotated.

        # Get a copy of the default arguments for this command then set overrides
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = ["/World/mesh_0", "/World/mesh_1"]
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES

        # Get a handle to the stage and run the command.
        file_name = "deduplicate_planes.usda"
        file_path = _get_test_data_file_path(file_name)
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        # Get the resulting transform of the prim values were copied to.
        prim = stage_after.GetPrimAtPath("/World/mesh_0")
        xformCache = UsdGeom.XformCache()
        matrix = xformCache.GetLocalToWorldTransform(prim)
        transform = Gf.Transform(matrix)

        # The scale should be near identity as the mesh only required rotating.
        msg = "Scale of {} differs from identity".format(transform.GetScale())
        expected = (1.0, 1.0, 1.0)
        returned = transform.GetScale()
        self.assertAlmostEqual(returned[0], expected[0], places=6, msg=msg)
        self.assertAlmostEqual(returned[1], expected[1], places=6, msg=msg)
        self.assertAlmostEqual(returned[2], expected[2], places=6, msg=msg)

    async def test_specifier_unchanged(self):
        """Check that deduplicate does not modify the specifier of prims"""
        # This test case contains meshes that are composed from references to prims with class and over specifiers.
        stage = self._open_stage("abstractPrims_input.usda")

        # Assert the initial state of the specifiers
        self.assertEqual(stage.GetPrimAtPath("/World/Classes").GetSpecifier(), Sdf.SpecifierClass)
        self.assertEqual(stage.GetPrimAtPath("/World/Meshes").GetSpecifier(), Sdf.SpecifierOver)

        # Get a copy of the default arguments for this command then set overrides.
        args = DEFAULT_ARGS.copy()
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.05

        # Run once with copy values.
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        self._execute_command(args)

        # Assert that the specifiers are still the same
        self.assertEqual(stage.GetPrimAtPath("/World/Classes").GetSpecifier(), Sdf.SpecifierClass)
        self.assertEqual(stage.GetPrimAtPath("/World/Meshes").GetSpecifier(), Sdf.SpecifierOver)

        # Reopen the stage
        stage = self._open_stage("abstractPrims_input.usda")

        # Run a second time with instanceable references.
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        self._execute_command(args)

        # Assert that the specifiers are still the same
        self.assertEqual(stage.GetPrimAtPath("/World/Classes").GetSpecifier(), Sdf.SpecifierClass)
        self.assertEqual(stage.GetPrimAtPath("/World/Meshes").GetSpecifier(), Sdf.SpecifierOver)

    async def test_scaled_deduplicates_with_tolerance(self):
        """Check that before and after meshes are within tolerance values when scaling is applied"""
        # Example scene with 2 peer meshes that have large scale difference and some point jitter applied.
        # If they are seen as duplicates we expect the before and after points to have moved no more than the tolerance
        # value in worldspace.
        file_name = "deduplicateTolerance.usda"
        file_path = _get_test_data_file_path(file_name)

        # Construct an xform cache to speed up local to world calculations.
        xformCache = UsdGeom.XformCache()

        # Get a copy of the default arguments for this command then set overrides
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = ["/World/Mesh_1", "/World/Mesh_0"]
        args["duplicateMethod"] = DUPLICATE_METHOD_REFERENCE
        args["tolerance"] = 1.1

        # Get a handle to the stage and run the command.
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        # Get a handle to the stage in its original form.
        layer = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer)

        # Iterate over path in the before and after stage making assertions.
        for path in args["meshPrimPaths"]:

            # Get the matching prims from the before and after stages.
            prim_before = stage_before.GetPrimAtPath(path)
            prim_after = stage_after.GetPrimAtPath(path)

            # Assert that the expected prims exist in the stages.
            self.assertTrue(prim_before.IsValid())
            self.assertTrue(prim_after.IsValid())

            # Assert that the points values match within tolerance before and after command execution.
            self.assertWorldspacePointsEqual(prim_before, prim_after, xformCache, tolerance=args["tolerance"])

    async def test_normals(self):
        """Check that the different styles of normals expression are all supported"""
        file_name = "normals_options.usda"
        file_path = _get_test_data_file_path(file_name)

        # Open the stage and get the initial list of unique meshs.
        stage = self._open_stage(file_name)
        unique_meshes_before = _get_unique_mesh_paths(stage)

        # RTX finds 10 unique meshes in this scene so assert that we find the same.
        self.assertEqual(len(unique_meshes_before), 10)

        paths = [
            "/none/mesh_0",
            "/attr_uniform/mesh_0",
            "/attr_vertex/mesh_0",
            "/attr_facevarying/mesh_0",
            "/primvar_uniform/mesh_0",
            "/primvar_vertex/mesh_0",
            "/primvar_facevarying/mesh_0",
            "/primvar_uniform_indexed/mesh_0",
            "/primvar_vertex_indexed/mesh_0",
            "/primvar_facevarying_indexed/mesh_0",
        ]

        # Execute the deduplicate command using copy values.
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = paths
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.05
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        self._execute_command(args)

        # Assert that there are less unique meshes after execution.
        unique_meshes_after = _get_unique_mesh_paths(stage)
        self.assertTrue(len(unique_meshes_before) > len(unique_meshes_after))

        # The logic currently results in 4 unique meshes.
        self.assertEqual(len(unique_meshes_after), 4)

        # Get a handle to the stage in its original form.
        layer = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer)

        # Construct an xform cache to speed up local to world calculations.
        xformCache = UsdGeom.XformCache()

        # Iterate over path in the before and after stage making assertions.
        for path in paths:

            # Get the matching prims from the before and after stages.
            prim_before = stage_before.GetPrimAtPath(path)
            prim_after = stage.GetPrimAtPath(path)

            # Assert that the expected prims exist in the stages.
            self.assertTrue(prim_before.IsValid())
            self.assertTrue(prim_after.IsValid())

            # Assert that the points values match within tolerance before and after command execution.
            self.assertWorldspacePointsEqual(prim_before, prim_after, xformCache, tolerance=args["tolerance"])

    async def test_data_volume_ignored_in_buckets(self):
        """Check that data volume of prims does not cause multiple buckets"""
        # When "Merge Static Meshes" uses the Bucket class it wants to split Buckets to ensure that the
        # total point count stay below the array size limit. However "Deduplicate Geometry" does not have
        # this concern and we do not want to artificially split prim sets that are actually duplicates.

        # Open the stage.
        file_name = "maxDataVolume.usdc"
        stage = self._open_stage(file_name)

        # Assert that there are no prototypes before deduplicate has run
        expected = 0
        returned = len(stage.GetPrototypes())
        self.assertEqual(returned, expected)

        # Execute the deduplicate command using instanceable references
        args = DEFAULT_ARGS.copy()
        args["considerDeepTransforms"] = True
        args["tolerance"] = 10000.0  # Use crazy tolerance to ensure all prims are equal
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE

        self._execute_command(args)

        # Assert that there is one prototype after deduplicate has run
        expected = 1
        returned = len(stage.GetPrototypes())
        self.assertEqual(returned, expected)

        # Assert that there is one non instanced Mesh after deduplicate has run
        expected = 1
        returned = len([x for x in stage.Traverse() if x.GetTypeName() == "Mesh"])
        self.assertEqual(returned, expected)

    async def test_complex_composition_crash(self):
        """Check that deduplicate does not cause crashes when complex composition is at play"""
        # Previously the transform setting code had caused crashes because, after checking if prims
        # had a named xform op we would blindly add them, however as composition was updated some prims
        # would inherit an xform op and the name clash were fatal.

        file_name = "various_construction_arcs.usda"

        # Use this path order so that the target prims are those taht have complex composition.
        args = DEFAULT_ARGS.copy()
        args["meshPrimPaths"] = ["/World_Copy", "/World"]
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.05

        # Execute the deduplicate command using copy values.
        self._open_stage(file_name)
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        self._execute_command(args)

        # Execute the deduplicate command using instanceable references.
        self._open_stage(file_name)
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        self._execute_command(args)

        # Reaching this point indicates that we have not crashed.

    async def test_deduplicate_geometry_fuzzy(self):
        """Test deduplicate geometry (fuzzy)"""

        # Test DUPLICATE_METHOD_SET_ATTRIBUTE
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_SET_ATTRIBUTE
        args["fuzzy"] = True

        expectedSets0 = []
        expectedSets1 = []

        for testCase in range(4):

            if testCase == 0:

                args["tolerance"] = 0.01
                args["allowScaling"] = False
                expectedSets0 = [1, 1, 0, 0, 0, 0]
                expectedSets1 = [1, 1, 0, 0, 0, 0]

            elif testCase == 1:

                args["tolerance"] = 0.01
                args["allowScaling"] = True
                expectedSets1 = [1, 1, 1, 0, 2, 2]
                expectedSets0 = [2, 2, 2, 0, 1, 1]

            elif testCase == 2:

                args["tolerance"] = 0.1
                args["allowScaling"] = False
                expectedSets0 = [1, 1, 0, 2, 2, 0]
                expectedSets1 = [2, 2, 0, 1, 1, 0]

            elif testCase == 3:

                args["tolerance"] = 0.1
                args["allowScaling"] = True
                expectedSets0 = [1, 1, 1, 2, 2, 2]
                expectedSets1 = [2, 2, 2, 1, 1, 1]

            stage = self._open_stage("fuzzyDedupTest.usda")

            success, result = self._execute_command(args)

            self.assertTrue(success)

            sets = []

            sets.append(stage.GetPrimAtPath("/box1/box1").GetAttribute("duplicationSet").Get())
            sets.append(stage.GetPrimAtPath("/box2/box2").GetAttribute("duplicationSet").Get())
            sets.append(stage.GetPrimAtPath("/box3/box3").GetAttribute("duplicationSet").Get())

            sets.append(stage.GetPrimAtPath("/torus1/torus1").GetAttribute("duplicationSet").Get())
            sets.append(stage.GetPrimAtPath("/torus2/torus2").GetAttribute("duplicationSet").Get())
            sets.append(stage.GetPrimAtPath("/torus3/torus3").GetAttribute("duplicationSet").Get())

            self.assertTrue(sets == expectedSets0 or sets == expectedSets1)

        # Test DUPLICATE_METHOD_REFERENCE
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_REFERENCE
        args["fuzzy"] = True
        args["useGpu"] = False
        args["tolerance"] = 0.1
        args["allowScaling"] = True

        stage = self._open_stage("fuzzyDedupTest.usda")

        success, result = self._execute_command(args)

        self.assertTrue(success)

        boxes = ["/box1/box1", "/box2/box2", "/box3/box3"]
        toruses = ["/torus1/torus1", "/torus2/torus2", "/torus3/torus3"]

        numReferences = 0
        for box in boxes:
            if stage.GetPrimAtPath(box).HasAuthoredReferences():
                numReferences += 1

        self.assertTrue(numReferences == 1)

        numReferences = 0
        for torus in toruses:
            if stage.GetPrimAtPath(torus).HasAuthoredReferences():
                numReferences += 1

        self.assertTrue(numReferences == 1)

        # Test DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["fuzzy"] = True
        args["useGpu"] = False
        args["tolerance"] = 0.1
        args["allowScaling"] = True

        stage = self._open_stage("fuzzyDedupTest.usda")

        success, result = self._execute_command(args)

        self.assertTrue(success)

        numReferences = 0
        for box in boxes:
            if stage.GetPrimAtPath(box).HasAuthoredReferences():
                numReferences += 1

        self.assertTrue(numReferences == 1)

        numReferences = 0
        for torus in toruses:
            if stage.GetPrimAtPath(torus).HasAuthoredReferences():
                numReferences += 1

        self.assertTrue(numReferences == 1)

        # Test DUPLICATE_METHOD_COPYVALUES
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES
        args["fuzzy"] = True
        args["useGpu"] = False
        args["tolerance"] = 0.1
        args["allowScaling"] = True

        stage = self._open_stage("fuzzyDedupTest.usda")

        success, result = self._execute_command(args)

        self.assertTrue(success)

        allPrimNames = [
            ["/torus1/torus1", "/torus2/torus2", "/torus3/torus3"],
            ["/box1/box1", "/box2/box2", "/box3/box3"],
        ]

        for i in range(len(allPrimNames)):
            equalPrimNames = allPrimNames[i]

            # Test whether the prims have the same primvars (checking the lengths only)
            sizes = []

            for j in range(len(equalPrimNames)):
                prim = stage.GetPrimAtPath(equalPrimNames[j])
                api = UsdGeom.PrimvarsAPI(prim)
                primvars = api.GetPrimvars()

                for k in range(len(primvars)):
                    primvar = primvars[k]
                    value = primvar.Get()
                    size = len(value) if value is not None else 0
                    if j == 0:
                        sizes.append(size)
                    else:
                        self.assertTrue(size == sizes[k])

        # Test DUPLICATE_METHOD_INSTANCEABLEREFERENCE with constant color and varied tesselation
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["fuzzy"] = True
        args["tolerance"] = 0.1
        args["allowScaling"] = True

        stage = self._open_stage("fuzzyDedupConstantColorTest.usda")

        success, result = self._execute_command(args)

        self.assertTrue(success)

        numReferences = 0
        for box in boxes:
            if stage.GetPrimAtPath(box).HasAuthoredReferences():
                numReferences += 1

        self.assertTrue(numReferences == 1)

    async def test_deduplicate_geometry_fuzzy_world(self):
        """Test fuzzy mode with meshes that have the same xform but different world points"""

        stage = self._open_stage("deduplicateGeometryFuzzyDeep.usda")

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["fuzzy"] = True
        args["tolerance"] = 0.5

        # duplicate cubes are currently meshes
        self.assertTrue(stage.GetPrimAtPath("/World/Cube").IsA(UsdGeom.Mesh))
        self.assertTrue(stage.GetPrimAtPath("/World/Cube2").IsA(UsdGeom.Mesh))

        success, result = self._execute_command(args)
        self.assertTrue(success)
        self.assertTrue(result[0])

        # duplicate cube is now an xform
        self.assertTrue(stage.GetPrimAtPath("/World/Cube2").IsA(UsdGeom.Xform))
        self.assertTrue(stage.GetPrimAtPath("/World/Cube2/Geometry").IsA(UsdGeom.Mesh))

        # Verify positions
        bboxCache = UsdGeom.BBoxCache(Usd.TimeCode.Default(), [UsdGeom.Tokens.default_])

        cube1 = stage.GetPrimAtPath("/World/Cube/Geometry")
        bounds = bboxCache.ComputeWorldBound(cube1)
        centroid = bounds.ComputeCentroid()
        self.assertTrue(Gf.IsClose(centroid, Gf.Vec3d(0, 0, 0), 0.001))

        cube2 = stage.GetPrimAtPath("/World/Cube2/Geometry")
        bounds = bboxCache.ComputeWorldBound(cube2)
        centroid = bounds.ComputeCentroid()
        self.assertTrue(Gf.IsClose(centroid, Gf.Vec3d(100, 100, 100), 0.001))

    async def test_time_varying_meshes(self):
        """Test deduplicate operation on meshes with authored time varying attributes, the mesh should not be processed"""
        # Get a copy of the default arguments for this command
        args = DEFAULT_ARGS.copy()
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")
        # run command
        success, result = self._execute_command(args)

        # asserts success of execution
        self.assertTrue(success)

        # currently skipping time sampled meshes to avoid corrupting the scene
        # test to be expanded when time samples are better handled in the operation
        # assert that no meshes have been turned into instances
        meshes = _get_meshes(stage)
        for mesh in meshes:
            self.assertFalse(mesh.IsInstance())

    def assert_pivot(self, prim, pivot):
        """Assert the expected value of a pivot"""
        pivotVal = prim.GetAttribute("xformOp:translate:pivot").Get()
        self.assertEqual(pivotVal, pivot)

    def get_worldspace_pivot(self, prim):
        """Returns the pivot of the given prim in worldspace"""
        xformable = UsdGeom.Xformable(prim)

        pivot_local = Gf.Vec3d(prim.GetAttribute("xformOp:translate:pivot").Get())

        # Build matrix from ops that come before the pivot op
        pre_pivot_matrix = Gf.Matrix4d(1)
        for op in xformable.GetOrderedXformOps():
            if op.GetOpName() == "xformOp:translate:pivot":
                break
            pre_pivot_matrix = pre_pivot_matrix * op.GetOpTransform(Usd.TimeCode.Default())

        # Get parent's local-to-world transform
        xform_cache = UsdGeom.XformCache(Usd.TimeCode.Default())
        parent_to_world = xform_cache.GetLocalToWorldTransform(prim.GetParent())

        return Gf.Vec3d((pre_pivot_matrix * parent_to_world).Transform(pivot_local))

    def get_naive_worldspace_pivot(self, prim):
        """Where the pivot widget lands if you just transform pivot_local through the prim's
        full local-to-world. Many viewers/editors draw the pivot this way, so it has to
        agree with the rotation-center calculation above. A regression that puts the pivot
        value in the wrong space (e.g. target-local while dedupTransform is still in L2W)
        shows up here as a shift by the dedupTransform's translation.
        """
        xform_cache = UsdGeom.XformCache(Usd.TimeCode.Default())
        pivot_local = Gf.Vec3d(prim.GetAttribute("xformOp:translate:pivot").Get())
        return Gf.Vec3d(xform_cache.GetLocalToWorldTransform(prim).Transform(pivot_local))

    async def test_deduplicate_inverse_pivot_copy(self):
        """Test deduplicating meshes with a pivot using copyValues"""

        stage = self._open_stage("deduplicateGeometryPivot.usda")

        cube = stage.GetPrimAtPath("/World/Cube")
        cubeDup = stage.GetPrimAtPath("/World/CubeDuplicate")
        cubeDupPivot = stage.GetPrimAtPath("/World/CubeDuplicateCustomPivot")
        cubeDupAlt = stage.GetPrimAtPath("/World/CubeDuplicateAlternateTopology")

        pointsCube = cube.GetAttribute("points").Get()
        pointsDup = cubeDup.GetAttribute("points").Get()
        pointsDupPivot = cubeDupPivot.GetAttribute("points").Get()
        pointsDupAlt = cubeDupAlt.GetAttribute("points").Get()

        # Assert initial state - cube and cubeDup have the same vertices,
        # the others are both different.
        self.assertEqual(pointsCube, pointsDup)
        self.assertNotEqual(pointsCube, pointsDupPivot)
        self.assertNotEqual(pointsCube, pointsDupAlt)
        self.assertNotEqual(pointsDupPivot, pointsDupAlt)

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES

        success, result = self._execute_command(args)
        self.assertTrue(success)

        # Assert all cubes have matching vertices after deduplication
        pointsCube = cube.GetAttribute("points").Get()
        pointsDup = cubeDup.GetAttribute("points").Get()
        pointsDupPivot = cubeDupPivot.GetAttribute("points").Get()
        pointsDupAlt = cubeDupAlt.GetAttribute("points").Get()

        # Assert new topology state
        self.assertEqual(pointsCube, pointsDup)
        self.assertEqual(pointsCube, pointsDupPivot)
        self.assertEqual(pointsCube, pointsDupAlt)

        # Assert pivot values
        self.assert_pivot(cube, Gf.Vec3d(-175, -175, -175))
        self.assert_pivot(cubeDup, Gf.Vec3d(-175, -175, -175))
        self.assert_pivot(cubeDupAlt, Gf.Vec3d(-175, -175, -175))
        # This is the main difference (custom pivot)
        self.assert_pivot(cubeDupPivot, Gf.Vec3d(-200, -200, -200))

    async def test_deduplicate_inverse_pivot_instance(self):
        """Test deduplicating meshes with a pivot using instanceableref"""

        file_name = "deduplicateGeometryPivot.usda"
        file_path = _get_test_data_file_path(file_name)

        # Capture worldspace point positions from a fresh pre-dedup stage so we can verify
        # that the actual geometry (not just the pivot widget) lands at the same place
        # after deduplication. Worldspace pivot equality alone is not enough: a buggy split
        # that leaves a stray pivot translate on the Geometry child can leave pivot widgets
        # in the right spot while shifting every vertex by the pivot value.
        layer_before = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer_before)
        xformCache_before = UsdGeom.XformCache()
        worldspace_points_before = {
            path: _get_worldspace_points(stage_before.GetPrimAtPath(path), xformCache_before)
            for path in (
                "/World/Cube",
                "/World/CubeDuplicate",
                "/World/CubeDuplicateCustomPivot",
                "/World/CubeDuplicateAlternateTopology",
                "/World/CubeDuplicateAlternateTopologyCustomPivot",
            )
        }

        stage = self._open_stage(file_name)

        cube = stage.GetPrimAtPath("/World/Cube")
        cubeDup = stage.GetPrimAtPath("/World/CubeDuplicate")
        cubeDupPivot = stage.GetPrimAtPath("/World/CubeDuplicateCustomPivot")
        cubeDupAlt = stage.GetPrimAtPath("/World/CubeDuplicateAlternateTopology")
        cubeDupAltPivot = stage.GetPrimAtPath("/World/CubeDuplicateAlternateTopologyCustomPivot")

        self.assertTrue(cube.IsA(UsdGeom.Mesh))
        self.assertTrue(cubeDup.IsA(UsdGeom.Mesh))
        self.assertTrue(cubeDupPivot.IsA(UsdGeom.Mesh))
        self.assertTrue(cubeDupAlt.IsA(UsdGeom.Mesh))
        self.assertTrue(cubeDupAltPivot.IsA(UsdGeom.Mesh))

        # get the worldspace pivots of the meshes so we can check the deduped versions have the same worldspace pivots
        cubeWorldPivot = self.get_worldspace_pivot(cube)
        cubeDupWorldPivot = self.get_worldspace_pivot(cubeDup)
        cubeDupPivotWorldPivot = self.get_worldspace_pivot(cubeDupPivot)
        cubeDupAltWorldPivot = self.get_worldspace_pivot(cubeDupAlt)
        cubeDupAltPivotWorldPivot = self.get_worldspace_pivot(cubeDupAltPivot)

        # Also capture the naive pivot widget position (pivot_local transformed through L2W).
        # The rotation-center computation above only looks at ops listed before the pivot in
        # xformOpOrder, so it stays correct as long as the pivot value is in the right local
        # space relative to those ops. Editors usually draw the pivot widget at the naive
        # L2W-transformed value, which is sensitive to the pivot's coordinate space across
        # the entire transform stack -- including the dedupTransform.
        cubeNaivePivot = self.get_naive_worldspace_pivot(cube)
        cubeDupNaivePivot = self.get_naive_worldspace_pivot(cubeDup)
        cubeDupPivotNaivePivot = self.get_naive_worldspace_pivot(cubeDupPivot)
        cubeDupAltNaivePivot = self.get_naive_worldspace_pivot(cubeDupAlt)
        cubeDupAltPivotNaivePivot = self.get_naive_worldspace_pivot(cubeDupAltPivot)

        # Execute operation
        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)
        self.assertTrue(success)

        # Original prims are now xforms
        self.assertTrue(cube.IsA(UsdGeom.Xform))
        self.assertTrue(cubeDup.IsA(UsdGeom.Xform))
        self.assertTrue(cubeDupPivot.IsA(UsdGeom.Xform))
        self.assertTrue(cubeDupAlt.IsA(UsdGeom.Xform))

        # assert the meshes have the same worldspace pivot values as before
        self.assertEqual(self.get_worldspace_pivot(cube), cubeWorldPivot)
        self.assertEqual(self.get_worldspace_pivot(cubeDup), cubeDupWorldPivot)
        self.assertEqual(self.get_worldspace_pivot(cubeDupPivot), cubeDupPivotWorldPivot)
        self.assertEqual(self.get_worldspace_pivot(cubeDupAlt), cubeDupAltWorldPivot)
        self.assertEqual(self.get_worldspace_pivot(cubeDupAltPivot), cubeDupAltPivotWorldPivot)

        # And assert the naive pivot widget position is preserved too. The dedup must place
        # the pivot value in whatever local space keeps `pivot_local * L2W` invariant -- a
        # regression that leaves the pivot in target-local space shifts the widget by the
        # dedupTransform's translation for every custom-pivot duplicate.
        self.assertEqual(self.get_naive_worldspace_pivot(cube), cubeNaivePivot)
        self.assertEqual(self.get_naive_worldspace_pivot(cubeDup), cubeDupNaivePivot)
        self.assertEqual(self.get_naive_worldspace_pivot(cubeDupPivot), cubeDupPivotNaivePivot)
        self.assertEqual(self.get_naive_worldspace_pivot(cubeDupAlt), cubeDupAltNaivePivot)
        self.assertEqual(self.get_naive_worldspace_pivot(cubeDupAltPivot), cubeDupAltPivotNaivePivot)

        # The deduplicated geometry must occupy the same world-space positions it did before
        # the operation ran. This catches pivot-handling regressions that ws_pivot misses --
        # e.g. a stray pivot translate left on the Geometry child by the split would shift
        # every vertex while still leaving the pivot widget at the right world location.
        xformCache_after = UsdGeom.XformCache()
        for path, points_before in worldspace_points_before.items():
            points_after = _get_worldspace_points(stage.GetPrimAtPath(path), xformCache_after)
            self.assertEqual(
                len(points_before),
                len(points_after),
                f"Point count for {path} changed across dedup ({len(points_before)} -> {len(points_after)})",
            )
            self.assertVec3ArrayAlmostEqual(
                points_before,
                points_after,
                tolerance=1e-6,
                msg=f"World-space points of {path} changed across dedup",
            )

        mesh = stage.GetPrimAtPath("/World/Cube/Geometry")
        meshDup = stage.GetPrimAtPath("/World/CubeDuplicate/Geometry")
        meshPivot = stage.GetPrimAtPath("/World/CubeDuplicateCustomPivot/Geometry")
        meshAlt = stage.GetPrimAtPath("/World/CubeDuplicateAlternateTopology/Geometry")

        # Assert mesh pivot values
        # The meshes are instance proxies, so won't have the custom pivot
        self.assert_pivot(mesh, Gf.Vec3d(-225, -225, -225))
        self.assert_pivot(meshDup, Gf.Vec3d(-225, -225, -225))
        self.assert_pivot(meshPivot, Gf.Vec3d(-225, -225, -225))
        self.assert_pivot(meshAlt, Gf.Vec3d(-225, -225, -225))

    async def test_deduplicate_empty_expression(self):
        """Test that finding no prims to deduplicate does not crash"""

        # Use Fuzzy mode which is where the crash originated.
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["fuzzy"] = True
        args["tolerance"] = 0.1
        args["allowScaling"] = True
        args["meshPrimPaths"] = ["/World/Foo/Invalid/Path"]

        self._open_stage("fuzzyDedupConstantColorTest.usda")

        # Execute, and then assert the result. That's all we need to do here, we are
        # validating that finding no prims does not cause a crash.
        success, result = self._execute_command(args)
        self.assertTrue(success)
        self.assertTrue(result[0])

    async def test_deduplicate_analysis(self):
        """Test analysis mode"""

        stage = self._open_stage("fuzzyDedupTest.usda")

        # Create analysis context
        context = _get_context(stage, analysis=True)

        # Configure
        args = DEFAULT_ARGS.copy()
        args["fuzzy"] = True
        args["allowScaling"] = True

        # Test with the default deduplicate method that uses composition
        success, result = self._execute_command(args, context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Should be two sets
        self.assertEqual(len(analysis), 2)

        set1 = analysis[0]
        self.assertEqual(len(set1), 2)
        self.assertIn("/box1/box1", set1)
        self.assertIn("/box3/box3", set1)

        set2 = analysis[1]
        self.assertEqual(len(set2), 2)
        self.assertIn("/torus2/torus2", set2)
        self.assertIn("/torus3/torus3", set2)

        # Test again, with a non-composition based deduplication
        args["duplicateMethod"] = DUPLICATE_METHOD_COPYVALUES

        success, result = self._execute_command(args, context)

        self.assertTrue(success)
        self.assertTrue(result[0])

        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Should be two sets still
        self.assertEqual(len(analysis), 2)

        # This set however has an extra entry
        set1 = analysis[0]
        self.assertEqual(len(set1), 3)
        self.assertIn("/box1/box1", set1)
        self.assertIn("/box2/box2", set1)
        self.assertIn("/box3/box3", set1)

        set2 = analysis[1]
        self.assertEqual(len(set2), 2)
        self.assertIn("/torus2/torus2", set2)
        self.assertIn("/torus3/torus3", set2)

    async def test_deduplicate_curves(self):
        """Test deduplicating basis curves"""

        stage = self._open_stage("duplicateCurves.usda")
        context = _get_context(stage)

        # Configure
        args = DEFAULT_ARGS.copy()

        duplicate_paths = [
            "/World/Duplicate1",
            "/World/Duplicate2",
            "/World/Duplicate3",
            "/World/Duplicate4",
            "/World/Duplicate5",
        ]
        unique_paths = ["/World/UniquePoints", "/World/UniquePrimvar", "/World/UniqueWidths"]

        # Initially all "duplicate" prims are explicit curves/not instances
        for prim_path in duplicate_paths:
            self.assertTrue(stage.GetPrimAtPath(prim_path).IsA(UsdGeom.BasisCurves))
            self.assertFalse(stage.GetPrimAtPath(prim_path).IsInstance())

        # Initially all "unique" prims are also explicit/not instances
        for prim_path in unique_paths:
            self.assertTrue(stage.GetPrimAtPath(prim_path).IsA(UsdGeom.BasisCurves))
            self.assertFalse(stage.GetPrimAtPath(prim_path).IsInstance())

        # Run operation
        success, result = self._execute_command(args, context)

        # After execution, duplicate prims (minus the prototype) are now instances
        instance_paths = duplicate_paths.copy()
        instance_paths.remove("/World/Duplicate1")  # remove prototype

        # Check the expected number of instances
        # (in this case, that is the number of things deduplicated)
        instances = _get_instances(stage)
        self.assertEqual(len(instances), 4)
        # This check is essentially that all instances were recorded in the original
        # duplicate_paths and will therefore be tested
        self.assertEqual(len(instances), len(instance_paths))

        # Verify prototype prim has changed to include an xform, but itself is not an instance
        self.assertTrue(stage.GetPrimAtPath("/World/Duplicate1").IsA(UsdGeom.Xform))
        self.assertFalse(stage.GetPrimAtPath("/World/Duplicate1").IsInstance())
        self.assertTrue(stage.GetPrimAtPath("/World/Duplicate1/Geometry").IsA(UsdGeom.BasisCurves))

        # Verify the other duplicates are now instances of the prototype
        for prim_path in instance_paths:
            self.assertTrue(stage.GetPrimAtPath(prim_path).IsA(UsdGeom.Xform))
            self.assertTrue(stage.GetPrimAtPath(prim_path).IsInstance())
            self.assertTrue(stage.GetPrimAtPath(prim_path + "/Geometry").IsA(UsdGeom.BasisCurves))
            self.assertTrue(stage.GetPrimAtPath(prim_path + "/Geometry").IsInstanceProxy())

        # Unique prims are still unique
        for prim_path in unique_paths:
            self.assertTrue(stage.GetPrimAtPath(prim_path).IsA(UsdGeom.BasisCurves))
            self.assertFalse(stage.GetPrimAtPath(prim_path).IsInstance())

        # Test that a mesh/curve with the same points did not deduplicate
        self.assertTrue(stage.GetPrimAtPath("/World/UniqueCurve").IsA(UsdGeom.BasisCurves))
        self.assertFalse(stage.GetPrimAtPath("/World/UniqueCurve").IsInstance())
        self.assertTrue(stage.GetPrimAtPath("/World/UniqueCube").IsA(UsdGeom.Mesh))
        self.assertFalse(stage.GetPrimAtPath("/World/UniqueCube").IsInstance())

    async def test_deduplicate_ignore_attributes(self):
        """Test dedup when ignoring attributes"""

        stage = self._open_stage("dedupIgnore.usda")
        context = _get_context(stage, analysis=True)

        args = DEFAULT_ARGS.copy()

        success, result = self._execute_command(args, context)
        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # Initially no results
        self.assertEqual(len(analysis), 0)

        # Run again, ignoring primvars: namespace
        args["ignoreAttributes"] = ["primvars:"]

        success, result = self._execute_command(args, context)
        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        prims = analysis[0]
        self.assertEqual(len(prims), 3)
        self.assertIn("/World/Cube_01", prims)
        self.assertIn("/World/Cube_02", prims)
        self.assertIn("/World/Cube_03", prims)

        # Run a third time, with an explicit attribute to ignore
        args["ignoreAttributes"] = ["primvars:", "uniqueAttribute"]

        success, result = self._execute_command(args, context)
        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        prims = analysis[0]
        self.assertEqual(len(prims), 4)
        self.assertIn("/World/Cube", prims)
        self.assertIn("/World/Cube_01", prims)
        self.assertIn("/World/Cube_02", prims)
        self.assertIn("/World/Cube_03", prims)

    async def test_deduplicate_abstract_subsets(self):
        """Test that meshes with the Over specifier and subsets do not cause a crash"""

        stage = self._open_stage("dedupSubsets.usda")
        context = _get_context(stage, analysis=True)

        args = DEFAULT_ARGS.copy()

        success, result = self._execute_command(args, context)
        self.assertTrue(success)
        self.assertTrue(result[0])
        self.assertTrue("analysis" in result[2])
        analysis = result[2]["analysis"]

        # No results - currently we do not support deduplicating meshes
        # with geom subsets, but we should not crash.
        self.assertEqual(len(analysis), 0)

    async def test_fuzzy_dedup_transform_correctness(self):
        """Test that fuzzy deduplication produces correct transforms using OBB-based computation"""

        # This test validates the fix for fuzzy transform corruption
        # It uses a scene with 5 identical pyramids at different positions
        # All should be deduplicated with correct worldspace positions preserved

        file_name = "fuzzyDedupTransformTest.usda"
        file_path = _get_test_data_file_path(file_name)

        # Open the original stage to capture initial worldspace positions
        layer = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer)

        # Compute initial worldspace positions for all pyramids
        xformCache_before = UsdGeom.XformCache()
        initial_centroids = {}

        for i in range(1, 6):
            prim_path = f"/World/Dedup{i}/Pyramid"
            prim = stage_before.GetPrimAtPath(prim_path)
            self.assertTrue(prim.IsValid(), f"Prim {prim_path} should exist in original scene")

            mesh = UsdGeom.Mesh(prim)
            points = mesh.GetPointsAttr().Get()

            # Compute centroid in worldspace
            matrix = xformCache_before.GetLocalToWorldTransform(prim)
            centroid = Gf.Vec3d(0, 0, 0)
            for point in points:
                world_point = matrix.Transform(point)
                centroid += world_point
            centroid /= len(points)
            initial_centroids[prim_path] = centroid

        # Execute fuzzy deduplication with instanceable references
        stage_after = self._open_stage(file_name)
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["fuzzy"] = True
        args["tolerance"] = 0.005
        args["allowScaling"] = False

        success, _ = self._execute_command(args)
        self.assertTrue(success, "Fuzzy deduplication should succeed")

        # Verify that deduplication occurred (some meshes should now be instances)
        instance_count = 0
        for i in range(1, 6):
            prim_path = f"/World/Dedup{i}/Pyramid"
            prim = stage_after.GetPrimAtPath(prim_path)
            if prim.IsInstanceable() or prim.HasAuthoredReferences():
                instance_count += 1

        self.assertGreater(instance_count, 0, "At least some pyramids should be instanced")

        # CRITICAL TEST: Verify worldspace positions are preserved after deduplication
        xformCache_after = UsdGeom.XformCache()
        # For simple identical geometry like pyramids, OBB should achieve very high accuracy
        # Tolerance accounts for numerical precision (sub-centimeter level)
        tolerance = 0.005

        for i in range(1, 6):
            # The path structure changes after deduplication with INSTANCEABLEREFERENCE:
            # Original: /World/Dedup{i}/Pyramid (Mesh)
            # After: /World/Dedup{i}/Pyramid (Xform) with /World/Dedup{i}/Pyramid/Geometry (Mesh)

            original_path = f"/World/Dedup{i}/Pyramid"
            geometry_path = f"/World/Dedup{i}/Pyramid/Geometry"

            # Validate the expected path structure after deduplication
            parent_prim = stage_after.GetPrimAtPath(original_path)
            self.assertTrue(parent_prim.IsValid(), f"Parent prim should exist at {original_path}")

            # For INSTANCEABLEREFERENCE, parent should be Xform with Geometry child
            if parent_prim.IsA(UsdGeom.Xform):
                # Expected structure: Xform parent with Mesh child
                prim = stage_after.GetPrimAtPath(geometry_path)
                self.assertTrue(prim.IsValid(), f"Geometry child should exist at {geometry_path} when parent is Xform")
                self.assertTrue(
                    UsdGeom.Mesh(prim), f"Prim at {geometry_path} should be a Mesh, got {prim.GetTypeName()}"
                )
            elif parent_prim.IsA(UsdGeom.Mesh):
                # Fallback: still a Mesh (deduplication may not have restructured this one)
                prim = parent_prim
            else:
                self.fail(
                    f"Unexpected prim type at {original_path}: {parent_prim.GetTypeName()} " f"(expected Xform or Mesh)"
                )

            self.assertTrue(prim.IsValid(), f"Geometry should exist after deduplication at {i}")

            mesh = UsdGeom.Mesh(prim)
            points = mesh.GetPointsAttr().Get()

            # Compute centroid in worldspace after deduplication
            matrix = xformCache_after.GetLocalToWorldTransform(prim)
            centroid_after = Gf.Vec3d(0, 0, 0)
            for point in points:
                world_point = matrix.Transform(point)
                centroid_after += world_point
            centroid_after /= len(points)

            # Compare with initial centroid
            initial_centroid = initial_centroids[original_path]
            distance = (centroid_after - initial_centroid).GetLength()

            self.assertLess(
                distance,
                tolerance,
                f"Pyramid {i} centroid moved by {distance} (expected < {tolerance}). "
                f"Initial: {initial_centroid}, After: {centroid_after}. "
                f"This indicates transform corruption in fuzzy deduplication.",
            )

        # Additional validation: Check that the reference transforms are reasonable
        # (not identity, not degenerate)
        for i in range(1, 6):
            prim_path = f"/World/Dedup{i}/Pyramid"
            prim = stage_after.GetPrimAtPath(prim_path)

            if prim.HasAuthoredReferences():
                # Check for the DeduplicateGeometryReferenceTransform
                xformable = UsdGeom.Xformable(prim)
                xform_ops = xformable.GetOrderedXformOps()

                found_dedup_xform = False
                for xform_op in xform_ops:
                    if "DeduplicateGeometryReferenceTransform" in xform_op.GetOpName():
                        found_dedup_xform = True

                        # Get the transform matrix
                        matrix = xform_op.Get()

                        # Verify the matrix is not degenerate (determinant != 0)
                        determinant = matrix.GetDeterminant()
                        self.assertNotEqual(determinant, 0.0, f"Transform matrix for {prim_path} is degenerate (det=0)")

                        break

                # If it's an instance, it should have the transform
                if prim.IsInstanceable():
                    self.assertTrue(
                        found_dedup_xform,
                        f"Instanceable prim {prim_path} should have DeduplicateGeometryReferenceTransform",
                    )

    async def test_deduplicate_geometry_deep_transform_fuzzy_and_nonfuzzy(self):
        """Test dedup with different tessellation and a deep transform, fuzzy and non-fuzzy"""

        # Scene has three meshes:
        # - Cube: cube with two corners displaced to break mirror symmetry (quads) at origin
        # - CubeTriangulated: same shape but triangulated (12 tri faces) with a (200,200,200)
        #   translation baked into the point values (deep transform)
        # - DifferentBox: a 20x200x100 box with very different proportions that should NOT match
        #
        # The displaced corners make some quad faces non-planar, so the quad and
        # triangulated meshes define slightly different surfaces depending on
        # diagonal choice.  This is safe because the fuzzy comparator works on the
        # point-cloud OBB, not the surface tessellation, and the vertex positions
        # (which define the OBB) are identical between the two meshes.

        file_name = "fuzzyDeepTransformTest.usda"
        file_path = _get_test_data_file_path(file_name)

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_INSTANCEABLEREFERENCE
        args["considerDeepTransforms"] = True
        args["tolerance"] = 0.5
        args["allowScaling"] = False

        # Non-fuzzy cannot match these because the tessellation differs (quads vs triangles)
        # and point values include a baked-in translation. All three prims should remain meshes.
        stage = self._open_stage(file_name)
        args["fuzzy"] = False
        success, result = self._execute_command(args)
        self.assertTrue(success)

        self.assertTrue(stage.GetPrimAtPath("/World/Cube").IsA(UsdGeom.Mesh))
        self.assertTrue(stage.GetPrimAtPath("/World/CubeTriangulated").IsA(UsdGeom.Mesh))
        self.assertTrue(stage.GetPrimAtPath("/World/DifferentBox").IsA(UsdGeom.Mesh))

        # Fuzzy mode uses OBB-based shape comparison, so it finds the two cubes as duplicates
        # despite different tessellation and baked-in point offsets.
        stage = self._open_stage(file_name)
        args["fuzzy"] = True
        success, result = self._execute_command(args)
        self.assertTrue(success)
        self.assertTrue(result[0])

        cube = stage.GetPrimAtPath("/World/Cube")
        cubeTriangulated = stage.GetPrimAtPath("/World/CubeTriangulated")

        cube_is_xform = cube.IsA(UsdGeom.Xform) and not cube.IsA(UsdGeom.Mesh)
        cubeTriangulated_is_xform = cubeTriangulated.IsA(UsdGeom.Xform) and not cubeTriangulated.IsA(UsdGeom.Mesh)
        self.assertTrue(
            cube_is_xform or cubeTriangulated_is_xform,
            "At least one cube should be deduplicated to an Xform with a Geometry child",
        )

        # The non-matching mesh should remain unchanged
        self.assertTrue(stage.GetPrimAtPath("/World/DifferentBox").IsA(UsdGeom.Mesh))
        self.assertFalse(stage.GetPrimAtPath("/World/DifferentBox").IsInstance())

        # Fuzzy dedup can change tessellation, so per-vertex comparison is
        # not meaningful.  Instead verify the worldspace bounding box is preserved.
        layer = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer)
        bboxCache_before = UsdGeom.BBoxCache(Usd.TimeCode.Default(), ["default", "render"])
        bboxCache_after = UsdGeom.BBoxCache(Usd.TimeCode.Default(), ["default", "render"])

        for prim_name in ["Cube", "CubeTriangulated"]:
            path = f"/World/{prim_name}"
            prim_before = stage_before.GetPrimAtPath(path)

            prim_after = stage.GetPrimAtPath(path)
            if prim_after.IsA(UsdGeom.Xform) and not prim_after.IsA(UsdGeom.Mesh):
                prim_after = stage.GetPrimAtPath(f"{path}/Geometry")

            self.assertTrue(prim_before.IsValid())
            self.assertTrue(prim_after.IsValid())

            bbox_before = bboxCache_before.ComputeWorldBound(prim_before).ComputeAlignedRange()
            bbox_after = bboxCache_after.ComputeWorldBound(prim_after).ComputeAlignedRange()

            tol = args["tolerance"]
            for i in range(3):
                self.assertAlmostEqual(
                    bbox_before.GetMin()[i], bbox_after.GetMin()[i], delta=tol, msg=f"Bbox min[{i}] differs for {path}"
                )
                self.assertAlmostEqual(
                    bbox_before.GetMax()[i], bbox_after.GetMax()[i], delta=tol, msg=f"Bbox max[{i}] differs for {path}"
                )

    async def test_deduplicate_reference_transform_is_innermost_op(self):
        """Regression test: when the dedup transform between the two meshes is a non-translation
        (here a 180-degree rotation around X) and the prim has a non-axis-aligned translate, the
        deduplicated prim must end up at the same world-space position as the original mesh.

        The reference transform must be the LAST entry in xformOpOrder (the innermost op) so
        that prototype points are mapped into the target's local space BEFORE the prim's
        existing translate/rotate/scale stack runs. Placing it first applied it last, which
        worked for pure-translation transforms but flipped Y/Z translates here.
        """
        file_name = "deduplicateGeometryFlippedDuplicate.usda"
        file_path = _get_test_data_file_path(file_name)

        # Capture worldspace points of both meshes from a fresh stage *before* deduplicating.
        layer_before = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer_before)
        xformCache_before = UsdGeom.XformCache()
        source_points_before = _get_worldspace_points(
            stage_before.GetPrimAtPath("/World/MeshSource"), xformCache_before
        )
        flipped_points_before = _get_worldspace_points(
            stage_before.GetPrimAtPath("/World/MeshFlipped"), xformCache_before
        )

        # Sanity check: the two meshes are not coincident before dedup -- if they were, the
        # test wouldn't actually distinguish the bug.
        self.assertNotEqual(source_points_before, flipped_points_before)

        # Default args use InstanceableReference, which routes through _conformUsingComposition
        # -- the code path that places the DeduplicateGeometryReferenceTransform op.
        stage = self._open_stage(file_name)
        args = DEFAULT_ARGS.copy()
        success, result = self._execute_command(args)
        self.assertTrue(success)
        self.assertTrue(result[0])

        # One of the two meshes should be the prototype (still a Mesh after the split into
        # Xform+Geometry, accessed via the Geometry child), the other should have been
        # converted to an Xform referencing the prototype.
        source_after = stage.GetPrimAtPath("/World/MeshSource")
        flipped_after = stage.GetPrimAtPath("/World/MeshFlipped")
        self.assertTrue(source_after.IsA(UsdGeom.Xform))
        self.assertTrue(flipped_after.IsA(UsdGeom.Xform))
        self.assertTrue(flipped_after.IsInstance())

        # The fix requires the dedup reference transform to be the LAST op in xformOpOrder
        # so it is applied first to the prototype's points. Assert that directly.
        flipped_xformable = UsdGeom.Xformable(flipped_after)
        op_order = flipped_xformable.GetXformOpOrderAttr().Get()
        self.assertIsNotNone(op_order)
        self.assertGreater(len(op_order), 0)
        self.assertTrue(
            op_order[-1].endswith("DeduplicateGeometryReferenceTransform"),
            f"Expected DeduplicateGeometryReferenceTransform to be the last (innermost) op, "
            f"got xformOpOrder={list(op_order)}",
        )

        # Worldspace points must be preserved for both prims. Compare against fresh values
        # taken from the pre-dedup stage above.
        xformCache_after = UsdGeom.XformCache()
        source_points_after = _get_worldspace_points(source_after, xformCache_after)
        flipped_points_after = _get_worldspace_points(flipped_after, xformCache_after)

        self.assertEqual(len(source_points_before), len(source_points_after))
        self.assertEqual(len(flipped_points_before), len(flipped_points_after))

        # Use a tight tolerance; the dedup transform is a pure rotation so the only error
        # source is float<->double round-tripping when authoring the matrix.
        self.assertVec3ArrayAlmostEqual(
            source_points_before,
            source_points_after,
            tolerance=1e-6,
            msg="World-space points of /World/MeshSource changed across dedup",
        )
        self.assertVec3ArrayAlmostEqual(
            flipped_points_before,
            flipped_points_after,
            tolerance=1e-6,
            msg="World-space points of /World/MeshFlipped changed across dedup -- "
            "the reference transform was likely applied in the wrong order",
        )

    async def test_create_point_instancer_common_root(self):
        """Duplicates are replaced by a PointInstancer authored at the common root of the duplicate meshes."""
        file_name = "deduplicatePeerMeshes.usda"
        file_path = _get_test_data_file_path(file_name)

        # mesh_1, mesh_2, mesh_3 are duplicates in this scene; mesh_0 is unique.
        duplicate_paths_before = [
            "/World/Asset/Part/Geom/mesh_1",
            "/World/Asset/Part/Geom/mesh_2",
            "/World/Asset/Part/Geom/mesh_3",
        ]

        # Compute the expected world-space points of each duplicate before the operation runs.
        layer = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer)
        xform_cache_before = UsdGeom.XformCache()
        expected_world_points = [
            _get_worldspace_points(stage_before.GetPrimAtPath(p), xform_cache_before) for p in duplicate_paths_before
        ]

        # Run the operation in Create PointInstancer mode with the default Common Root parent.
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_COMMON_ROOT
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        # All three original duplicate prims should be removed.
        for path in duplicate_paths_before:
            self.assertFalse(
                stage_after.GetPrimAtPath(path).IsValid(),
                f"Expected {path} to be removed after PointInstancer deduplication",
            )

        # The unique mesh_0 should remain untouched.
        self.assertTrue(stage_after.GetPrimAtPath("/World/Asset/Part/Geom/mesh_0").IsValid())

        # Exactly one PointInstancer should exist, authored under the common root of the duplicates.
        instancers = [p for p in stage_after.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 1)
        instancer_prim = instancers[0]
        self.assertEqual(instancer_prim.GetPath().GetParentPath(), Sdf.Path("/World/Asset/Part/Geom"))

        # The prototype mesh must live as a direct child of the PointInstancer.
        instancer = UsdGeom.PointInstancer(instancer_prim)
        proto_targets = instancer.GetPrototypesRel().GetTargets()
        self.assertEqual(len(proto_targets), 1)
        prototype_prim = stage_after.GetPrimAtPath(proto_targets[0])
        self.assertTrue(prototype_prim.IsValid())
        self.assertTrue(prototype_prim.IsA(UsdGeom.Mesh))
        self.assertEqual(prototype_prim.GetPath().GetParentPath(), instancer_prim.GetPath())

        # protoIndices should reference index 0 for every instance and positions/scales/orientations must each have
        # one entry per original duplicate.
        proto_indices = instancer.GetProtoIndicesAttr().Get()
        self.assertEqual(len(proto_indices), len(duplicate_paths_before))
        self.assertTrue(all(i == 0 for i in proto_indices))
        self.assertEqual(len(instancer.GetPositionsAttr().Get()), len(duplicate_paths_before))
        self.assertEqual(len(instancer.GetOrientationsAttr().Get()), len(duplicate_paths_before))
        self.assertEqual(len(instancer.GetScalesAttr().Get()), len(duplicate_paths_before))

        # World-space transforms must be preserved. Compare each instance's computed local-to-world
        # against the original mesh's by transforming the prototype's points.
        instance_xforms = list(
            instancer.ComputeInstanceTransformsAtTime(Usd.TimeCode.Default(), Usd.TimeCode.Default())
        )
        self.assertEqual(len(instance_xforms), len(duplicate_paths_before))

        proto_points = UsdGeom.Mesh(prototype_prim).GetPointsAttr().Get()

        # Each instance's transformed prototype points must match exactly one of the original duplicates'
        # world-space points (in some order, since the duplicate set is not order-preserving).
        # The PointInstancer is at /World/Asset/Part/Geom which has identity worldspace transform here,
        # so ComputeInstanceTransformsAtTime returns world-space transforms directly. Tolerance is loose
        # because positions are stored as float32 in the PointInstancer; at ~1e5 magnitude the spacing
        # between representable floats is ~1e-2.
        for xform in instance_xforms:
            instance_world_points = [xform.Transform(Gf.Vec3d(p)) for p in proto_points]
            matched = any(
                len(original_world_points) == len(instance_world_points)
                and all(
                    (Gf.Vec3d(a) - Gf.Vec3d(b)).GetLength() < 0.05
                    for a, b in zip(original_world_points, instance_world_points)
                )
                for original_world_points in expected_world_points
            )
            self.assertTrue(matched, "Instance world-space points do not match any original duplicate mesh")

    async def test_create_point_instancer_minimum_duplicates(self):
        """minimumDuplicates gates PointInstancer creation on the size of the duplicate set."""
        # deduplicatePeerMeshes.usda holds one unique mesh (mesh_0) and a set of three duplicates
        # (mesh_1/2/3). A minimum larger than the set leaves everything untouched; a minimum equal to
        # the set size still produces the instancer.
        file_name = "deduplicatePeerMeshes.usda"
        duplicate_names = ("mesh_1", "mesh_2", "mesh_3")
        all_names = ("mesh_0",) + duplicate_names

        # Minimum of 4 is greater than the 3 duplicates in the set, so no PointInstancer is authored
        # and every original mesh remains in place.
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_COMMON_ROOT
        args["minimumDuplicates"] = 4
        stage = self._open_stage(file_name)
        self._execute_command(args)

        self.assertEqual(
            [p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)],
            [],
            "No PointInstancer should be created when the set is smaller than minimumDuplicates",
        )
        for name in all_names:
            self.assertTrue(
                stage.GetPrimAtPath(f"/World/Asset/Part/Geom/{name}").IsValid(),
                f"Expected {name} to be left untouched when below minimumDuplicates",
            )

        # Minimum of 3 exactly matches the set size, so the PointInstancer is created and the duplicates
        # are removed (mesh_0 stays, being unique).
        args["minimumDuplicates"] = 3
        stage = self._open_stage(file_name)
        self._execute_command(args)

        instancers = [p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 1, "A PointInstancer should be created when the set meets minimumDuplicates")
        for name in duplicate_names:
            self.assertFalse(
                stage.GetPrimAtPath(f"/World/Asset/Part/Geom/{name}").IsValid(),
                f"Expected {name} to be removed once the set meets minimumDuplicates",
            )
        self.assertTrue(stage.GetPrimAtPath("/World/Asset/Part/Geom/mesh_0").IsValid())

    async def test_create_point_instancer_custom_path(self):
        """A user-supplied parent path is honored and auto-created if missing."""
        file_name = "deduplicatePeerMeshes.usda"

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_CUSTOM_PATH
        args["pointInstancerParentPath"] = "/World/Instancers"

        stage = self._open_stage(file_name)
        self.assertFalse(stage.GetPrimAtPath("/World/Instancers").IsValid())

        self._execute_command(args)

        # The custom parent should have been auto-created as an Xform.
        parent_prim = stage.GetPrimAtPath("/World/Instancers")
        self.assertTrue(parent_prim.IsValid())
        self.assertEqual(parent_prim.GetTypeName(), "Xform")

        # The PointInstancer should live directly under the requested parent.
        instancers = [p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 1)
        self.assertEqual(instancers[0].GetPath().GetParentPath(), Sdf.Path("/World/Instancers"))

    async def test_create_point_instancer_empty_custom_path_fails(self):
        """Custom-path mode with an empty parent path should fail fast before touching the stage."""
        file_name = "deduplicatePeerMeshes.usda"

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_CUSTOM_PATH
        args["pointInstancerParentPath"] = ""

        stage = self._open_stage(file_name)
        success, result = self._execute_command(args)

        self.assertFalse(success)

        # The stage must be untouched -- no PointInstancer authored and no original meshes removed.
        self.assertEqual([p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)], [])
        for name in ("mesh_0", "mesh_1", "mesh_2", "mesh_3"):
            self.assertTrue(stage.GetPrimAtPath(f"/World/Asset/Part/Geom/{name}").IsValid())

    async def test_create_point_instancer_invalid_custom_path_fails(self):
        """Custom-path mode with a non-absolute or syntactically-invalid path should fail fast."""
        file_name = "deduplicatePeerMeshes.usda"

        # Relative paths are syntactically valid SdfPath strings but not absolute prim paths, so the
        # IsAbsolutePath check rejects them. A path string with illegal characters fails the
        # IsValidPathString check. Both should produce a failed operation without modifying the stage.
        for bad_path in ("relative/path", "/bad name with spaces"):
            with self.subTest(parent_path=bad_path):
                args = DEFAULT_ARGS.copy()
                args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
                args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_CUSTOM_PATH
                args["pointInstancerParentPath"] = bad_path

                stage = self._open_stage(file_name)
                success, result = self._execute_command(args)

                self.assertFalse(success, f"Expected failure for parent path '{bad_path}'")

                self.assertEqual([p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)], [])
                for name in ("mesh_0", "mesh_1", "mesh_2", "mesh_3"):
                    self.assertTrue(stage.GetPrimAtPath(f"/World/Asset/Part/Geom/{name}").IsValid())

    async def test_create_point_instancer_pivot_and_deep_transform(self):
        """Worldspace points are preserved for duplicates that differ by pivots and by a deep transform."""
        # deduplicateGeometryPivot.usda contains 5 cubes that are all duplicates up to deep transforms / pivots.
        # The cubes differ in: (a) the actual point values written in `points` (3 cubes at -250..-200, 2 at -200..-150),
        # (b) translate offsets, and (c) per-mesh and per-parent pivot xform pairs. The PointInstancer mode must
        # account for all three when authoring per-instance positions/orientations/scales -- otherwise the prototype
        # ends up planted in the wrong worldspace location.
        file_name = "deduplicateGeometryPivot.usda"
        file_path = _get_test_data_file_path(file_name)

        cube_paths = [
            "/World/Cube",
            "/World/CubeDuplicate",
            "/World/CubeDuplicateCustomPivot",
            "/World/CubeDuplicateAlternateTopology",
            "/World/CubeDuplicateAlternateTopologyCustomPivot",
        ]

        layer_before = Sdf.Layer.OpenAsAnonymous(file_path)
        stage_before = Usd.Stage.Open(layer_before)
        xform_cache_before = UsdGeom.XformCache()
        expected_world_points = [
            _get_worldspace_points(stage_before.GetPrimAtPath(p), xform_cache_before) for p in cube_paths
        ]

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_COMMON_ROOT
        args["tolerance"] = 0.05
        stage_after = self._open_stage(file_name)
        self._execute_command(args)

        for path in cube_paths:
            self.assertFalse(
                stage_after.GetPrimAtPath(path).IsValid(),
                f"Expected {path} to be removed after PointInstancer deduplication",
            )

        instancers = [p for p in stage_after.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 1)
        instancer = UsdGeom.PointInstancer(instancers[0])

        proto_targets = instancer.GetPrototypesRel().GetTargets()
        self.assertEqual(len(proto_targets), 1)
        prototype_prim = stage_after.GetPrimAtPath(proto_targets[0])
        proto_points = UsdGeom.Mesh(prototype_prim).GetPointsAttr().Get()

        instance_xforms = list(
            instancer.ComputeInstanceTransformsAtTime(Usd.TimeCode.Default(), Usd.TimeCode.Default())
        )
        self.assertEqual(len(instance_xforms), len(cube_paths))

        # Each instance, when applied to the prototype's points, must match one of the original cubes' worldspace
        # points. Tolerance is loose enough to absorb float32 storage in the PointInstancer attributes.
        unmatched_expected = list(expected_world_points)
        for xform in instance_xforms:
            instance_world_points = [xform.Transform(Gf.Vec3d(p)) for p in proto_points]
            match_index = None
            for i, original_world_points in enumerate(unmatched_expected):
                if len(original_world_points) != len(instance_world_points):
                    continue
                if all(
                    (Gf.Vec3d(a) - Gf.Vec3d(b)).GetLength() < 0.05
                    for a, b in zip(original_world_points, instance_world_points)
                ):
                    match_index = i
                    break
            self.assertIsNotNone(
                match_index,
                f"Instance worldspace points do not match any remaining original cube; first inst point="
                f"{instance_world_points[0]}",
            )
            # Remove the matched entry so that one cube can't satisfy two instances.
            unmatched_expected.pop(match_index)

    async def test_create_point_instancer_splits_by_material(self):
        """Duplicates with different bound materials become separate PointInstancers so no material is lost."""
        # The fixture holds four identical cubes: cube_red_a/b bound to /World/Looks/Red and cube_blue_a/b bound to
        # /World/Looks/Blue. Splitting the duplicate set by bound material must yield one PointInstancer per material.
        stage = self._open_stage("deduplicatePointInstancerMaterials.usda")

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_COMMON_ROOT
        self._execute_command(args)

        # Two PointInstancers, one per distinct bound material; each has a single prototype with two instances.
        instancers = [UsdGeom.PointInstancer(p) for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 2)

        instances_per_material = {}
        for instancer in instancers:
            proto_targets = instancer.GetPrototypesRel().GetTargets()
            self.assertEqual(len(proto_targets), 1, "Each material-homogeneous set should yield a single prototype")

            proto_prim = stage.GetPrimAtPath(proto_targets[0])
            material, _ = UsdShade.MaterialBindingAPI(proto_prim).ComputeBoundMaterial()
            self.assertTrue(material, f"Prototype {proto_targets[0]} lost its material binding")

            proto_indices = list(instancer.GetProtoIndicesAttr().Get())
            self.assertTrue(all(i == 0 for i in proto_indices))
            instances_per_material[material.GetPath()] = len(proto_indices)

        self.assertEqual(
            instances_per_material,
            {Sdf.Path("/World/Looks/Red"): 2, Sdf.Path("/World/Looks/Blue"): 2},
        )

    async def test_create_point_instancer_rebinds_inherited_material(self):
        """A material inherited from an ancestor is re-authored on the prototype when it is relocated."""
        # The cubes inherit /World/Looks/Wood from /World/Group rather than binding it directly.
        stage = self._open_stage("deduplicatePointInstancerInheritedMaterial.usda")

        cube_a = stage.GetPrimAtPath("/World/Group/cube_a")
        self.assertFalse(cube_a.HasRelationship("material:binding"), "Fixture cube should not bind a material directly")
        material_before, _ = UsdShade.MaterialBindingAPI(cube_a).ComputeBoundMaterial()
        self.assertEqual(material_before.GetPath(), Sdf.Path("/World/Looks/Wood"), "Fixture cube should inherit Wood")

        # Author the PointInstancer under a separate branch so the prototype no longer sits beneath /World/Group and
        # therefore cannot keep inheriting Wood through namespace -- the binding has to be re-authored on it.
        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_CUSTOM_PATH
        args["pointInstancerParentPath"] = "/World/Instancers"
        self._execute_command(args)

        instancers = [p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 1)
        instancer = UsdGeom.PointInstancer(instancers[0])
        self.assertEqual(instancer.GetPath().GetParentPath(), Sdf.Path("/World/Instancers"))

        proto_targets = instancer.GetPrototypesRel().GetTargets()
        self.assertEqual(len(proto_targets), 1)
        proto_prim = stage.GetPrimAtPath(proto_targets[0])

        # The prototype lives under /World/Instancers (not /World/Group), so it must carry the binding directly.
        self.assertTrue(
            proto_prim.HasRelationship("material:binding"),
            "Inherited binding was not re-authored on the relocated prototype",
        )
        material, _ = UsdShade.MaterialBindingAPI(proto_prim).ComputeBoundMaterial()
        self.assertEqual(material.GetPath(), Sdf.Path("/World/Looks/Wood"))

    async def test_create_point_instancer_keeps_non_representable_instance(self):
        """A duplicate needing shear / rotated non-uniform scale is left as a mesh; the rest are instanced."""
        # cube_a / cube_b are translation-only (representable); cube_c is rotate-then-scale (not representable).
        stage = self._open_stage("deduplicatePointInstancerShear.usda")

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_COMMON_ROOT
        self._execute_command(args)

        # One PointInstancer with exactly the two representable instances.
        instancers = [p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 1)
        instancer = UsdGeom.PointInstancer(instancers[0])
        self.assertEqual(len(instancer.GetProtoIndicesAttr().Get()), 2)

        # cube_a and cube_b were instanced and removed; cube_c is left untouched as a mesh.
        self.assertFalse(stage.GetPrimAtPath("/World/Geom/cube_a").IsValid())
        self.assertFalse(stage.GetPrimAtPath("/World/Geom/cube_b").IsValid())
        cube_c = stage.GetPrimAtPath("/World/Geom/cube_c")
        self.assertTrue(cube_c.IsValid())
        self.assertTrue(cube_c.IsA(UsdGeom.Mesh))

    async def test_create_point_instancer_skips_set_with_too_few_representable(self):
        """If fewer than two duplicates are representable, the whole set is left untouched."""
        # Restrict the set to one representable (cube_a) and one non-representable (cube_c) duplicate.
        stage = self._open_stage("deduplicatePointInstancerShear.usda")

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["meshPrimPaths"] = ["/World/Geom/cube_a", "/World/Geom/cube_c"]
        self._execute_command(args)

        # No PointInstancer is authored and both meshes remain.
        self.assertEqual([p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)], [])
        self.assertTrue(stage.GetPrimAtPath("/World/Geom/cube_a").IsValid())
        self.assertTrue(stage.GetPrimAtPath("/World/Geom/cube_c").IsValid())

    async def test_create_point_instancer_prototype_source_is_deleted(self):
        """Regression: the prototype must be a representable duplicate that is itself deleted, never a leftover.

        The prototype source was previously always the last prim in the set. When that prim's own placement was not
        representable (cube_c's shear), it was still copied under the PointInstancer but excluded from the instanced
        (deleted) prims -- leaving its mesh data twice, once as the standalone original and once as the prototype
        copy. The prototype must instead come from a representable duplicate whose original is removed.
        """
        stage = self._open_stage("deduplicatePointInstancerShear.usda")

        args = DEFAULT_ARGS.copy()
        args["duplicateMethod"] = DUPLICATE_METHOD_POINT_INSTANCER
        args["pointInstancerLocation"] = POINT_INSTANCER_LOCATION_COMMON_ROOT
        self._execute_command(args)

        instancers = [p for p in stage.Traverse() if p.IsA(UsdGeom.PointInstancer)]
        self.assertEqual(len(instancers), 1)
        instancer = UsdGeom.PointInstancer(instancers[0])

        # The prototype copied under the PointInstancer must come from a representable cube (cube_a/cube_b), not the
        # sheared cube_c, and its original must have been deleted so its mesh data is not duplicated.
        proto_targets = instancer.GetPrototypesRel().GetTargets()
        self.assertEqual(len(proto_targets), 1)
        prototype_prim = stage.GetPrimAtPath(proto_targets[0])
        self.assertTrue(prototype_prim.IsValid())
        prototype_name = prototype_prim.GetName()
        self.assertIn(prototype_name, ("cube_a", "cube_b"))
        self.assertFalse(
            stage.GetPrimAtPath(f"/World/Geom/{prototype_name}").IsValid(),
            "The prototype's source prim must be deleted, not left beside its copy",
        )

        # cube_c could not be represented, so it stays exactly once as a standalone mesh, and it is the only original
        # mesh remaining directly under Geom (the representable cubes were instanced and removed).
        cube_c = stage.GetPrimAtPath("/World/Geom/cube_c")
        self.assertTrue(cube_c.IsValid())
        self.assertTrue(cube_c.IsA(UsdGeom.Mesh))
        remaining_meshes = [
            p.GetName() for p in stage.GetPrimAtPath("/World/Geom").GetChildren() if p.IsA(UsdGeom.Mesh)
        ]
        self.assertEqual(remaining_meshes, ["cube_c"])
