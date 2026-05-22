# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#


from pxr import Gf, Sdf, Usd, UsdGeom, UsdLux

from .test_utils import Test_Operation, _get_test_data_file_path

EMPTY_TEST_FILE = "empty.usda"
SIMPLE_GEO_TEST_FILE = "editStageMetrics_torus.usda"
XFORM_GEO_TEST_FILE = "editStageMetrics_xformGeo.usda"
XFORM_TIME_SAMPLES_TEST_FILE = "editStageMetrics_xformTimeSamples.usda"
SINGLE_AXIS_ROTATES_GEO_TEST_FILE = "editStageMetrics_singleAxisRotatesGeo.usda"
CURVES_TEST_FILE = "editStageMetrics_basisCurves.usda"
SKEL_TEST_FILE = "pixarUsdSkelExample.usdc"
INSTANCES_TEST_FILE = "editStageMetrics_scaleInstances.usda"
POINT_INSTANCER_TEST_FILE = "editStageMetrics_pointInstancer.usda"
VARIANTS_TEST_FILE = "editStageMetrics_variants.usda"
UP_AXIS_CORRECTION_TEST_FILE = "editStageMetrics_upAxisCorrection.usda"
PHYSICS_TEST_FILE = "editStageMetrics_physics.usda"
KIT_CAMERAS_TEST_FILE = "editStageMetrics_kitCameras.usda"

CONFIG_SCALE_TO_METERS = "scaleToMeters.json"
CONFIG_SCALE_TO_CENTIMETERS = "scaleToCenitmeters.json"
CONFIG_SCALE_TO_MILLIMETERS = "scaleToMillimeters.json"
CONFIG_UP_AXIS_Y = "upAxisY.json"
CONFIG_UP_AXIS_Z = "upAxisZ.json"
CONFIG_UP_AXIS_Z_COLLAPSE_XFORMS = "upAxisZCollapseXforms.json"
CONFIG_SCALE_TO_METERS_AND_UP_AXIS_Y = "scaleToMetersAndUpAxisY.json"
CONFIG_SCALE_TO_METERS_AND_UP_AXIS_Z = "scaleToMetersAndUpAxisZ.json"
CONFIG_SCALE_TO_CENTIMETERS_AND_UP_AXIS_Y = "scaleToCentimetersAndUpAxisY.json"
CONFIG_IGNORE_KIT_CAMERAS_ON = "editStageMetrics_ignoreKitCamerasOn.json"
CONFIG_IGNORE_KIT_CAMERAS_OFF = "editStageMetrics_ignoreKitCamerasOff.json"


# helper class for storing information about an expected up-axis rotation
class RotateData(object):
    def __init__(self, change_of_basis, rotation):
        self.change_of_basis = change_of_basis
        self.change_of_basis_inv = change_of_basis.GetInverse()
        self.rotation = rotation


# constants for different axis rotations
NO_ROTATION = RotateData(
    Gf.Matrix4d(1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1), Gf.Rotation(Gf.Vec3d(1.0, 0.0, 0.0), 0)
)
Y_TO_Z_ROTATION = RotateData(
    Gf.Matrix4d(1, 0, 0, 0, 0, 0, -1, 0, 0, 1, 0, 0, 0, 0, 0, 1), Gf.Rotation(Gf.Vec3d(1.0, 0.0, 0.0), 90)
)
Z_TO_Y_ROTATION = RotateData(
    Gf.Matrix4d(1, 0, 0, 0, 0, 0, 1, 0, 0, -1, 0, 0, 0, 0, 0, 1), Gf.Rotation(Gf.Vec3d(1.0, 0.0, 0.0), -90)
)


def _get_worldspace_points(prim, xformCache):
    """Returns points in worldspace"""
    # Get the points and local to world transform matrix.
    points = UsdGeom.PointBased(prim).GetPointsAttr().Get()
    matrix = xformCache.GetLocalToWorldTransform(prim)

    # Return the points with the matrix applied.
    return [matrix.Transform(x) for x in points]


