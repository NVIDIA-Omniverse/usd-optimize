# SPDX-FileCopyrightText: Copyright (c) 2024-2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

from pxr import Pcp, Usd

from .test_utils import Test_Operation, _get_context


class Test_Operation_Deduplicate_Hierarchies(Test_Operation):

    OPERATION = "deduplicateHierarchies"

    def assert_is_instance_ref_to(self, prim: Usd.Prim, target_path: str):
        """Assert prim has an internal reference to target_path and is instanceable."""
        self.assertTrue(prim.IsValid(), f"prim not valid: {prim.GetPath()}")
        self.assertTrue(prim.IsInstanceable(), f"prim not instanceable: {prim.GetPath()}")
        self.assertEqual(len(prim.GetAllChildren()), 0, f"prim still has children: {prim.GetPath()}")
        arcs = Usd.PrimCompositionQuery.GetDirectReferences(prim).GetCompositionArcs()
        targets = []
        for arc in arcs:
            if arc.GetArcType() == Pcp.ArcTypeReference:
                targets.append(str(arc.GetTargetPrimPath()))
        self.assertIn(
            target_path, targets, f"prim {prim.GetPath()} missing reference to {target_path}; found {targets}"
        )

    async def test_basic_dedup(self):
        """Three structurally identical sibling Xforms — first stays, other two become refs.

        A fourth structurally-equivalent prim with an *already-authored* reference must be
        excluded from the duplicate group: its existing reference is preserved
        verbatim and we do not author a new one over it.
        """
        stage = self._open_stage("dedupHierarchies_basic.usda")

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        # Prototype is untouched.
        proto = stage.GetPrimAtPath("/World/Tree_Original")
        self.assertTrue(proto.IsValid())
        self.assertFalse(proto.IsInstanceable())
        self.assertGreater(len(proto.GetAllChildren()), 0)

        # The two duplicates are now instanceable refs to the prototype.
        self.assert_is_instance_ref_to(stage.GetPrimAtPath("/World/Tree_Copy1"), "/World/Tree_Original")
        self.assert_is_instance_ref_to(stage.GetPrimAtPath("/World/Tree_Copy2"), "/World/Tree_Original")

        # The already-referenced fourth tree must keep its authored ref intact.
        # We did not set instanceable on it, did not clear+rewrite its ref list,
        # and did not delete its (empty) child set.
        already = stage.GetPrimAtPath("/World/Tree_Already_Referenced")
        self.assertTrue(already.IsValid())
        self.assertTrue(already.HasAuthoredReferences())
        # The original ref target is /World/Tree_Original — confirm still pointing there.
        arcs = Usd.PrimCompositionQuery.GetDirectReferences(already).GetCompositionArcs()
        targets = [str(a.GetTargetPrimPath()) for a in arcs if a.GetArcType() == Pcp.ArcTypeReference]
        self.assertIn("/World/Tree_Original", targets)
        # And its instanceable bit was not changed by us.
        self.assertFalse(already.IsInstanceable())

        # The standalone Bush is left alone.
        bush = stage.GetPrimAtPath("/World/Bush")
        self.assertTrue(bush.IsValid())
        self.assertFalse(bush.IsInstanceable())

        # Looks scope is left alone (material-related skip).
        looks = stage.GetPrimAtPath("/World/Looks")
        self.assertTrue(looks.IsValid())

    async def test_no_duplicates_is_no_op(self):
        """A stage with structurally distinct prims must be left untouched and succeed."""
        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)
        a = stage.DefinePrim("/World/A", "Xform")
        a.SetDisplayName("Alpha")
        stage.DefinePrim("/World/A/Child1", "Mesh")
        b = stage.DefinePrim("/World/B", "Xform")
        b.SetDisplayName("Beta")
        stage.DefinePrim("/World/B/Child1", "Mesh")
        stage.DefinePrim("/World/B/Child2", "Mesh")

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        for path in ("/World/A", "/World/B"):
            prim = stage.GetPrimAtPath(path)
            self.assertTrue(prim.IsValid())
            self.assertFalse(prim.IsInstanceable())
            self.assertFalse(prim.HasAuthoredReferences())

    async def test_paths_arg_restricts_subtree(self):
        """When `paths` is set, the BFS starts at children of those subtrees only."""
        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)
        # Subtree A has duplicates we DO want processed.
        stage.DefinePrim("/World/BucketA", "Xform")
        for name in ("Tree_X", "Tree_Y"):
            child = stage.DefinePrim(f"/World/BucketA/{name}", "Xform")
            child.SetDisplayName("Tree")
            stage.DefinePrim(f"/World/BucketA/{name}/Mesh", "Xform")
        # Subtree B also has duplicates but must be untouched.
        stage.DefinePrim("/World/BucketB", "Xform")
        for name in ("Tree_P", "Tree_Q"):
            child = stage.DefinePrim(f"/World/BucketB/{name}", "Xform")
            child.SetDisplayName("Tree")
            stage.DefinePrim(f"/World/BucketB/{name}/Mesh", "Xform")

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"paths": ["/World/BucketA"]}, context)
        self.assertTrue(ok)

        # BucketA's second instance should now be a ref.
        a_dup = stage.GetPrimAtPath("/World/BucketA/Tree_Y")
        self.assertTrue(a_dup.IsInstanceable())
        self.assertTrue(a_dup.HasAuthoredReferences())

        # BucketB must be untouched.
        for name in ("Tree_P", "Tree_Q"):
            prim = stage.GetPrimAtPath(f"/World/BucketB/{name}")
            self.assertFalse(prim.IsInstanceable())
            self.assertFalse(prim.HasAuthoredReferences())

    async def test_no_default_prim_no_paths_is_safe_no_op(self):
        """No default prim and no `paths` argument: emit a warning and succeed without mutating."""
        stage = Usd.Stage.CreateInMemory()
        # Create some content but never SetDefaultPrim -> stage has no default.
        a = stage.DefinePrim("/World/A", "Xform")
        a.SetDisplayName("Tree")
        b = stage.DefinePrim("/World/B", "Xform")
        b.SetDisplayName("Tree")

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)
        # No default prim means we never started traversal -> nothing changed.
        for path in ("/World/A", "/World/B"):
            prim = stage.GetPrimAtPath(path)
            self.assertFalse(prim.IsInstanceable())
            self.assertFalse(prim.HasAuthoredReferences())

    async def test_no_default_prim_with_explicit_paths(self):
        """Explicit `paths` works even when the stage has no default prim."""
        stage = Usd.Stage.CreateInMemory()
        # Note: no SetDefaultPrim
        stage.DefinePrim("/Lab/Bucket", "Xform")
        for name in ("Tree_X", "Tree_Y"):
            child = stage.DefinePrim(f"/Lab/Bucket/{name}", "Xform")
            child.SetDisplayName("Tree")
            stage.DefinePrim(f"/Lab/Bucket/{name}/Mesh", "Xform")

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"paths": ["/Lab/Bucket"]}, context)
        self.assertTrue(ok)

        # The second instance is now a ref to the first.
        dup = stage.GetPrimAtPath("/Lab/Bucket/Tree_Y")
        self.assertTrue(dup.IsInstanceable())
        self.assertTrue(dup.HasAuthoredReferences())

    async def test_nested_duplicate_levels(self):
        """BFS recurses into children of *unmatched* prims and globally groups
        structurally-identical prims found at the deeper level — not per-parent.

        Two top-level Cabinets have different subtree shapes so neither matches
        at level 1; their children become level 2. At level 2 the four Screws
        are all (Xform > Mesh) so they form ONE group of size 4 with a single
        prototype, not two per-parent pairs.
        """
        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)
        # Give the cabinets different shapes (different child counts) so they
        # don't match at level 1 — we want the BFS to reach the screws.
        for parent_name, extra_children in (("Cabinet_A", 1), ("Cabinet_B", 2)):
            stage.DefinePrim(f"/World/{parent_name}", "Xform")
            for screw_name in ("Screw_1", "Screw_2"):
                stage.DefinePrim(f"/World/{parent_name}/{screw_name}", "Xform")
                stage.DefinePrim(f"/World/{parent_name}/{screw_name}/Mesh", "Xform")
            for i in range(extra_children):
                stage.DefinePrim(f"/World/{parent_name}/Filler_{i}", "Xform")

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        # Cabinets themselves are untouched.
        for parent_name in ("Cabinet_A", "Cabinet_B"):
            cab = stage.GetPrimAtPath(f"/World/{parent_name}")
            self.assertFalse(cab.IsInstanceable())
            self.assertFalse(cab.HasAuthoredReferences())

        # Exactly one Screw is the prototype (the first encountered at level 2)
        # and the other three are instanceable refs pointing at it.
        screw_paths = [
            "/World/Cabinet_A/Screw_1",
            "/World/Cabinet_A/Screw_2",
            "/World/Cabinet_B/Screw_1",
            "/World/Cabinet_B/Screw_2",
        ]
        prototype = None
        duplicates = []
        for path in screw_paths:
            prim = stage.GetPrimAtPath(path)
            if prim.IsInstanceable():
                self.assertTrue(prim.HasAuthoredReferences(), f"{path} instanceable but no ref authored")
                duplicates.append(path)
            else:
                self.assertIsNone(
                    prototype,
                    f"more than one non-instanceable Screw "
                    f"({prototype} and {path}); group should have a single prototype",
                )
                prototype = path
        self.assertIsNotNone(prototype, "no prototype Screw found")
        self.assertEqual(
            len(duplicates), 3, f"expected 3 duplicates referencing the prototype, found {len(duplicates)}"
        )
        # All duplicates point at the same prototype.
        for dup_path in duplicates:
            arcs = Usd.PrimCompositionQuery.GetDirectReferences(stage.GetPrimAtPath(dup_path)).GetCompositionArcs()
            targets = [str(a.GetTargetPrimPath()) for a in arcs if a.GetArcType() == Pcp.ArcTypeReference]
            self.assertIn(
                prototype, targets, f"{dup_path} does not reference the prototype {prototype}; targets={targets}"
            )

    async def test_invalid_paths_arg_is_skipped(self):
        """Non-existent or non-absolute entries in `paths` are warned about and skipped;
        the rest of the operation still runs on the valid ones."""
        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)
        stage.DefinePrim("/World/Bucket", "Xform")
        for name in ("Tree_X", "Tree_Y"):
            t = stage.DefinePrim(f"/World/Bucket/{name}", "Xform")
            t.SetDisplayName("Tree")
            stage.DefinePrim(f"/World/Bucket/{name}/Mesh", "Xform")

        context = _get_context(stage, verbose=False)
        # Mix of: non-existent absolute path, relative path (invalid), and a valid one.
        ok, _ = self._execute_command(
            {"paths": ["/Nonexistent", "Bucket", "/World/Bucket"]},
            context,
        )
        self.assertTrue(ok)

        # Valid path's content was processed.
        dup = stage.GetPrimAtPath("/World/Bucket/Tree_Y")
        self.assertTrue(dup.IsInstanceable())

    async def test_structural_hash_matches_same_shape_different_displayname(self):
        """Structurally identical subtrees with different displayNames are
        deduplicated — matching is by subtree shape, not by displayName."""
        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)
        # Two structurally identical Xforms with different displayNames and
        # different prim names.
        for name, dn in (("Bracket_A", "BoltAssembly"), ("Bracket_B", "ScrewAssembly")):
            parent = stage.DefinePrim(f"/World/{name}", "Xform")
            parent.SetDisplayName(dn)
            stage.DefinePrim(f"/World/{name}/Mesh", "Xform")
            stage.DefinePrim(f"/World/{name}/Mesh/Sub", "Xform")

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        # Bracket_B should now be a ref to Bracket_A.
        a = stage.GetPrimAtPath("/World/Bracket_A")
        b = stage.GetPrimAtPath("/World/Bracket_B")
        self.assertFalse(a.IsInstanceable(), "Bracket_A should remain the prototype")
        self.assertTrue(b.IsInstanceable(), "Bracket_B should be folded as a duplicate by structural hash")
        self.assertTrue(b.HasAuthoredReferences())

    async def test_structural_hash_skips_same_displayname_different_shape(self):
        """Two prims share displayName but have DIFFERENT subtree shapes:
        structural-hash matching correctly leaves them alone."""
        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        # Two siblings sharing displayName='Widget' but with different subtree
        # shapes (one has an extra child).
        a = stage.DefinePrim("/World/Widget_A", "Xform")
        a.SetDisplayName("Widget")
        stage.DefinePrim("/World/Widget_A/Mesh", "Xform")

        b = stage.DefinePrim("/World/Widget_B", "Xform")
        b.SetDisplayName("Widget")
        stage.DefinePrim("/World/Widget_B/Mesh", "Xform")
        stage.DefinePrim("/World/Widget_B/ExtraChild", "Xform")  # extra structural element

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        for path in ("/World/Widget_A", "/World/Widget_B"):
            prim = stage.GetPrimAtPath(path)
            self.assertFalse(
                prim.IsInstanceable(),
                f"{path} should not be deduped: shapes differ even though displayName matches",
            )
            self.assertFalse(prim.HasAuthoredReferences())

    async def test_same_structure_different_values_not_deduped(self):
        """Two subtrees with identical structure but different attribute values
        (e.g. different mesh points) must NOT be deduplicated."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        # Two Xforms with the same subtree structure: each has one Mesh child.
        for name, pts in (
            ("Chair_A", [(0, 0, 0), (1, 0, 0), (0, 1, 0)]),
            ("Chair_B", [(5, 5, 5), (6, 5, 5), (5, 6, 5)]),
        ):
            stage.DefinePrim(f"/World/{name}", "Xform")
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        # Neither prim should be deduplicated — values differ.
        for path in ("/World/Chair_A", "/World/Chair_B"):
            prim = stage.GetPrimAtPath(path)
            self.assertFalse(prim.IsInstanceable(), f"{path} should NOT be deduped: mesh data differs")
            self.assertFalse(prim.HasAuthoredReferences())

    async def test_same_structure_same_values_are_deduped(self):
        """Two subtrees with identical structure AND identical property values
        must be deduplicated (confirms value comparison passes for true dupes)."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        pts = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        for name in ("Chair_A", "Chair_B"):
            stage.DefinePrim(f"/World/{name}", "Xform")
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        # Chair_B should now reference Chair_A.
        a = stage.GetPrimAtPath("/World/Chair_A")
        b = stage.GetPrimAtPath("/World/Chair_B")
        self.assertFalse(a.IsInstanceable())
        self.assertTrue(b.IsInstanceable(), "Chair_B should be deduped — values are identical")
        self.assertTrue(b.HasAuthoredReferences())

    async def test_different_xform_same_mesh_still_deduped(self):
        """Prims with different xformOp values but identical mesh data must still
        be deduplicated — transform differences are expected for instances."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        pts = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        for name, translate in (("Inst_A", (0, 0, 0)), ("Inst_B", (10, 0, 0))):
            parent = stage.DefinePrim(f"/World/{name}", "Xform")
            xformable = UsdGeom.Xformable(parent)
            xformable.AddTranslateOp().Set(Gf.Vec3d(*translate))
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)

        a = stage.GetPrimAtPath("/World/Inst_A")
        b = stage.GetPrimAtPath("/World/Inst_B")
        self.assertFalse(a.IsInstanceable())
        self.assertTrue(b.IsInstanceable(), "Inst_B should be deduped — only xform differs")
        self.assertTrue(b.HasAuthoredReferences())

    async def test_tolerance_allows_small_vertex_drift(self):
        """With tolerance > 0, subtrees whose float values differ within tolerance
        are still deduplicated."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        # Two meshes: identical topology, points differ by 0.0005 per component.
        pts_a = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        pts_b = [(0.0005, 0.0005, 0.0005), (1.0005, 0.0005, 0.0005), (0.0005, 1.0005, 0.0005)]
        for name, pts in (("A", pts_a), ("B", pts_b)):
            stage.DefinePrim(f"/World/{name}", "Xform")
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        # Without tolerance (default=0): should NOT dedup.
        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 0.0}, context)
        self.assertTrue(ok)
        self.assertFalse(stage.GetPrimAtPath("/World/B").IsInstanceable())

        # Re-open a fresh stage (previous run may have mutated nothing, but be safe).
        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)
        for name, pts in (("A", pts_a), ("B", pts_b)):
            stage.DefinePrim(f"/World/{name}", "Xform")
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        # With tolerance=0.001: should dedup (drift is 0.0005 < 0.001).
        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 0.001}, context)
        self.assertTrue(ok)
        self.assertTrue(
            stage.GetPrimAtPath("/World/B").IsInstanceable(),
            "B should be deduped: vertex drift is within tolerance",
        )

    async def test_tolerance_does_not_affect_topology(self):
        """Tolerance only applies to float values. Different topology indices
        (integer arrays) must still prevent deduplication even with high tolerance."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        pts = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]
        for name, indices in (("A", [0, 1, 2]), ("B", [0, 1, 3])):
            stage.DefinePrim(f"/World/{name}", "Xform")
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray(indices))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 100.0}, context)
        self.assertTrue(ok)
        self.assertFalse(
            stage.GetPrimAtPath("/World/B").IsInstanceable(),
            "B should NOT be deduped: topology differs (integer array, not affected by tolerance)",
        )

    async def test_multi_mesh_hierarchy_tolerance_accepts_small_drift(self):
        """A hierarchy with multiple meshes where each mesh has small vertex
        drift (within tolerance) across the two copies should be deduplicated."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        base_pts_body = [(0, 0, 0), (2, 0, 0), (1, 2, 0), (0, 0, 1), (2, 0, 1), (1, 2, 1)]
        base_pts_wheel = [(0, 0, 0), (0.5, 0, 0), (0, 0.5, 0)]
        drift = 0.0003

        for idx, name in enumerate(("Car_A", "Car_B")):
            stage.DefinePrim(f"/World/{name}", "Xform")
            body = UsdGeom.Mesh.Define(stage, f"/World/{name}/Body")
            body_pts = [Gf.Vec3f(p[0] + drift * idx, p[1] + drift * idx, p[2] + drift * idx) for p in base_pts_body]
            body.GetPointsAttr().Set(Vt.Vec3fArray(body_pts))
            body.GetFaceVertexCountsAttr().Set(Vt.IntArray([3, 3, 3, 3]))
            body.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2, 3, 4, 5, 0, 3, 5, 1, 4, 2]))

            wheel = UsdGeom.Mesh.Define(stage, f"/World/{name}/Wheel")
            wheel_pts = [Gf.Vec3f(p[0] + drift * idx, p[1] + drift * idx, p[2] + drift * idx) for p in base_pts_wheel]
            wheel.GetPointsAttr().Set(Vt.Vec3fArray(wheel_pts))
            wheel.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            wheel.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 0.001}, context)
        self.assertTrue(ok)
        self.assertTrue(
            stage.GetPrimAtPath("/World/Car_B").IsInstanceable(),
            "Car_B should be deduped: all mesh drift (0.0003) is within tolerance (0.001)",
        )

    async def test_multi_mesh_hierarchy_tolerance_rejects_large_drift(self):
        """When one mesh in a hierarchy drifts beyond tolerance the whole
        hierarchy must NOT be deduplicated, even if other meshes are fine."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        base_pts_body = [(0, 0, 0), (2, 0, 0), (1, 2, 0)]
        base_pts_wheel = [(0, 0, 0), (0.5, 0, 0), (0, 0.5, 0)]

        for idx, name in enumerate(("Car_A", "Car_B")):
            stage.DefinePrim(f"/World/{name}", "Xform")

            body = UsdGeom.Mesh.Define(stage, f"/World/{name}/Body")
            body_pts = [Gf.Vec3f(*p) for p in base_pts_body]
            body.GetPointsAttr().Set(Vt.Vec3fArray(body_pts))
            body.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            body.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

            wheel = UsdGeom.Mesh.Define(stage, f"/World/{name}/Wheel")
            large_drift = 0.01 * idx
            wheel_pts = [Gf.Vec3f(p[0] + large_drift, p[1] + large_drift, p[2] + large_drift) for p in base_pts_wheel]
            wheel.GetPointsAttr().Set(Vt.Vec3fArray(wheel_pts))
            wheel.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            wheel.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 0.001}, context)
        self.assertTrue(ok)
        self.assertFalse(
            stage.GetPrimAtPath("/World/Car_B").IsInstanceable(),
            "Car_B should NOT be deduped: Wheel drift (0.01) exceeds tolerance (0.001)",
        )

    async def test_descendant_transform_differs_blocks_dedup(self):
        """Two hierarchies that are identical except for a child-level
        transform must NOT be deduplicated — only root xformOps are ignored."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        pts = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        for name, child_translate in (("A", (0, 0, 0)), ("B", (0, 5, 0))):
            stage.DefinePrim(f"/World/{name}", "Xform")
            child = stage.DefinePrim(f"/World/{name}/Part", "Xform")
            xformable = UsdGeom.Xformable(child)
            xformable.AddTranslateOp().Set(Gf.Vec3d(*child_translate))
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Part/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 100.0}, context)
        self.assertTrue(ok)
        self.assertFalse(
            stage.GetPrimAtPath("/World/B").IsInstanceable(),
            "B should NOT be deduped: descendant Part has a different transform",
        )

    async def test_root_xform_differs_descendant_xform_matches_deduped(self):
        """Root prims have different placement transforms, but descendants
        have identical local transforms — should still deduplicate."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        pts = [(0, 0, 0), (1, 0, 0), (0, 1, 0)]
        child_offset = (0, 2, 0)
        for name, root_translate in (("A", (0, 0, 0)), ("B", (50, 0, 0))):
            parent = stage.DefinePrim(f"/World/{name}", "Xform")
            UsdGeom.Xformable(parent).AddTranslateOp().Set(Gf.Vec3d(*root_translate))
            child = stage.DefinePrim(f"/World/{name}/Part", "Xform")
            UsdGeom.Xformable(child).AddTranslateOp().Set(Gf.Vec3d(*child_offset))
            mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/Part/Mesh")
            mesh.GetPointsAttr().Set(Vt.Vec3fArray([Gf.Vec3f(*p) for p in pts]))
            mesh.GetFaceVertexCountsAttr().Set(Vt.IntArray([3]))
            mesh.GetFaceVertexIndicesAttr().Set(Vt.IntArray([0, 1, 2]))

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({}, context)
        self.assertTrue(ok)
        self.assertTrue(
            stage.GetPrimAtPath("/World/B").IsInstanceable(),
            "B should be deduped: root xforms differ (ignored) but descendant xforms match",
        )

    async def test_deep_hierarchy_mixed_drift_within_tolerance(self):
        """A three-level hierarchy (root > group > mesh) where meshes at
        different depths each have small independent drift. All drift is
        within tolerance, so dedup should succeed."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        base_pts = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]
        topo_counts = Vt.IntArray([3, 3])
        topo_indices = Vt.IntArray([0, 1, 2, 1, 3, 2])

        for idx, name in enumerate(("Tree_A", "Tree_B")):
            stage.DefinePrim(f"/World/{name}", "Xform")
            for gi, grp in enumerate(("Trunk", "Canopy")):
                stage.DefinePrim(f"/World/{name}/{grp}", "Xform")
                for mi, mname in enumerate(("Mesh0", "Mesh1")):
                    mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/{grp}/{mname}")
                    d = 0.0002 * idx * (gi + 1) * (mi + 1)
                    pts = Vt.Vec3fArray([Gf.Vec3f(p[0] + d, p[1] + d, p[2] + d) for p in base_pts])
                    mesh.GetPointsAttr().Set(pts)
                    mesh.GetFaceVertexCountsAttr().Set(topo_counts)
                    mesh.GetFaceVertexIndicesAttr().Set(topo_indices)

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 0.001}, context)
        self.assertTrue(ok)
        self.assertTrue(
            stage.GetPrimAtPath("/World/Tree_B").IsInstanceable(),
            "Tree_B should be deduped: max drift (0.0008) is within tolerance (0.001)",
        )

    async def test_deep_hierarchy_one_mesh_exceeds_tolerance(self):
        """Same as above, but one leaf mesh has drift exceeding tolerance.
        The entire hierarchy must be rejected."""
        from pxr import Gf, UsdGeom, Vt

        stage = Usd.Stage.CreateInMemory()
        world = stage.DefinePrim("/World", "Xform")
        stage.SetDefaultPrim(world)

        base_pts = [(0, 0, 0), (1, 0, 0), (0, 1, 0), (1, 1, 0)]
        topo_counts = Vt.IntArray([3, 3])
        topo_indices = Vt.IntArray([0, 1, 2, 1, 3, 2])

        for idx, name in enumerate(("Tree_A", "Tree_B")):
            stage.DefinePrim(f"/World/{name}", "Xform")
            for gi, grp in enumerate(("Trunk", "Canopy")):
                stage.DefinePrim(f"/World/{name}/{grp}", "Xform")
                for mi, mname in enumerate(("Mesh0", "Mesh1")):
                    mesh = UsdGeom.Mesh.Define(stage, f"/World/{name}/{grp}/{mname}")
                    d = 0.0002 * idx * (gi + 1) * (mi + 1)
                    if name == "Tree_B" and grp == "Canopy" and mname == "Mesh1":
                        d = 0.05
                    pts = Vt.Vec3fArray([Gf.Vec3f(p[0] + d, p[1] + d, p[2] + d) for p in base_pts])
                    mesh.GetPointsAttr().Set(pts)
                    mesh.GetFaceVertexCountsAttr().Set(topo_counts)
                    mesh.GetFaceVertexIndicesAttr().Set(topo_indices)

        context = _get_context(stage, verbose=False)
        ok, _ = self._execute_command({"tolerance": 0.001}, context)
        self.assertTrue(ok)
        self.assertFalse(
            stage.GetPrimAtPath("/World/Tree_B").IsInstanceable(),
            "Tree_B should NOT be deduped: Canopy/Mesh1 drift (0.05) exceeds tolerance (0.001)",
        )

    async def test_analysis_mode_returns_duplicates_without_mutating(self):
        """Analysis mode finds duplicates and returns JSON but does NOT author
        references or set instanceable on any prim."""
        stage = self._open_stage("dedupHierarchies_basic.usda")

        context = _get_context(stage, verbose=False)
        context.analysisMode = 1
        ok, result = self._execute_command({}, context)
        self.assertTrue(ok)

        # Stage must be untouched.
        for path in ("/World/Tree_Copy1", "/World/Tree_Copy2"):
            prim = stage.GetPrimAtPath(path)
            self.assertFalse(prim.IsInstanceable(), f"{path} mutated in analysis mode")
            self.assertFalse(prim.HasAuthoredReferences())

        # result[2] is the parsed output dict.
        self.assertIn("analysis", result[2])
        analysis = result[2]["analysis"]
        self.assertIn("/World/Tree_Original", analysis)
        self.assertEqual(sorted(analysis["/World/Tree_Original"]), ["/World/Tree_Copy1", "/World/Tree_Copy2"])
