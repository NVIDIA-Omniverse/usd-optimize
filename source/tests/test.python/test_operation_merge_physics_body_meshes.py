# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Sdf, Usd, UsdGeom, UsdPhysics

from .test_utils import Test_Operation, _get_context


def _body(stage, path):
    """Define an Xform with RigidBodyAPI for an ownership scenario."""
    prim = UsdGeom.Xform.Define(stage, path).GetPrim()
    UsdPhysics.RigidBodyAPI.Apply(prim)
    return prim


def _mesh(stage, path):
    """Define a Mesh prim for candidate discovery."""
    return UsdGeom.Mesh.Define(stage, path).GetPrim()


def _scope(body, parent, *meshes, collision=(), visual_shapes=(), collision_shapes=()):
    """Build the expected candidate-scope report."""
    return {
        "rigidBody": body,
        "outputParent": parent,
        "visualOnlyMeshes": list(meshes),
        "collisionMeshes": list(collision),
        "visualShapes": list(visual_shapes),
        "collisionShapes": list(collision_shapes),
    }


class Test_Operation_MergePhysicsBodyMeshes(Test_Operation):
    OPERATION = "mergePhysicsBodyMeshes"

    def _discover(self, stage, paths=None):
        """Return discovery results and require every source layer to remain unchanged."""
        before = {layer.identifier: layer.ExportToString() for layer in stage.GetUsedLayers()}
        _, result = self._execute_command({"paths": paths or []}, _get_context(stage, analysis=True))
        self.assertTrue(result[0], result[1])
        self.assertEqual({layer.identifier: layer.ExportToString() for layer in stage.GetUsedLayers()}, before)
        return result[2]["analysis"]

    def _open_shared_collision_collection(self, indirect):
        """Open colliders whose filtering collection also binds their material, directly or through another collection."""
        stage = self._open_stage("mergePhysicsBodyMeshes/connections.usda")
        root = "/SharedCollisionCollection"
        collection = UsdPhysics.CollisionGroup(stage.GetPrimAtPath(root + "/Group")).GetCollidersCollectionAPI()
        if indirect:
            included = Usd.CollectionAPI.Apply(stage.GetPrimAtPath(root), "IncludedColliders")
            included.CreateIncludesRel().SetTargets(collection.GetIncludesRel().GetTargets())
            collection.GetIncludesRel().SetTargets([included.GetCollectionPath()])
            stage.GetPrimAtPath(root + "/Body").GetRelationship(
                "material:binding:collection:physics:Colliders"
            ).SetTargets([included.GetCollectionPath(), root + "/Material"])
        return stage, collection

    def test_bodies_have_separate_output_scopes(self):
        # Each body owns its meshes; articulation alone does not make unowned meshes merge candidates.
        """Each body owns its meshes; articulation alone does not make unowned meshes merge candidates."""
        stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")
        stage.SetDefaultPrim(stage.GetPrimAtPath("/Robot"))

        analysis = self._discover(stage, ["/Robot", "/Standalone"])

        self.assertCountEqual(
            analysis["scopes"],
            [
                _scope(body, body, body + "/Geometry/A", body + "/Geometry/B")
                for body in ("/Robot/Base", "/Robot/Arm", "/Standalone")
            ],
        )
        self.assertIn(
            {"path": "/Robot/Unowned", "reason": "no rigid body found within traversal start"}, analysis["skipped"]
        )

    def test_nested_bodies_do_not_require_transform_resets(self):
        # Nested bodies use the nearest owner, with or without a reset or an enabled simulation state.
        """Nested bodies use the nearest owner, with or without a reset or an enabled simulation state."""
        for reset in (False, True):
            for enabled in (False, True):
                with self.subTest(reset=reset, enabled=enabled):
                    stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")
                    nested = stage.GetPrimAtPath("/NestedBodies/Nested")
                    UsdGeom.Xformable(nested).SetResetXformStack(reset)
                    UsdPhysics.RigidBodyAPI(nested).CreateRigidBodyEnabledAttr(enabled)

                    analysis = self._discover(stage, ["/NestedBodies"])

                    self.assertCountEqual(
                        analysis["scopes"],
                        [
                            _scope(
                                "/NestedBodies", "/NestedBodies", "/NestedBodies/Outer", "/NestedBodies/AfterNested"
                            ),
                            _scope("/NestedBodies/Nested", "/NestedBodies/Nested", "/NestedBodies/Nested/Inner"),
                        ],
                    )

    def test_overlapping_traversal_starts_include_each_body_once(self):
        # Starts above or on bodies include each selected body once, ignoring duplicate and descendant starts.
        """Starts above or on bodies include each selected body once, ignoring duplicate and descendant starts."""
        stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")

        analysis = self._discover(stage, ["/World/Right/Body", "/World/Left/Body/A", "/World/Left", "/World/Left"])

        self.assertCountEqual(
            analysis["scopes"],
            [_scope(body, body, body + "/A", body + "/B") for body in ("/World/Left/Body", "/World/Right/Body")],
        )
        self.assertEqual(analysis["rejectedPaths"], [])

    def test_physics_apis_on_meshes_establish_boundaries(self):
        # A Mesh that owns a body or articulation keeps its own geometry and descendant candidates below itself.
        """A Mesh that owns a body or articulation keeps its own geometry and descendant candidates below itself."""
        for scenario in ("RigidMeshBoundary", "ArticulationMeshBoundary"):
            with self.subTest(scenario=scenario):
                stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")
                body = "/" + scenario
                boundary = body + "/Boundary"
                owner = boundary if scenario == "RigidMeshBoundary" else body

                analysis = self._discover(stage, [body])

                self.assertCountEqual(
                    analysis["scopes"],
                    [
                        _scope(owner, boundary, boundary, boundary + "/Child"),
                        _scope(boundary + "/Nested", boundary + "/Nested", boundary + "/Nested/Inner"),
                    ],
                )

    def test_selection_below_body_limits_nested_body_discovery(self):
        # A start below a body discovers nested bodies but does not use the ancestor body or sibling hierarchies.
        """A start below a body discovers nested bodies but does not use the ancestor body or sibling hierarchies."""
        stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")

        analysis = self._discover(stage, ["/SelectedDescendants/Selected"])

        self.assertEqual(
            analysis["scopes"],
            [
                _scope(
                    "/SelectedDescendants/Selected/Nested",
                    "/SelectedDescendants/Selected/Nested",
                    "/SelectedDescendants/Selected/Nested/Visual",
                )
            ],
        )
        self.assertIn(
            {"path": "/SelectedDescendants/Selected/Visual", "reason": "no rigid body found within traversal start"},
            analysis["skipped"],
        )

    def test_articulation_container_and_joint_do_not_own_geometry(self):
        # Articulation containers bound placement; an articulation on a joint does not reassign sibling body ownership.
        """Articulation containers bound placement; an articulation on a joint does not reassign sibling body
        ownership.
        """
        stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")

        analysis = self._discover(stage, ["/ArticulationBoundary"])

        self.assertCountEqual(
            analysis["scopes"],
            [
                _scope("/ArticulationBoundary", "/ArticulationBoundary", "/ArticulationBoundary/Visual"),
                _scope(
                    "/ArticulationBoundary",
                    "/ArticulationBoundary/Articulation",
                    "/ArticulationBoundary/Articulation/Visual",
                ),
            ],
        )

    def test_transform_and_visibility_boundaries(self):
        # Output stays below animated and reset boundaries, including boundaries applied directly to Mesh prims.
        """Output stays below animated and reset boundaries, including boundaries applied directly to Mesh prims."""
        for kind in ("transform", "reset", "visibility"):
            for schema in (UsdGeom.Xform, UsdGeom.Mesh):
                with self.subTest(kind=kind, schema=schema):
                    stage = Usd.Stage.CreateInMemory()
                    _body(stage, "/Body")
                    _mesh(stage, "/Body/Visual")
                    boundary = schema.Define(stage, "/Body/Boundary").GetPrim()
                    if kind == "transform":
                        op = UsdGeom.Xformable(boundary).AddTranslateOp()
                        op.Set((0, 0, 0), 0)
                        op.Set((1, 0, 0), 1)
                    elif kind == "reset":
                        UsdGeom.Xformable(boundary).SetResetXformStack(True)
                    else:
                        attr = UsdGeom.Imageable(boundary).CreateVisibilityAttr()
                        attr.Set(UsdGeom.Tokens.inherited, 0)
                        attr.Set(UsdGeom.Tokens.invisible, 1)
                    _mesh(stage, "/Body/Boundary/Child")
                    meshes = ["/Body/Boundary/Child"]
                    if schema == UsdGeom.Mesh:
                        meshes.insert(0, "/Body/Boundary")

                    analysis = self._discover(stage)

                    self.assertCountEqual(
                        analysis["scopes"],
                        [
                            _scope("/Body", "/Body", "/Body/Visual"),
                            _scope("/Body", "/Body/Boundary", *meshes),
                        ],
                    )

    def test_discovery_does_not_bucket_or_prepare_geometry(self):
        # Incompatible properties and analytic shapes remain candidates without preparing geometry in analysis.
        """Incompatible properties and analytic shapes remain candidates without preparing geometry in analysis."""
        stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")

        analysis = self._discover(stage, ["/CandidateGeometry"])

        self.assertEqual(
            analysis["scopes"],
            [
                _scope(
                    "/CandidateGeometry",
                    "/CandidateGeometry",
                    "/CandidateGeometry/Visual",
                    "/CandidateGeometry/OtherVisual",
                    collision=["/CandidateGeometry/Collider"],
                    visual_shapes=["/CandidateGeometry/Shape"],
                )
            ],
        )

    def test_collision_classification_uses_applied_api(self):
        # CollisionAPI separates candidates regardless of visibility or enabled state and is not inherited.
        """CollisionAPI separates candidates regardless of visibility or enabled state and is not inherited."""
        for visible in (False, True):
            for enabled in (False, True):
                with self.subTest(visible=visible, enabled=enabled):
                    stage = Usd.Stage.CreateInMemory()
                    body = _body(stage, "/Body")
                    UsdPhysics.CollisionAPI.Apply(body)
                    _mesh(stage, "/Body/Visual")
                    collider = _mesh(stage, "/Body/Collider")
                    UsdPhysics.CollisionAPI.Apply(collider).CreateCollisionEnabledAttr(enabled)
                    UsdGeom.Imageable(collider).CreateVisibilityAttr(
                        UsdGeom.Tokens.inherited if visible else UsdGeom.Tokens.invisible
                    )

                    analysis = self._discover(stage)

                    self.assertEqual(
                        analysis["scopes"],
                        [_scope("/Body", "/Body", "/Body/Visual", collision=["/Body/Collider"])],
                    )

    def test_known_compatibility_relationships_remain_candidates(self):
        # Material bindings and simulation ownership remain eligible without comparing effective values yet.
        """Material bindings and simulation ownership remain eligible without comparing effective values yet."""
        stage = self._open_stage("mergePhysicsBodyMeshes/connections.usda")

        analysis = self._discover(stage, ["/KnownRelationships"])

        self.assertEqual(
            analysis["scopes"],
            [
                _scope(
                    "/KnownRelationships",
                    "/KnownRelationships",
                    "/KnownRelationships/A",
                    "/KnownRelationships/B",
                    collision=["/KnownRelationships/Collider"],
                )
            ],
        )

    def test_unknown_outgoing_relationships_exclude_geometry(self):
        # Unknown non-empty relationships on a mesh or subset exclude that mesh; empty relationships are benign.
        """Unknown non-empty relationships on a mesh or subset exclude that mesh; empty relationships are benign."""
        for on_subset in (False, True):
            with self.subTest(on_subset=on_subset):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                mesh = _mesh(stage, "/Body/Dependent")
                source = UsdGeom.Subset.Define(stage, "/Body/Dependent/Faces").GetPrim() if on_subset else mesh
                source.CreateRelationship("custom:dependency").SetTargets(["/Other"])
                stage.DefinePrim("/Other")
                visual = _mesh(stage, "/Body/Visual")
                visual.CreateRelationship("custom:empty")
                visual.CreateRelationship("custom:cleared").SetTargets([])
                UsdGeom.Mesh(visual).CreatePointsAttr().SetConnections([])

                analysis = self._discover(stage)

                self.assertEqual(analysis["scopes"], [_scope("/Body", "/Body", "/Body/Visual")])
                self.assertIn(
                    {
                        "path": "/Body/Dependent",
                        "reason": "unsupported relationship: " + str(source.GetPath()) + ".custom:dependency",
                    },
                    analysis["skipped"],
                )

    def test_external_references_to_geometry_exclude_mesh(self):
        # References from outside the selection protect a mesh, its properties, and its subsets from replacement.
        """References from outside the selection protect a mesh, its properties, and its subsets from replacement."""
        for target in (
            "/Body/Referenced",
            "/Body/Referenced.points",
            "/Body/Referenced/Faces",
            "/Body/Referenced/Faces.indices",
        ):
            with self.subTest(target=target):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                _mesh(stage, "/Body/Referenced")
                UsdGeom.Subset.Define(stage, "/Body/Referenced/Faces")
                _mesh(stage, "/Body/Visual")
                stage.DefinePrim("/Outside").CreateRelationship("geometry").SetTargets([target])

                analysis = self._discover(stage, ["/Body"])

                self.assertEqual(analysis["scopes"], [_scope("/Body", "/Body", "/Body/Visual")])
                self.assertIn(
                    {"path": "/Body/Referenced", "reason": "referenced by /Outside.geometry: " + target},
                    analysis["skipped"],
                )

    def test_instance_proxy_connections_exclude_geometry(self):
        # Relationships and attribute connections inside an instance protect geometry outside that instance.
        """Instance-proxy consumers prevent replacement of the geometry they reference."""
        for connection in ("relationship", "attribute"):
            with self.subTest(connection=connection):
                stage = self._open_stage("mergePhysicsBodyMeshes/connections.usda")
                body = "/InstanceConsumer/Body"
                target = body + "/Referenced"
                property_name = "geometry"
                if connection == "attribute":
                    reader = stage.GetPrimAtPath("/InstanceConsumer/ReaderClass/Reader")
                    reader.RemoveProperty("geometry")
                    target += ".points"
                    property_name = "input"
                    reader.CreateAttribute(property_name, Sdf.ValueTypeNames.Point3fArray).SetConnections([target])
                self.assertTrue(stage.GetPrimAtPath("/InstanceConsumer/Consumer/Reader").IsInstanceProxy())

                analysis = self._discover(stage, [body])

                self.assertEqual(analysis["scopes"], [_scope(body, body, body + "/Visual")])
                self.assertIn(
                    {
                        "path": body + "/Referenced",
                        "reason": "referenced by /InstanceConsumer/Consumer/Reader." + property_name + ": " + target,
                    },
                    analysis["skipped"],
                )

    def test_collision_group_material_collections_exclude_geometry(self):
        # A collection used for collision filtering protects its colliders even when it also binds their material.
        """Direct and included collision-group collections remain geometry dependencies."""
        for indirect in (False, True):
            with self.subTest(indirect=indirect):
                stage, _ = self._open_shared_collision_collection(indirect)
                root = "/SharedCollisionCollection"
                source = root + (
                    ".collection:IncludedColliders:includes" if indirect else "/Group.collection:colliders:includes"
                )

                analysis = self._discover(stage, [root + "/Body"])

                self.assertEqual(analysis["scopes"], [])
                for name in ("A", "B"):
                    path = root + "/Body/" + name
                    self.assertIn(
                        {"path": path, "reason": "referenced by " + source + ": " + path}, analysis["skipped"]
                    )

    def test_shared_collision_collection_membership_is_preserved(self):
        # Collider merging leaves collision-group members intact when the same collection binds their material.
        """Collision filtering retains the same active colliders after normal execution."""
        for indirect in (False, True):
            with self.subTest(indirect=indirect):
                stage, collection = self._open_shared_collision_collection(indirect)
                body = "/SharedCollisionCollection/Body"
                expected = {Sdf.Path(body + "/A"), Sdf.Path(body + "/B")}
                self.assertCountEqual(
                    Usd.CollectionAPI.ComputeIncludedPaths(collection.ComputeMembershipQuery(), stage), expected
                )

                _, result = self._execute_command({"paths": [body], "mergeColliders": True}, _get_context(stage))

                self.assertTrue(result[0], result[1])
                self.assertEqual(result[2]["merged"], [])
                self.assertCountEqual(
                    Usd.CollectionAPI.ComputeIncludedPaths(collection.ComputeMembershipQuery(), stage), expected
                )

    def test_joint_target_mesh_keeps_frame_as_output_parent(self):
        # Joint targets retain their frame as an output parent, whether the Mesh owns or inherits its rigid body.
        """Joint targets retain their frame as an output parent, whether the Mesh owns or inherits its rigid body."""
        for is_body in (False, True):
            with self.subTest(is_body=is_body):
                stage = Usd.Stage.CreateInMemory()
                _body(stage, "/Body")
                mesh = _mesh(stage, "/Body/JointTarget")
                if is_body:
                    UsdPhysics.RigidBodyAPI.Apply(mesh)
                UsdPhysics.FixedJoint.Define(stage, "/OutsideJoint").CreateBody1Rel().SetTargets([mesh.GetPath()])

                analysis = self._discover(stage, ["/Body"])

                owner = "/Body/JointTarget" if is_body else "/Body"
                self.assertEqual(
                    analysis["scopes"],
                    [_scope(owner, "/Body/JointTarget", "/Body/JointTarget")],
                )
                self.assertEqual(analysis["skipped"], [])

    def test_attribute_connections_exclude_geometry(self):
        # Incoming and outgoing attribute connections protect affected mesh geometry, including through forwarding.
        """Incoming and outgoing attribute connections protect affected mesh geometry, including through forwarding."""
        for scenario in ("IncomingConnection", "OutgoingConnection", "ForwardedConnection"):
            with self.subTest(scenario=scenario):
                stage = self._open_stage("mergePhysicsBodyMeshes/connections.usda")
                body = "/" + scenario + "/Body"

                analysis = self._discover(stage, [body])

                self.assertEqual(analysis["scopes"], [_scope(body, body, body + "/Visual")])
                self.assertEqual([item["path"] for item in analysis["skipped"]], [body + "/Connected"])

    def test_retained_body_does_not_allow_other_geometry_references(self):
        # Retaining a body allows direct joint body targets, not arbitrary references or targets to its geometry data.
        """Retaining a body allows direct joint body targets, not arbitrary references or targets to its geometry
        data.
        """
        for joint_property in (False, True):
            with self.subTest(joint_property=joint_property):
                stage = Usd.Stage.CreateInMemory()
                body = _mesh(stage, "/Body")
                UsdPhysics.RigidBodyAPI.Apply(body)
                if joint_property:
                    UsdPhysics.FixedJoint.Define(stage, "/Joint").CreateBody1Rel().SetTargets(["/Body.points"])
                else:
                    stage.DefinePrim("/Outside").CreateRelationship("geometry").SetTargets(["/Body"])

                analysis = self._discover(stage)

                self.assertEqual(analysis["scopes"], [])
                self.assertEqual([item["path"] for item in analysis["skipped"]], ["/Body"])

    def test_reference_to_inactive_subset_protects_owning_mesh(self):
        # An active external reference to an inactive subset still prevents replacing its owning mesh.
        """An active external reference to an inactive subset still prevents replacing its owning mesh."""
        stage = self._open_stage("mergePhysicsBodyMeshes/connections.usda")

        analysis = self._discover(stage, ["/InactiveSubset/Body"])

        self.assertEqual(analysis["scopes"], [])
        self.assertIn(
            {
                "path": "/InactiveSubset/Body/Referenced",
                "reason": "referenced by /InactiveSubset/Outside.faces: /InactiveSubset/Body/Referenced/Faces",
            },
            analysis["skipped"],
        )

    def test_dependencies_bound_container_output(self):
        # Output remains below referenced containers and containers with unknown outgoing dependencies.
        """Output remains below referenced containers and containers with unknown outgoing dependencies."""
        for scenario in ("IncomingContainer", "OutgoingContainer"):
            with self.subTest(scenario=scenario):
                stage = self._open_stage("mergePhysicsBodyMeshes/connections.usda")
                body = "/" + scenario + "/Body"
                container = body + "/Container"

                analysis = self._discover(stage, [body])

                self.assertCountEqual(
                    analysis["scopes"],
                    [
                        _scope(body, body, body + "/Visual"),
                        _scope(body, container, container + "/A", container + "/B"),
                    ],
                )

    def test_mesh_with_children_is_retained_as_output_parent(self):
        # A mesh with hierarchy children keeps those children attached while its geometry remains a candidate.
        """A mesh with hierarchy children keeps those children attached while its geometry remains a candidate."""
        stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")

        analysis = self._discover(stage, ["/HierarchyChildren"])

        self.assertCountEqual(
            analysis["scopes"],
            [
                _scope("/HierarchyChildren", "/HierarchyChildren", "/HierarchyChildren/Sibling"),
                _scope(
                    "/HierarchyChildren",
                    "/HierarchyChildren/Parent",
                    "/HierarchyChildren/Parent",
                    "/HierarchyChildren/Parent/Child",
                ),
            ],
        )

    def test_excluded_hierarchies_and_inactive_meshes(self):
        # Discovery excludes skeleton, camera, material, and instance hierarchies and inactive or abstract meshes.
        """Discovery excludes skeleton, camera, material, and instance hierarchies and inactive or abstract meshes."""
        stage = self._open_stage("mergePhysicsBodyMeshes/discovery.usda")

        analysis = self._discover(stage, ["/ExcludedHierarchies"])

        self.assertEqual(
            analysis["scopes"], [_scope("/ExcludedHierarchies", "/ExcludedHierarchies", "/ExcludedHierarchies/Visual")]
        )
        self.assertCountEqual(
            [entry["path"] for entry in analysis["skipped"]],
            [
                "/ExcludedHierarchies/Skeleton",
                "/ExcludedHierarchies/Camera",
                "/ExcludedHierarchies/Instances",
            ],
        )

    def test_explicit_mesh_and_pseudo_root_selections(self):
        # A Mesh start is a body only with RigidBodyAPI; the pseudo-root matches whole-stage discovery.
        """A Mesh start is a body only with RigidBodyAPI; the pseudo-root matches whole-stage discovery."""
        stage = Usd.Stage.CreateInMemory()
        _body(stage, "/Body")
        _mesh(stage, "/Body/Visual")
        mesh_body = _mesh(stage, "/Body/MeshBody")
        UsdPhysics.RigidBodyAPI.Apply(mesh_body)

        selected = self._discover(stage, ["/Body/Visual"])
        selected_body = self._discover(stage, ["/Body/MeshBody"])
        whole_stage = self._discover(stage)
        pseudo_root = self._discover(stage, ["/"])

        self.assertEqual(selected["scopes"], [])
        self.assertIn(
            {"path": "/Body/Visual", "reason": "no rigid body found within traversal start"}, selected["skipped"]
        )
        self.assertEqual(selected_body["scopes"], [_scope("/Body/MeshBody", "/Body/MeshBody", "/Body/MeshBody")])
        self.assertEqual(whole_stage, pseudo_root)

    def test_invalid_selections_do_not_block_valid_paths(self):
        # Rejected starts retain their input and reason while valid starts still discover bodies.
        """Rejected starts retain their input and reason while valid starts still discover bodies."""
        stage = Usd.Stage.CreateInMemory()
        _body(stage, "/Body")
        _mesh(stage, "/Body/Visual")
        _body(stage, "/OtherBody")
        _mesh(stage, "/OtherBody/Visual")
        invalid_path = "invalid prim path; path expressions are not supported"
        expected_absolute_path = "expected an absolute prim path"
        cases = (
            ("", invalid_path),
            ("/Bad Path", invalid_path),
            ("Body", expected_absolute_path),
            ("/Body.visibility", expected_absolute_path),
            ("/Body{variant=value}", expected_absolute_path),
            ("/Missing", "prim does not exist"),
            ("/Other*", invalid_path),
            ("//{hasAPI:PhysicsRigidBodyAPI}", invalid_path),
            ("/Body + /OtherBody", invalid_path),
        )
        for path, reason in cases:
            with self.subTest(path=path):
                analysis = self._discover(stage, [path, "/Body/Visual", "/Body", "/Body"])

                self.assertEqual(analysis["scopes"], [_scope("/Body", "/Body", "/Body/Visual")])
                self.assertEqual(analysis["rejectedPaths"], [{"path": path, "reason": reason}])

    def test_all_invalid_selections_do_not_select_whole_stage(self):
        # All rejected starts are reported and succeed without traversing the stage.
        """All rejected starts are reported and succeed without traversing the stage."""
        stage = Usd.Stage.CreateInMemory()
        _body(stage, "/Body")
        _mesh(stage, "/Body/Visual")

        analysis = self._discover(stage, ["/Missing", "/Body/*", ""])

        self.assertEqual(analysis["scopes"], [])
        self.assertCountEqual(
            analysis["rejectedPaths"],
            [
                {"path": "/Missing", "reason": "prim does not exist"},
                {"path": "/Body/*", "reason": "invalid prim path; path expressions are not supported"},
                {"path": "", "reason": "invalid prim path; path expressions are not supported"},
            ],
        )

    def test_empty_stage_has_no_candidates(self):
        # An empty stage is a successful discovery with no candidates or skipped prims.
        """An empty stage is a successful discovery with no candidates or skipped prims."""
        analysis = self._discover(Usd.Stage.CreateInMemory())

        self.assertEqual(analysis["scopes"], [])
        self.assertEqual(analysis["skipped"], [])
        self.assertEqual(analysis["rejectedPaths"], [])

    def test_normal_execution_preserves_empty_geometry(self):
        # Empty Mesh prims have no mergeable geometry and remain unchanged during normal execution.
        """Empty Mesh prims have no mergeable geometry and remain unchanged during normal execution."""
        stage = Usd.Stage.CreateInMemory()
        _body(stage, "/Body")
        _mesh(stage, "/Body/A")
        _mesh(stage, "/Body/B")
        before = stage.GetRootLayer().ExportToString()

        _, result = self._execute_command({}, _get_context(stage))

        self.assertTrue(result[0], result[1])
        self.assertEqual(result[2]["merged"], [])
        self.assertEqual(stage.GetRootLayer().ExportToString(), before)