class Test_Operation_EditStageMetrics(Test_Operation):

    def _open_stages(self, file_path):
        layer = Sdf.Layer.OpenAsAnonymous(_get_test_data_file_path(file_path))
        stage_old = Usd.Stage.Open(layer)
        stage_new = self._open_stage(file_path)
        return stage_old, stage_new

    def _assert_float_scaled(self, a, b, scale):
        self.assertAlmostEqual(a * scale, b)

    def _assert_float_not_scaled(self, a, b):
        self.assertAlmostEqual(a, b)

    def _assert_float_array_scaled(self, a, b, scale):
        if len(a) != len(b):
            self.fail(f"vectors have different lengths: {a} -- {b}")
        for c, d in zip(a, b):
            self.assertAlmostEqual(c * scale, d)

    def _assert_vec2_scaled(self, a, b, scale):
        for i in range(2):
            self.assertAlmostEqual(a[i] * scale, b[i])

    def _assert_vec3_array_scaled(self, a, b, scale):
        if len(a) != len(b):
            self.fail(f"vectors have different lengths: {a} -- {b}")
        for c, d in zip(a, b):
            for i in range(3):
                self.assertAlmostEqual(c[i] * scale, d[i])

    def _assert_world_space_points_are_scaled(self, old_stage, new_stage, scale, timesample=None):
        xformCache = UsdGeom.XformCache()
        if timesample is not None:
            xformCache.SetTime(timesample)
        for old_prim in old_stage.TraverseAll():
            if not UsdGeom.PointBased(old_prim):
                continue
            new_prim = new_stage.GetPrimAtPath(old_prim.GetPath())
            points_old = _get_worldspace_points(old_prim, xformCache)
            points_new = _get_worldspace_points(new_prim, xformCache)
            for a, b in zip(points_old, points_new):
                a *= scale
                for i in range(3):
                    self.assertAlmostEqual(a[i], b[i], places=4)

    def _assert_vectors_equal(self, a, b):
        for i in range(3):
            self.assertAlmostEqual(a[i], b[i])

    def _assert_vec3_array_rotated(self, a, b, rotation):
        if len(a) != len(b):
            self.fail(f"vectors have different lengths: {a} -- {b}")
        for c, d in zip(a, b):
            c = rotation.rotation.TransformDir(c)
            for i in range(3):
                self.assertAlmostEqual(c[i], d[i], places=4)

    def _assert_quat_array_rotated(self, a, b, rotation):
        for c, d in zip(a, b):
            c = rotation.change_of_basis * Gf.Matrix4d(Gf.Rotation(c), Gf.Vec3d(0, 0, 0)) * rotation.change_of_basis_inv
            c = c.ExtractRotationQuat()
            self.assertAlmostEqual(c.GetReal(), d.GetReal(), places=3)
            for i in range(3):
                self.assertAlmostEqual(c.GetImaginary()[i], d.GetImaginary()[i], places=3)

    def _assert_matrix4_basis_changed(self, a, b, rotation):
        a = rotation.change_of_basis * a * rotation.change_of_basis_inv
        for i in range(4):
            for j in range(4):
                self.assertAlmostEqual(a[i][j], b[i][j])

    def _assert_matrix4_basis_changed_and_rotated(self, a, b, rotation):
        a = rotation.change_of_basis * a * rotation.change_of_basis_inv
        a = Gf.Matrix4d(rotation.rotation, Gf.Vec3d(0, 0, 0)) * a
        for i in range(4):
            for j in range(4):
                self.assertAlmostEqual(a[i][j], b[i][j])

    def _assert_world_space_points_are_rotated(self, old_stage, new_stage, rotation: RotateData, timesample=None):
        xformCache = UsdGeom.XformCache()
        if timesample is not None:
            xformCache.SetTime(timesample)
        for old_prim in old_stage.TraverseAll():
            if not UsdGeom.PointBased(old_prim):
                continue
            new_prim = new_stage.GetPrimAtPath(old_prim.GetPath())
            points_old = _get_worldspace_points(old_prim, xformCache)
            points_new = _get_worldspace_points(new_prim, xformCache)
            for a, b in zip(points_old, points_new):
                a = rotation.rotation.TransformDir(a)
                for i in range(3):
                    self.assertAlmostEqual(a[i], b[i], places=4)

    def _assert_world_space_points_are_scaled_and_rotated(self, old_stage, new_stage, scale, rotation: RotateData):
        xformCache = UsdGeom.XformCache()
        for old_prim in old_stage.TraverseAll():
            if not UsdGeom.PointBased(old_prim):
                continue
            new_prim = new_stage.GetPrimAtPath(old_prim.GetPath())
            points_old = _get_worldspace_points(old_prim, xformCache)
            points_new = _get_worldspace_points(new_prim, xformCache)
            for a, b in zip(points_old, points_new):
                a *= scale
                a = rotation.rotation.TransformDir(a)
                for i in range(3):
                    self.assertAlmostEqual(a[i], b[i], places=4)

    async def test_scaleNone(self):
        """
        Tests that converting a stage in cms to cms does not change the world space points.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_CENTIMETERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 1.0)

    async def test_scaleUpGeo(self):
        """
        Tests changing a stage's meters per unit from cm to meters causes the world space points to be scaled 10x
        smaller.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01)

    async def test_scaleDownGeo(self):
        """
        Tests changing a stage's meters per unit from cm to mm causes the world space points to be scaled 10x larger.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_MILLIMETERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 10.0)

    async def test_scaleGeoWithTranslate(self):
        """
        Tests that changing the meters per unit of a stage containing a geometry prim with a translate xformOp causes
        the world space points to be scaled.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01)

    async def test_scaleGeoWithTRS(self):
        """
        Tests that changing the meters per unit of a stage containing a geometry prim with translate, rotateXYZ, and
        scale xformOps causes the world space points to be scaled.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01)

    async def test_scaleGeoWithTRSAndPivot(self):
        """
        Tests that changing the meters per unit of a stage containing a geometry prim with translate, pivot, rotateXYZ,
        and scale xformOps causes the world space points to be scaled.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddTranslateOp(opSuffix="pivot").Set(value=(-10, 20, 30))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01)

    async def test_scaleGeoWithMatrix(self):
        """
        Tests that changing the meters per unit of a stage containing a geometry prim with a matrix xformOp causes the
        world space points to be scaled.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply matrix xform to both stages
        matrix = Gf.Matrix4d(
            (0.0632, 0.0661, -0.0411, 0),
            (-0.2075, 0.2215, 0.0374, 0),
            (0.0349, 0.0185, 0.0835, 0),
            (-9.4454, 15.8978, -8.9597, 1),
        )
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTransformOp().Set(value=matrix)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01)

    async def test_scaleGeoWithShearMatrix(self):
        """
        Tests that changing the meters per unit of a stage containing a geometry prim with a matrix with shear xformOp
        causes the world space points to be scaled.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply matrix xform to both stages
        matrix = Gf.Matrix4d(
            #                vvvv - this is the shear component
            (0.0632, 0.0661, -1.5, 0),
            (-0.2075, 0.2215, 0.0374, 0),
            (0.0349, 0.0185, 0.0835, 0),
            (-9.4454, 15.8978, -8.9597, 1),
        )
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTransformOp().Set(value=matrix)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01)

    async def test_scaleXformTimeSamples(self):
        """
        Tests that changing the meters per unit of a stage with timesampled xforms causes world space points to be
        scaled for all time samples.
        """
        stage_old, stage_new = self._open_stages(XFORM_TIME_SAMPLES_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01, timesample=1)
        self._assert_world_space_points_are_scaled(stage_old, stage_new, 0.01, timesample=2)

    async def test_scaleCurves(self):
        """
        Tests that changing the meters per unit of a stage containing a curves prim causes world space attributes to be
        scaled.
        """
        stage_old, stage_new = self._open_stages(CURVES_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        scale = 0.01
        self._assert_world_space_points_are_scaled(stage_old, stage_new, scale)
        # check widths
        curves_path = "/World/BasisCurves"
        curves_old = UsdGeom.BasisCurves(stage_old.GetPrimAtPath(curves_path))
        curves_new = UsdGeom.BasisCurves(stage_new.GetPrimAtPath(curves_path))
        self._assert_float_array_scaled(curves_old.GetWidthsAttr().Get(), curves_new.GetWidthsAttr().Get(), scale)

    async def test_scaleSchemaPrim(self):
        """
        Tests that changing the meters per unit of a stage various schema prims causes all relevant world space
        attributes to be scaled.
        """
        stage_old, stage_new = self._open_stages(EMPTY_TEST_FILE)
        # create a camera in each stage
        camera_path = "/World/Camera"
        camera_old = UsdGeom.Camera.Define(stage_old, camera_path)
        camera_old.CreateFocalLengthAttr(50.0)
        camera_new = UsdGeom.Camera.Define(stage_new, camera_path)
        camera_new.CreateFocalLengthAttr(50.0)
        # create a cylinder light in each stage
        cylinder_light_path = "/World/CylinderLight"
        cylinder_light_old = UsdLux.CylinderLight.Define(stage_old, cylinder_light_path)
        cylinder_light_old.CreateRadiusAttr(10.0)
        cylinder_light_new = UsdLux.CylinderLight.Define(stage_new, cylinder_light_path)
        cylinder_light_new.CreateRadiusAttr(10.0)
        # create a disk light in each stage
        disk_light_path = "/World/DiskLight"
        disk_light_old = UsdLux.DiskLight.Define(stage_old, disk_light_path)
        disk_light_new = UsdLux.DiskLight.Define(stage_new, disk_light_path)
        # create a dome light in each stage
        dome_light_path = "/World/DomeLight"
        dome_light_old = UsdLux.DomeLight.Define(stage_old, dome_light_path)
        dome_light_new = UsdLux.DomeLight.Define(stage_new, dome_light_path)
        # create a rect light in each stage
        rect_light_path = "/World/RectLight"
        rect_light_old = UsdLux.RectLight.Define(stage_old, rect_light_path)
        rect_light_old.CreateWidthAttr(25.0)
        rect_light_new = UsdLux.RectLight.Define(stage_new, rect_light_path)
        rect_light_new.CreateWidthAttr(25.0)
        # create a sphere light in each stage
        sphere_light_path = "/World/SphereLight"
        sphere_light_old = UsdLux.SphereLight.Define(stage_old, sphere_light_path)
        sphere_light_new = UsdLux.SphereLight.Define(stage_new, sphere_light_path)
        # create a capsule in each stage
        capsule_path = "/World/Capsule"
        capsule_old = UsdGeom.Capsule.Define(stage_old, capsule_path)
        capsule_old.CreateHeightAttr(5.5)
        capsule_new = UsdGeom.Capsule.Define(stage_new, capsule_path)
        capsule_new.CreateHeightAttr(5.5)
        # create a cone in each stage
        cone_path = "/World/Cone"
        cone_old = UsdGeom.Cone.Define(stage_old, cone_path)
        cone_old.CreateHeightAttr(10.0)
        cone_new = UsdGeom.Cone.Define(stage_new, cone_path)
        cone_new.CreateHeightAttr(10.0)
        # create a cube in each stage
        cube_path = "/World/Cube"
        extent = [(-5, -15, -25), (10, 20, 30)]
        cube_old = UsdGeom.Cube.Define(stage_old, cube_path)
        cube_old.CreateExtentAttr().Set(extent)
        cube_new = UsdGeom.Cube.Define(stage_new, cube_path)
        cube_new.CreateExtentAttr().Set(extent)
        # create a cylinder in each stage
        cylinder_path = "/World/Cylinder"
        cylinder_old = UsdGeom.Cylinder.Define(stage_old, cylinder_path)
        cylinder_old.CreateRadiusAttr(33.3)
        cylinder_new = UsdGeom.Cylinder.Define(stage_new, cylinder_path)
        cylinder_new.CreateRadiusAttr(33.3)
        # create a plane in each stage
        plane_path = "/World/Plane"
        plane_old = UsdGeom.Plane.Define(stage_old, plane_path)
        plane_old.CreateLengthAttr(40.0)
        plane_new = UsdGeom.Plane.Define(stage_new, plane_path)
        plane_new.CreateLengthAttr(40.0)
        # create a sphere in each stage
        sphere_path = "/World/Sphere"
        sphere_old = UsdGeom.Sphere.Define(stage_old, sphere_path)
        sphere_new = UsdGeom.Sphere.Define(stage_new, sphere_path)
        # perform scaling
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        scale = 0.01
        # check camera
        self._assert_vec2_scaled(
            camera_old.GetClippingRangeAttr().Get(), camera_new.GetClippingRangeAttr().Get(), scale
        )
        self._assert_float_not_scaled(
            camera_old.GetHorizontalApertureAttr().Get(), camera_new.GetHorizontalApertureAttr().Get()
        )
        self._assert_float_not_scaled(
            camera_old.GetHorizontalApertureOffsetAttr().Get(), camera_new.GetHorizontalApertureOffsetAttr().Get()
        )
        self._assert_float_not_scaled(
            camera_old.GetVerticalApertureAttr().Get(), camera_new.GetVerticalApertureAttr().Get()
        )
        self._assert_float_not_scaled(
            camera_old.GetVerticalApertureOffsetAttr().Get(), camera_new.GetVerticalApertureOffsetAttr().Get()
        )
        self._assert_float_not_scaled(camera_old.GetFocalLengthAttr().Get(), camera_new.GetFocalLengthAttr().Get())
        self._assert_float_not_scaled(camera_old.GetFocusDistanceAttr().Get(), camera_new.GetFocusDistanceAttr().Get())
        self._assert_float_not_scaled(camera_old.GetFStopAttr().Get(), camera_new.GetFStopAttr().Get())
        self._assert_float_not_scaled(camera_old.GetShutterOpenAttr().Get(), camera_new.GetShutterOpenAttr().Get())
        self._assert_float_not_scaled(camera_old.GetShutterCloseAttr().Get(), camera_new.GetShutterCloseAttr().Get())
        # check cylinder light
        self._assert_float_scaled(
            cylinder_light_old.GetLengthAttr().Get(), cylinder_light_new.GetLengthAttr().Get(), scale
        )
        self._assert_float_scaled(
            cylinder_light_old.GetRadiusAttr().Get(), cylinder_light_new.GetRadiusAttr().Get(), scale
        )
        # check disk light
        self._assert_float_scaled(disk_light_old.GetRadiusAttr().Get(), disk_light_new.GetRadiusAttr().Get(), scale)
        # check dome light
        self._assert_float_scaled(
            dome_light_old.GetGuideRadiusAttr().Get(), dome_light_new.GetGuideRadiusAttr().Get(), scale
        )
        # check rect light
        self._assert_float_scaled(rect_light_old.GetWidthAttr().Get(), rect_light_new.GetWidthAttr().Get(), scale)
        self._assert_float_scaled(rect_light_old.GetHeightAttr().Get(), rect_light_new.GetHeightAttr().Get(), scale)
        # check sphere light
        self._assert_float_scaled(sphere_light_old.GetRadiusAttr().Get(), sphere_light_new.GetRadiusAttr().Get(), scale)
        # check capsule
        self._assert_float_scaled(capsule_old.GetHeightAttr().Get(), capsule_new.GetHeightAttr().Get(), scale)
        self._assert_float_scaled(capsule_old.GetRadiusAttr().Get(), capsule_new.GetRadiusAttr().Get(), scale)
        self._assert_vec3_array_scaled(capsule_old.GetExtentAttr().Get(), capsule_new.GetExtentAttr().Get(), scale)
        # check cone
        self._assert_float_scaled(cone_old.GetHeightAttr().Get(), cone_new.GetHeightAttr().Get(), scale)
        self._assert_float_scaled(cone_old.GetRadiusAttr().Get(), cone_new.GetRadiusAttr().Get(), scale)
        # check cube
        self._assert_float_scaled(cube_old.GetSizeAttr().Get(), cube_new.GetSizeAttr().Get(), scale)
        self._assert_vec3_array_scaled(cube_old.GetExtentAttr().Get(), cube_new.GetExtentAttr().Get(), scale)
        # check cylinder
        self._assert_float_scaled(cylinder_old.GetHeightAttr().Get(), cylinder_new.GetHeightAttr().Get(), scale)
        self._assert_float_scaled(cylinder_old.GetRadiusAttr().Get(), cylinder_new.GetRadiusAttr().Get(), scale)
        # check plane
        self._assert_float_scaled(plane_old.GetWidthAttr().Get(), plane_new.GetWidthAttr().Get(), scale)
        self._assert_float_scaled(plane_old.GetLengthAttr().Get(), plane_new.GetLengthAttr().Get(), scale)
        # check sphere
        self._assert_float_scaled(sphere_old.GetRadiusAttr().Get(), sphere_new.GetRadiusAttr().Get(), scale)

    async def test_scaleInstances(self):
        """
        Tests that changing the meters per unit of a stage containing a instances causes world space attributes to be
        scaled including the prototype prim and each of the actual instances of the prim.
        """
        stage_old, stage_new = self._open_stages(INSTANCES_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        scale = 0.01
        # this only checks the prototype
        self._assert_world_space_points_are_scaled(stage_old, stage_new, scale)
        xformCache = UsdGeom.XformCache()
        # get the prototype prims and their transforms
        prototype_mesh_path = "/Flattened_Prototype_1/grid1/mesh_0"
        mesh_old = stage_old.GetPrimAtPath(prototype_mesh_path)
        mesh_matrix_old = xformCache.GetLocalToWorldTransform(mesh_old)
        mesh_new = stage_new.GetPrimAtPath(prototype_mesh_path)
        mesh_matrix_new = xformCache.GetLocalToWorldTransform(mesh_new)
        # discover the instances that use the prototype
        for prim_old in stage_old.Traverse():
            if not prim_old.IsInstance():
                continue
            prim_new = stage_new.GetPrimAtPath(prim_old.GetPath())
            # compute the full transforms
            instance_matrix_old = mesh_matrix_old * xformCache.GetLocalToWorldTransform(prim_old)
            instance_matrix_new = mesh_matrix_new * xformCache.GetLocalToWorldTransform(prim_new)
            # resolve points
            points_old = [instance_matrix_old.Transform(x) for x in UsdGeom.PointBased(mesh_old).GetPointsAttr().Get()]
            points_new = [instance_matrix_new.Transform(x) for x in UsdGeom.PointBased(mesh_new).GetPointsAttr().Get()]
            # check points are scaled
            for old_p, new_p in zip(points_old, points_new):
                for i in range(3):
                    self.assertTrue((old_p[i] * scale) - new_p[i] < 0.0001)

    async def test_scalePointInstancer(self):
        """
        Tests that instances of a PointInstancer are scaled when the metersPerUnit is changed.
        """
        stage_old, stage_new = self._open_stages(POINT_INSTANCER_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)
        scale = 0.01
        # this only checks the prototype
        self._assert_world_space_points_are_scaled(stage_old, stage_new, scale)
        # get the point instancer prim and check positions
        point_instancer_path = "/copytopoints1"
        point_instancer_old = UsdGeom.PointInstancer(stage_old.GetPrimAtPath(point_instancer_path))
        point_instancer_new = UsdGeom.PointInstancer(stage_new.GetPrimAtPath(point_instancer_path))
        self._assert_vec3_array_scaled(
            point_instancer_old.GetPositionsAttr().Get(), point_instancer_new.GetPositionsAttr().Get(), scale
        )

    async def test_scaleVariants(self):
        """
        Tests that changing the meters per unit of a stage containing variants causes world space attributes in all
        variants to be scaled.
        """
        stage_old, stage_new = self._open_stages(VARIANTS_TEST_FILE)
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS)

        scale = 0.01
        cube_path = "/World/Cube"
        prim_old = stage_old.GetPrimAtPath(cube_path)
        prim_new = stage_new.GetPrimAtPath(cube_path)

        # check the cube size parameters on the default variant
        cube_old = UsdGeom.Cube(prim_old)
        cube_new = UsdGeom.Cube(prim_new)

        self.assertAlmostEqual(cube_old.GetSizeAttr().Get() * scale, cube_new.GetSizeAttr().Get())
        self._assert_vec3_array_scaled(cube_old.GetExtentAttr().Get(), cube_new.GetExtentAttr().Get(), scale)

        # switch to the b variant and check the cube size parameters
        stage_old.GetPrimAtPath("/World").GetVariantSet("test").SetVariantSelection("b")
        stage_new.GetPrimAtPath("/World").GetVariantSet("test").SetVariantSelection("b")

        self.assertAlmostEqual(cube_old.GetSizeAttr().Get() * scale, cube_new.GetSizeAttr().Get())
        self._assert_vec3_array_scaled(cube_old.GetExtentAttr().Get(), cube_new.GetExtentAttr().Get(), scale)

    async def test_YtoYVector(self):
        """
        Tests that changing the up axis of a stage from Y to Y does not change the direction of a vector.
        """
        # open an empty stage and set the up axis
        stage = self._open_stage(EMPTY_TEST_FILE)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        # create a prim with a vector pointing up
        vector = Gf.Vec3f(0, 1, 0)
        vector_prim = stage.DefinePrim("/World/Vector")
        vector_prim.CreateAttribute("vector", Sdf.ValueTypeNames.Vector3d).Set(vector)
        # execute and check the vector still points along y afterwards
        self._execute_json(stage, CONFIG_UP_AXIS_Y)
        self._assert_vectors_equal(vector_prim.GetAttribute("vector").Get(), Gf.Vec3f(0, 1, 0))

    async def test_YtoZVector(self):
        """
        Tests that changing the up axis of a stage from Y to Z changes the direction of a vector.
        """
        # open an empty stage and set the up axis
        stage = self._open_stage(EMPTY_TEST_FILE)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.y)
        # create a prim with a vector pointing up
        vector = Gf.Vec3f(0, 1, 0)
        vector_prim = stage.DefinePrim("/World/Vector")
        vector_prim.CreateAttribute("vector", Sdf.ValueTypeNames.Vector3d).Set(vector)
        # execute and check the vector points along z afterwards
        self._execute_json(stage, CONFIG_UP_AXIS_Z)
        self._assert_vectors_equal(vector_prim.GetAttribute("vector").Get(), Gf.Vec3f(0, 0, 1))

    async def test_ZtoYVector(self):
        """
        Tests that changing the up axis of a stage from Y to Z changes the direction of a vector.
        """
        # open an empty stage and set the up axis
        stage = self._open_stage(EMPTY_TEST_FILE)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
        # create a prim with a vector pointing up
        vector = Gf.Vec3f(0, 0, 1)
        vector_prim = stage.DefinePrim("/World/Vector")
        vector_prim.CreateAttribute("vector", Sdf.ValueTypeNames.Vector3d).Set(vector)
        # execute and check the vector points along y afterwards
        self._execute_json(stage, CONFIG_UP_AXIS_Y)
        self._assert_vectors_equal(vector_prim.GetAttribute("vector").Get(), Gf.Vec3f(0, 1, 0))

    async def test_ZtoZVector(self):
        """
        Tests that changing the up axis of a stage from Z to Z does not change the direction of a vector.
        """
        # open an empty stage and set the up axis
        stage = self._open_stage(EMPTY_TEST_FILE)
        UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
        # create a prim with a vector pointing up
        vector = Gf.Vec3f(0, 0, 1)
        vector_prim = stage.DefinePrim("/World/Vector")
        vector_prim.CreateAttribute("vector", Sdf.ValueTypeNames.Vector3d).Set(vector)
        # execute and check the vector still points along z afterwards
        self._execute_json(stage, CONFIG_UP_AXIS_Z)
        self._assert_vectors_equal(vector_prim.GetAttribute("vector").Get(), Gf.Vec3f(0, 0, 1))

    async def test_YtoZGeo(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_ZtoYGeo(self):
        """
        Tests that changing the up axis from Z to Y causes the world space points of a geometry prim to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # change the up axis to Z
        UsdGeom.SetStageUpAxis(stage_old, UsdGeom.Tokens.z)
        UsdGeom.SetStageUpAxis(stage_new, UsdGeom.Tokens.z)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Y)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Z_TO_Y_ROTATION)

    async def test_YtoZGeoWithTranslate(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a translate
        xformOp to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithTRS(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a translate,
        rotateXYZ, and scale xformOps to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithTRSAndPivot(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a translate,
        pivot, rotateXYZ, and scale xformOps to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddTranslateOp(opSuffix="pivot").Set(value=(-10, 20, 30))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithTRSRotateXZY(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a translate,
        rotateXZY, and scale xformOps to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddRotateXZYOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithTRSRotateZYX(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a translate,
        rotateZYX, and scale xformOps to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddRotateZYXOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithTRSSingleAxis(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a translate,
        rotateY, rotateZ , and scale xformOps to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddRotateYOp().Set(value=45.0)
            xform.AddRotateZOp().Set(value=-25.0)
            xform.AddXformOp(UsdGeom.XformOp.TypeRotateY, UsdGeom.XformOp.PrecisionFloat, "testSuffix").Set(value=-13.0)
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithTRSUniformScale(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a translate,
        rotateXYZ, and uniform scale xformOps to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(2, 2, 2))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithTRSCollapseXforms(self):
        """
        Tests that changing the up axis from Y to Z and collapsing xforms causes the world space points of a geometry
        prim with a translate, rotateXYZ, and scale xformOps to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z_COLLAPSE_XFORMS)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithMatrix(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a matrix
        xformOp to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply matrix xform to both stages
        matrix = Gf.Matrix4d(
            (0.0632, 0.0661, -0.0411, 0),
            (-0.2075, 0.2215, 0.0374, 0),
            (0.0349, 0.0185, 0.0835, 0),
            (-9.4454, 15.8978, -8.9597, 1),
        )
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTransformOp().Set(value=matrix)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZGeoWithShearMatrix(self):
        """
        Tests that changing the up axis from Y to Z causes the world space points of a geometry prim with a matrix with
        shear xformOp to be rotated.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply matrix xform to both stages
        matrix = Gf.Matrix4d(
            #                vvvv - this is the shear component
            (0.0632, 0.0661, -1.5, 0),
            (-0.2075, 0.2215, 0.0374, 0),
            (0.0349, 0.0185, 0.0835, 0),
            (-9.4454, 15.8978, -8.9597, 1),
        )
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTransformOp().Set(value=matrix)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_YtoZXformTimeSamples(self):
        """
        Tests that changing up axis of a stage with timesampled xforms causes world space points to be rotated for all
        time samples.
        """
        stage_old, stage_new = self._open_stages(XFORM_TIME_SAMPLES_TEST_FILE)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION, timesample=1)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION, timesample=2)

    async def test_YtoZSimpleOver(self):
        """
        Tests that the geo from the sublayer with a non-xform over in the active layer is not rotated because its
        transform is not in active edit layer.
        """
        stage_old, stage_new = self._open_stages(EMPTY_TEST_FILE)
        # add a sublayer with xformed geo and add an over on a non worldspace attribute
        sub_layer = Sdf.Layer.OpenAsAnonymous(_get_test_data_file_path(XFORM_GEO_TEST_FILE))
        for stage in [stage_old, stage_new]:
            stage.GetRootLayer().subLayerPaths.append(sub_layer.identifier)
            xform_prim = stage.DefinePrim("/World/XformGeo")
            xform_prim.CreateAttribute("purpose", Sdf.ValueTypeNames.Token, variability=Sdf.VariabilityUniform).Set(
                "render"
            )
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, NO_ROTATION)

    async def test_YtoZCurves(self):
        """
        Tests that changing the up axis of a stage containing a curves prim causes world space attributes to be rotated.
        """
        stage_old, stage_new = self._open_stages(CURVES_TEST_FILE)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)

    async def test_ZtoYSkeleton(self):
        """
        Tests that changing the up axis of a stage containing an UsdSkelAnimation prim causes world space attributes to
        be rotated.
        """
        stage_old, stage_new = self._open_stages(SKEL_TEST_FILE)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Y)
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Z_TO_Y_ROTATION)
        skel_path = "/HumanFemale_Group/SkelAnim"
        skel_prim_old = stage_old.GetPrimAtPath(skel_path)
        skel_prim_new = stage_new.GetPrimAtPath(skel_path)
        # test rotations are rotated
        old_attr = skel_prim_old.GetAttribute("rotations")
        new_attr = skel_prim_new.GetAttribute("rotations")
        for time_sample in old_attr.GetTimeSamples():
            self._assert_quat_array_rotated(old_attr.Get(time_sample), new_attr.Get(time_sample), Z_TO_Y_ROTATION)
        # test translations are rotated
        old_attr = skel_prim_old.GetAttribute("translations")
        new_attr = skel_prim_new.GetAttribute("translations")
        for time_sample in old_attr.GetTimeSamples():
            self._assert_vec3_array_rotated(old_attr.Get(time_sample), new_attr.Get(time_sample), Z_TO_Y_ROTATION)

    async def test_YtoZSchemaPrim(self):
        """
        Tests that certian schema prims (cameras, rectLight, etc) have additional rotation applied when the up axis is
        changed from Y to Z, while other schema prims (sphere, cube, etc) do not have additional rotation applied.
        """
        stage_old, stage_new = self._open_stages(EMPTY_TEST_FILE)
        # create a camera in each stage
        camera_path = "/World/Camera"
        camera_old = UsdGeom.Camera.Define(stage_old, camera_path)
        camera_new = UsdGeom.Camera.Define(stage_new, camera_path)
        # create a cylinder light in each stage
        cylinder_light_path = "/World/CylinderLight"
        cylinder_light_old = UsdLux.CylinderLight.Define(stage_old, cylinder_light_path)
        cylinder_light_new = UsdLux.CylinderLight.Define(stage_new, cylinder_light_path)
        # create a disk light in each stage
        disk_light_path = "/World/DiskLight"
        disk_light_old = UsdLux.DiskLight.Define(stage_old, disk_light_path)
        disk_light_new = UsdLux.DiskLight.Define(stage_new, disk_light_path)
        # create a dome light in each stage
        dome_light_path = "/World/DomeLight"
        dome_light_old = UsdLux.DomeLight.Define(stage_old, dome_light_path)
        dome_light_new = UsdLux.DomeLight.Define(stage_new, dome_light_path)
        # create a sphere light in each stage
        sphere_light_path = "/World/SphereLight"
        sphere_light_old = UsdLux.SphereLight.Define(stage_old, sphere_light_path)
        sphere_light_new = UsdLux.SphereLight.Define(stage_new, sphere_light_path)
        # create a rect light in each stage with an existing transform
        rect_light_path = "/World/RectLight"
        rect_light_old = UsdLux.RectLight.Define(stage_old, rect_light_path)
        rect_light_new = UsdLux.RectLight.Define(stage_new, rect_light_path)
        for rect_light in [rect_light_old, rect_light_new]:
            xform = UsdGeom.Xform(rect_light.GetPrim())
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddTranslateOp(opSuffix="pivot").Set(value=(-10, 20, 30))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
            # existing rotateX:upAxisCorrection to ensure a new rotateX is added
            xform.AddXformOp(UsdGeom.XformOp.TypeRotateX, UsdGeom.XformOp.PrecisionFloat, "upAxisCorrection").Set(
                value=90
            )
        # create a distant light in each stage
        distant_light_path = "/World/DistantLight"
        distant_light_old = UsdLux.DistantLight.Define(stage_old, distant_light_path)
        distant_light_new = UsdLux.DistantLight.Define(stage_new, distant_light_path)
        # create a capsule in each stage
        capsule_path = "/World/Capsule"
        capsule_old = UsdGeom.Capsule.Define(stage_old, capsule_path)
        capsule_new = UsdGeom.Capsule.Define(stage_new, capsule_path)
        # create a cone in each stage
        cone_path = "/World/Cone"
        cone_old = UsdGeom.Cone.Define(stage_old, cone_path)
        cone_new = UsdGeom.Cone.Define(stage_new, cone_path)
        # create a cube in each stage with an existing transform
        cube_path = "/World/Cube"
        cube_old = UsdGeom.Cube.Define(stage_old, cube_path)
        cube_new = UsdGeom.Cube.Define(stage_new, cube_path)
        for cube in [cube_old, cube_new]:
            xform = UsdGeom.Xform(cube.GetPrim())
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddTranslateOp(opSuffix="pivot").Set(value=(-10, 20, 30))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        # create a cylinder in each stage
        cylinder_path = "/World/Cylinder"
        cylinder_old = UsdGeom.Cylinder.Define(stage_old, cylinder_path)
        cylinder_new = UsdGeom.Cylinder.Define(stage_new, cylinder_path)
        # create a plane in each stage
        plane_path = "/World/Plane"
        plane_old = UsdGeom.Plane.Define(stage_old, plane_path)
        plane_new = UsdGeom.Plane.Define(stage_new, plane_path)
        # create a sphere in each stage
        sphere_path = "/World/Sphere"
        sphere_old = UsdGeom.Sphere.Define(stage_old, sphere_path)
        sphere_new = UsdGeom.Sphere.Define(stage_new, sphere_path)
        # perform rotating
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        xformCache = UsdGeom.XformCache()
        # check camera xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(camera_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(camera_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check cylinder light xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(cylinder_light_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(cylinder_light_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check disk light xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(disk_light_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(disk_light_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check dome light xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(dome_light_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(dome_light_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check rect light xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(rect_light_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(rect_light_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check sphere light xforms (shouldn't have additional rotation applied)
        self._assert_matrix4_basis_changed(
            xformCache.GetLocalToWorldTransform(sphere_light_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(sphere_light_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check distant light xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(distant_light_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(distant_light_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check capsule xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(capsule_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(capsule_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check cone xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(cone_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(cone_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check cube xforms (shouldn't have additional rotation applied)
        self._assert_matrix4_basis_changed(
            xformCache.GetLocalToWorldTransform(cube_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(cube_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check cylinder xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(cylinder_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(cylinder_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check plane xforms
        self._assert_matrix4_basis_changed_and_rotated(
            xformCache.GetLocalToWorldTransform(plane_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(plane_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # check sphere xforms (shouldn't have additional rotation applied)
        self._assert_matrix4_basis_changed(
            xformCache.GetLocalToWorldTransform(sphere_old.GetPrim()),
            xformCache.GetLocalToWorldTransform(sphere_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )

    async def test_YtoZInstances(self):
        """
        Tests that Instance prims and the related prototype prim are rotated when the up axis is changed from Y to Z.
        """
        stage_old, stage_new = self._open_stages(INSTANCES_TEST_FILE)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        # this only checks the prototype
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)
        xformCache = UsdGeom.XformCache()
        # get the prototype prims and their transforms
        prototype_mesh_path = "/Flattened_Prototype_1/grid1/mesh_0"
        mesh_old = stage_old.GetPrimAtPath(prototype_mesh_path)
        mesh_matrix_old = xformCache.GetLocalToWorldTransform(mesh_old)
        mesh_new = stage_new.GetPrimAtPath(prototype_mesh_path)
        mesh_matrix_new = xformCache.GetLocalToWorldTransform(mesh_new)
        # discover the instances that use the prototype
        for prim_old in stage_old.Traverse():
            if not prim_old.IsInstance():
                continue
            prim_new = stage_new.GetPrimAtPath(prim_old.GetPath())
            # compute the full transforms
            instance_matrix_old = mesh_matrix_old * xformCache.GetLocalToWorldTransform(prim_old)
            instance_matrix_new = mesh_matrix_new * xformCache.GetLocalToWorldTransform(prim_new)
            # resolve points
            points_old = [instance_matrix_old.Transform(x) for x in UsdGeom.PointBased(mesh_old).GetPointsAttr().Get()]
            points_new = [instance_matrix_new.Transform(x) for x in UsdGeom.PointBased(mesh_new).GetPointsAttr().Get()]
            # check points are rotated
            for old_p, new_p in zip(points_old, points_new):
                rot_p = Y_TO_Z_ROTATION.rotation.TransformDir(old_p)
                for i in range(3):
                    self.assertTrue(rot_p[i] - new_p[i] < 0.0001)

    async def test_YtoZPointInstancer(self):
        """
        Tests that instances of a PointInstancer are rotated when the up axis is changed from Y to Z.
        """
        stage_old, stage_new = self._open_stages(POINT_INSTANCER_TEST_FILE)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        # this only checks the prototype
        self._assert_world_space_points_are_rotated(stage_old, stage_new, Y_TO_Z_ROTATION)
        # get the point instancer prim and check positions and orientations
        point_instancer_path = "/copytopoints1"
        point_instancer_old = UsdGeom.PointInstancer(stage_old.GetPrimAtPath(point_instancer_path))
        point_instancer_new = UsdGeom.PointInstancer(stage_new.GetPrimAtPath(point_instancer_path))
        self._assert_vec3_array_rotated(
            point_instancer_old.GetPositionsAttr().Get(), point_instancer_new.GetPositionsAttr().Get(), Y_TO_Z_ROTATION
        )
        self._assert_quat_array_rotated(
            point_instancer_old.GetOrientationsAttr().Get(),
            point_instancer_new.GetOrientationsAttr().Get(),
            Y_TO_Z_ROTATION,
        )

    async def test_YtoZVariants(self):
        """
        Tests that variants are rotated when the up axis is changed from Y to Z.
        """
        stage_old, stage_new = self._open_stages(VARIANTS_TEST_FILE)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)
        cube_path = "/World/Cube"
        cube_old = stage_old.GetPrimAtPath(cube_path)
        cube_new = stage_new.GetPrimAtPath(cube_path)
        xformCacheA = UsdGeom.XformCache()
        # check cube xforms (shouldn't have additional rotation applied)
        self._assert_matrix4_basis_changed(
            xformCacheA.GetLocalToWorldTransform(cube_old.GetPrim()),
            xformCacheA.GetLocalToWorldTransform(cube_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )
        # switch to the b variant and check the xforms
        stage_old.GetPrimAtPath("/World").GetVariantSet("test").SetVariantSelection("b")
        stage_new.GetPrimAtPath("/World").GetVariantSet("test").SetVariantSelection("b")
        xformCacheB = UsdGeom.XformCache()
        self._assert_matrix4_basis_changed(
            xformCacheB.GetLocalToWorldTransform(cube_old.GetPrim()),
            xformCacheB.GetLocalToWorldTransform(cube_new.GetPrim()),
            Y_TO_Z_ROTATION,
        )

    async def test_YtoZSublayerWithXform(self):
        """
        Tests that modifying a stage that applies an over to just a xformOp:rotateX xformOp in a sublayer will overlay
        the y rotation onto z rotation and update the xformOpOrder.
        """
        stage_old, stage_new = self._open_stages(EMPTY_TEST_FILE)
        # add a sublayer with geo with single axis rotate xformOps
        sub_layer = Sdf.Layer.OpenAsAnonymous(_get_test_data_file_path(SINGLE_AXIS_ROTATES_GEO_TEST_FILE))
        for stage in [stage_old, stage_new]:
            stage.GetRootLayer().subLayerPaths.append(sub_layer.identifier)
            prim = stage.GetPrimAtPath("/World/XformGeo")
            prim.GetAttribute("xformOp:rotateY").Set(33.0)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)

        prim = stage_new.GetPrimAtPath("/World/XformGeo")
        # rotateX should be left unchanged
        self.assertAlmostEqual(prim.GetAttribute("xformOp:rotateX").Get(), 12.0)
        # rotateY value should be unchanged - but it should no longer be in the xformOpOrder
        self.assertAlmostEqual(prim.GetAttribute("xformOp:rotateY").Get(), 45.0)
        # rotateZ is the result of original z rotation plus the value of the original y rotation
        self.assertAlmostEqual(prim.GetAttribute("xformOp:rotateZ").Get(), 8.0)
        # check the new xformOpOrder
        self.assertEqual(
            prim.GetAttribute("xformOpOrder").Get(),
            ["xformOp:translate", "xformOp:rotateX", "xformOp:rotateZ", "xformOp:scale"],
        )

    async def test_YtoZSublayerWithUpAxisCorrection(self):
        """Test Y-to-Z up-axis conversion on a sublayer that already has an upAxisCorrection xformOp."""
        stage_old, stage_new = self._open_stages(EMPTY_TEST_FILE)
        # add a sublayer with a camera that already has an upAxisCorrection xformOp
        sub_layer = Sdf.Layer.OpenAsAnonymous(_get_test_data_file_path(UP_AXIS_CORRECTION_TEST_FILE))
        for stage in [stage_old, stage_new]:
            stage.GetRootLayer().subLayerPaths.append(sub_layer.identifier)
            camera_prim = stage.DefinePrim("/World/Camera")
            camera_prim.SetSpecifier(Sdf.SpecifierDef)
        self._execute_json(stage_new, CONFIG_UP_AXIS_Z)

        prim = stage_new.GetPrimAtPath("/World/Camera")
        # check the original upAxisCorrection xformOp is there and has the correct value
        self.assertAlmostEqual(prim.GetAttribute("xformOp:rotateX:upAxisCorrection").Get(), -90.0)
        # check there is an additional upAxisCorrection xformOp
        self.assertAlmostEqual(prim.GetAttribute("xformOp:rotateX:upAxisCorrection1").Get(), 90.0)
        # check the new xformOpOrder
        self.assertEqual(
            prim.GetAttribute("xformOpOrder").Get(),
            [
                "xformOp:translate",
                "xformOp:rotateYXZ",
                "xformOp:scale",
                "xformOp:rotateX:upAxisCorrection",
                "xformOp:rotateX:upAxisCorrection1",
            ],
        )

    async def test_scaleAndYtoZGeoWithTRSAndPivot(self):
        """
        Tests that changing the meters per unit and the up axis from Y to Z works both scales and rotates the resolved
        points of the geo.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddTranslateOp(opSuffix="pivot").Set(value=(-10, 20, 30))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS_AND_UP_AXIS_Z)
        self._assert_world_space_points_are_scaled_and_rotated(stage_old, stage_new, 0.01, Y_TO_Z_ROTATION)

    async def test_scaleAndZtoYGeoWithTRSAndPivot(self):
        """
        Tests that changing the meters per unit and the up axis from Z to Y works both scales and rotates the resolved
        points of the geo.
        """
        stage_old, stage_new = self._open_stages(SIMPLE_GEO_TEST_FILE)
        # change up axis and apply translate, rotate, and scale xform to both stages
        for stage in [stage_old, stage_new]:
            UsdGeom.SetStageUpAxis(stage, UsdGeom.Tokens.z)
            xform = UsdGeom.Xform(stage.GetPrimAtPath("/World/SimpleGeo"))
            xform.AddTranslateOp().Set(value=(100, -200, 300))
            xform.AddTranslateOp(opSuffix="pivot").Set(value=(-10, 20, 30))
            xform.AddRotateXYZOp().Set(value=(-30, 60, -90))
            xform.AddScaleOp().Set(value=(1.5, 2.5, 3.0))
        self._execute_json(stage_new, CONFIG_SCALE_TO_METERS_AND_UP_AXIS_Y)
        self._assert_world_space_points_are_scaled_and_rotated(stage_old, stage_new, 0.01, Z_TO_Y_ROTATION)

    async def test_time_varying_meshes(self):
        """Test edit stage metrics operation on meshes with authored time varying attributes"""
        # Open the stage
        stage = self._open_stage("time_varying_meshes.usd")

        # several operations reported crashes on time varying meshes, this is a smoke test
        # run command, self validating
        self._execute_json(stage, CONFIG_SCALE_TO_METERS_AND_UP_AXIS_Y)

    async def test_scaleAndZtoYPhysics(self):
        """
        Tests that changing the meters per unit and the up axis from Z to Y affects special physics attributes
        correctly.
        """
        stage = self._open_stage(PHYSICS_TEST_FILE)
        self._execute_json(stage, CONFIG_SCALE_TO_CENTIMETERS_AND_UP_AXIS_Y)

        # get the prim with the angularVelocity attribute
        prim = stage.GetPrimAtPath("/World/AngVelCubeWithCOM")
        angVelAttr = prim.GetAttribute("physics:angularVelocity")
        # angular velocity should be rotated but not scaled
        self._assert_vectors_equal(angVelAttr.Get(), Gf.Vec3f(0.0, 1000.0, 0.0))

        # get the prim with mass set directly
        prim = stage.GetPrimAtPath("/World/MassTest1")
        massAttr = prim.GetAttribute("physics:mass")
        # mass should not be changed
        self.assertAlmostEqual(massAttr.Get(), 1000000.0)

        # get the prim with mass set via density
        prim = stage.GetPrimAtPath("/World/MassTest2")
        densityAttr = prim.GetAttribute("physics:density")
        # density should be changed to account for the scale change
        self.assertAlmostEqual(densityAttr.Get(), 10.0)

        # get the prim with phsyics limits
        prim = stage.GetPrimAtPath("/World/d6Joint")
        # schemas should have been reordered
        schemas = prim.GetAppliedSchemas()
        self.assertEquals(
            schemas,
            [
                "PhysicsLimitAPI:transX",
                "PhysicsLimitAPI:transZ",
                "PhysicsLimitAPI:transY",
                "PhysicsLimitAPI:rotX",
                "PhysicsLimitAPI:rotZ",
                "PhysicsLimitAPI:rotY",
            ],
        )
        # x rotation attributes should be left untouched
        self.assertAlmostEqual(prim.GetAttribute("limit:rotX:physics:high").Get(), 0.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:rotX:physics:low").Get(), -180.0)
        # y and z rotation attributes should be flipped but not scaled
        self.assertAlmostEqual(prim.GetAttribute("limit:rotY:physics:high").Get(), -10.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:rotY:physics:low").Get(), 10.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:rotZ:physics:high").Get(), 45.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:rotZ:physics:low").Get(), -45.0)
        # x translation should have just been scaled
        self.assertAlmostEqual(prim.GetAttribute("limit:transX:physics:high").Get(), -100.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:transX:physics:low").Get(), 100.0)
        # y and z translations should be flipped and scaled
        self.assertAlmostEqual(prim.GetAttribute("limit:transY:physics:high").Get(), 100.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:transY:physics:low").Get(), -100.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:transZ:physics:high").Get(), 400.0)
        self.assertAlmostEqual(prim.GetAttribute("limit:transZ:physics:low").Get(), -400.0)

    async def test_ignoreKitCamerasOn(self):
        """
        Tests that the ignoreKitCameras argument works correctly when on
        """
        stage = self._open_stage(KIT_CAMERAS_TEST_FILE)
        self._execute_json(stage, CONFIG_IGNORE_KIT_CAMERAS_ON)

        persp_prim = stage.GetPrimAtPath("/OmniverseKit_Persp")

        self._assert_vectors_equal(persp_prim.GetAttribute("xformOp:translate").Get(), Gf.Vec3f(500.0, 500.0, 500.0))
        self._assert_vectors_equal(persp_prim.GetAttribute("xformOp:rotateXYZ").Get(), Gf.Vec3f(-35, 45, 0.0))

    async def test_ignoreKitCamerasOff(self):
        """
        Tests that the ignoreKitCameras argument works correctly when off
        """
        stage = self._open_stage(KIT_CAMERAS_TEST_FILE)
        self._execute_json(stage, CONFIG_IGNORE_KIT_CAMERAS_OFF)

        persp_prim = stage.GetPrimAtPath("/OmniverseKit_Persp")

        self._assert_vectors_equal(persp_prim.GetAttribute("xformOp:translate").Get(), Gf.Vec3f(5.0, -5.0, 5.0))
        self._assert_vectors_equal(persp_prim.GetAttribute("xformOp:rotateXYZ").Get(), Gf.Vec3f(-35, 0.0, 45.0))
