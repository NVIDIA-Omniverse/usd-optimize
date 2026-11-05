# Changelog

## [1.4.0] - 2026-11-05
### Added
- `docs/gpu-acceleration.rst`: which operations and validator rules to run on the GPU, and the
  caveats.
- `conformMeshes` operation: run before `merge`, it makes meshes that `merge` would group together
  compatible, so `merge` combines them. It generates UVs that some meshes lack, and normals unless the meshes are
  subdivision surfaces, renames `st`/`st0` UV sets to a common name, adds primvars that only some meshes have (with a
  caller-supplied or neutral default), and sets `doubleSided` to true where it differs. It groups meshes as `merge`
  does, so a mesh is never changed to match one `merge` keeps apart for another reason, and it logs every change and
  every difference it cannot reconcile.
- `merge`: logs where it left meshes unmerged and why, such as a time-sampled transform, an
  invisible parent or an instance. Meshes skipped together are logged once under their common
  prim. These lines go to the report and, with the verbose flag, the console; one INFO line says
  how many there are.
- `mergePhysicsBodyMeshes` operation: merges compatible visual meshes within each rigid body,
  keeping body, joint and articulation frames. Collision shapes are kept; `mergeColliders` merges compatible
  mesh colliders and combines their explicit mass properties. Instances are left unchanged unless `instanceMode`
  makes selected ones local. It can generate missing normals and UVs, fill missing primvars and reconcile
  `doubleSided` first, and it reports each mesh it leaves unmerged and why.
- `primitivesToMeshes` converts capsules as well, unless the new `convertCapsules` argument is off;
  `capsuleRadialDivisions`, `capsuleLengthDivisions` and `capsuleCapDivisions` set their mesh detail.

### Changed
- Linux release packages: `libmesh_tools_lib.so` is stripped, saving 27 MB.
- Release packages: the bundled Python library (`python312.dll`, `libpython3.12.so.1.0`) moves from 3.12.12 to
  3.12.16 for its security fixes.
- `decimateMeshes` documentation: new "Output mesh structure" section covering the steps every
  targeted mesh goes through and their effect on vertex and face counts.
- `findOccludedMeshes`: when the GPU check fails, for example out of GPU memory or on a GPU its
  kernels were not built for, it reruns on the CPU instead of failing.
- Performance validators: a rule's own parameter defaults now apply over usd-validation-nvidia's
  requirement defaults (`--parameter` still overrides both). `OccludedMeshesChecker` therefore runs on
  the GPU by default and uses `CHECK_TRANSPARENCY=True` as documented, so meshes behind transparent
  geometry are no longer reported as occluded.
- `shrinkwrap`: `Temporal Combined` uses the stage's time range unless the new `useStageTimeRange` is off, replacing
  the NaN time defaults that hid Shrinkwrap's arguments in UIs. Configs that set `startTime` or `endTime` must turn
  it off and give both.
- `printStats` collects stats at `UsdTimeCode::Default` unless the new `useDefaultTime` is off, replacing the
  NaN `time` default. Configs that set `time` must turn it off.
- Move `omnimesh_ops_usd` to `1.2.0-r1` for USD 26.08 and 25.11.

### Removed
- USD 25.05 builds and drops, and the `TbbCompat.h` header that existed only for them. Build against
  USD 25.11 or 26.08.

### Fixed
- Reports written without a `reportPath` go to a new temporary file created by `ArchMakeTmpFile`.
- Mesh merging transforms `point3f[]`, `vector3f[]` and `normal3f[]` primvars into the merged frame, not just `normals`.
- `optimizeSkelRoots`: skinned meshes with material GeomSubsets are now left unmerged,
  with a warning. Merging split them by material, which dropped faces and wrote
  joint influence arrays that OpenUSD cannot skin, while still reporting success.
- `merge` no longer drops faces when several of a mesh's material subsets, or a subset and the rest of the mesh,
  share a material.
- `merge` and `split`: skip prims with `PhysicsRigidBodyAPI` or `PhysicsArticulationRootAPI`
  and their descendants during mesh discovery. Explicitly selecting a descendant allows processing
  below that point; nested physics body roots are still skipped.
- `decimateMeshes` documentation: two "Choosing a stop condition" headings showed literal double
  backticks, because RST does not render literals inside bold text. Reworded so the literals sit outside
  the bold.
- `shrinkwrap`: `Per Mesh` outputs now copy their source's xform ops, pivot included.
  They were baked into the parent's space with no ops, which left the pivot at the parent's origin.
  Under a zero-scale parent the output now resets the xform stack instead of getting infinite points.
- `merge`: "Merged N meshes" counts only the meshes that were merged, not every mesh considered.
- `optimizeMaterials` deduplication rebinds purpose-specific bindings, such as `material:binding:physics`,
  and collection bindings to the material it keeps. It rebound only the all-purpose binding, so the others
  still targeted the duplicate it removed, and physics materials were silently lost.
- `optimizeMaterials` Convert to color, Remove unbound and Deduplicate with primvars no longer delete a material
  that a purpose-specific binding, such as `material:binding:physics`, or a collection binding still targets. The
  binding was left dangling, and Remove unbound deleted every material that was bound only for physics.
- `primitivesToMeshes` and `fitPrimitives` cap their division arguments at 1000.
- `primitivesToMeshes` converts primitives defined in a sublayer. They kept their Cube, Sphere, Cylinder or Cone
  type, although the log reported them as replaced.
- `primitivesToMeshes` no longer leaves the meshes it creates without an extent. It blocked the
  extent, which also hid the prototype's, so every converted primitive added one `ExtentsChecker`
  failure in `usd-validation-nvidia`.
- `generateNormals` and `computeExtents` process meshes below an `over` or `class` prim, such as the prototypes
  instances reference, instead of skipping them. A mesh that is itself an `over` is still skipped.
- `pruneLeaves` keeps empty `Xform` and `Scope` prims that a relationship targets, such as an OmniGraph node's
  `inputs:targetPrims`, along with any leaf that contains one. Deleting them left the relationship dangling.
  Turning off the new `preserveRelationshipTargets` argument prunes them anyway.
- `generateAtlasUVs` skips meshes with NaN or infinite points, which crashed or hung it. USD 25.11 builds
  now use the autouv-core that 26.08 builds already had, so Windows debug builds no longer abort in it.
- `merge`: selecting an instance whose reference targets a prim in another file no longer merges an unrelated
  prim at the same path on the current stage.
- `merge`: merging the whole stage no longer copies a prototype's meshes once per instance into its merged
  mesh, when the instances use `add references`.
- `merge`: a point instancer's prototype outside the instancer is merged once, inside the prototype, and a prototype
  that is a single mesh stays where it is. It was merged twice, or into the stage's merged mesh, which left the
  instancer with nothing to draw.
- `merge` and `conformMeshes` no longer crash at the Original Prim merge point when a point instancer's
  prototype is a mesh.
- `merge`: normals of meshes with a non-uniform scale or shear are transformed correctly into the merged mesh.
  Only the rotation was applied to them, so they were no longer perpendicular to the surface.
- Prebuilt drops: the install guides now list the third-party headers that the C++ headers need
  (OpenUSD, Carbonite, CPython, TBB and pybind11), and `JsonUtils.h` includes the OpenUSD
  header that declares `JsWriteToString`, so it also compiles on its own under GCC.
- Docs: Linux needs glibc 2.35 or newer, and building the Linux wheel needs a glibc 2.35 host.
- Operations given explicit prim paths containing non-ASCII characters, such as `/World/Défaut`, no
  longer silently do nothing: literal prim paths are no longer run through `SdfPathExpression`'s
  ASCII-only text grammar, so they resolve the same as ASCII ones.
- Operations fail without running when a path argument, such as `paths`, is not a valid path or path expression,
  instead of reporting success.
- `usdOptimize -a`: bool arguments take `true` and `false` in any case, as well as `1` and `0`, and reject
  any other value. `true` and `false` used to fail with "failed to set argument: stoi".
- `THIRD_PARTY_NOTICES.md` lists the third-party code the drops ship inside other packages: the CPython
  runtime library, with bzip2, liblzma and zlib built into the Linux one and HACL* into the Windows one,
  and in the 25.11 drops OpenUSD's Alembic, Imath, OpenSubdiv and zlib and oneTBB's hwloc. It said CPython
  is not redistributed.
- `editStageMetrics` no longer crashes or fails when changing the up axis of a stage with a blocked attribute value,
  such as `normals = None`.

## [1.3.1] - 2026-10-07
### Changed
- Move `omnimesh_ops_usd` to `1.1.0-r7` for USD 26.08 and 25.11, shrinking the drops and the
  wheel: it embeds no sm_86 code (sm_86 GPUs run the sm_80 code) and PTX for sm_90 only.
  25.05 stays on `1.1.0-r4`.
- The Linux wheel no longer bundles OpenUSD or oneTBB; it uses usd-exchange's copies, which it
  links by their original library names, and requires `usd-exchange>=3.0.1,<4`.

### Fixed
- Prebuilt packages and source builds: stage OpenUSD's `usdShaders` plugin, which defines
  `UsdPreviewSurface` to Sdr. Without it `UsdShadeShaderSdrCompliance` failed every
  `UsdPreviewSurface` shader and `MaterialUsdPreviewSurfaceChecker` could check none, so real
  shader errors were indistinguishable from the false ones.
- The Linux wheel no longer loads a second OpenUSD next to usd-exchange's, which left
  operations on the caller's stage failing with "Stage id ... not found in UsdUtilsStageCache".

## [1.3.0] - 2026-09-30
### Added
- OpenUSD 26.08 as a build target on windows-x86_64, linux-x86_64 and linux-aarch64,
  alongside 25.05 and 25.11.
- `usd_optimize`: an import-time `ImportError` when the extension module was built for a
  different Python than the running interpreter, instead of a misleading `ModuleNotFoundError`.
- Versioned documentation on GitHub Pages, with a populated version dropdown and a `latest/`
  that follows the newest release.
- `docs/choosing-operations.rst`: an "Improving Load Time" section.

### Changed
- **Breaking (PyPI wheel only):** the wheel now targets OpenUSD 26.08 and requires
  `usd-exchange>=3.0.0,<4`; pin `usd-optimize==1.2.1` to stay on 25.05.
- OpenUSD 26.08 is now the repository default build version; 25.05 and 25.11 stay selectable
  with `--usd-ver`.
- Raise the `usd-validation-nvidia` floor to `1.22.0`, bounded below 2, in the wheel and the
  `run-validators` wrappers.
- `PrimitiveFitChecker`: drop the fix suggestion for meshes with non-constant primvars, so
  `--fix` cannot discard them unattended.
- `ColocatedVerticesChecker`: its `meshCleanup` fix now also removes the degenerate faces that
  welding creates.
- `run-validators`: `--fix` fails closed when the output would be clobbered or cannot be saved.
- `THIRD_PARTY_NOTICES.md`: attribute MaterialX and oneTBB, which OpenUSD 26.08 ships as
  separate packages.
- `meshCleanup`: document that the passes run in a fixed order and are not independent.
- `deduplicateGeometry`: correct what `considerDeepTransforms` compares, and scope
  `ignoreAttributes` to the reference and point-instancer methods.
- `shrinkwrap`: document that `adaptivity` trades containment for triangle count in
  `Temporal Combined` mode.
- Correct the prebuilt-drop and C++ docs to match the shipped artifact.

### Fixed
- `pruneLeaves`: undefined prims (pure `over`s) no longer count as content, so a grouping prim
  is pruned even when a removed reference target left dead overs behind. Inactive prims are
  unaffected and stay controlled by `filterInactive`.
- `optimizeMaterials`: no longer deduplicates materials that reference different files when
  neither composes any shaders, e.g. MaterialX without its standard library installed.
- `run-validators`: a `--fix` run that fails before saving no longer leaves the unfixed copy
  behind, which read as a successful fix.
- `run-validators`: a failed `usd-validation-nvidia` install exits 2 (nothing validated)
  instead of 1 (issues found).
- PyPI wheel (Linux): share usd-exchange's oneTBB instead of loading a second copy that ignored
  the host's `Work.SetConcurrencyLimit`.
- Agent skills: correct claims the shipped binary contradicts and recipes that gave wrong
  results when followed literally.

## [1.2.1] - 2026-09-04
### Added
- `shrinkwrap`: `Temporal Combined` mode — sweep meshes over a time range into one static
  envelope mesh (`startTime`, `endTime`, `timeStep`, `connectTimeSamples`).
- `editStageMetrics`: `stopAtSkeletonRoot` — correct a skeleton at its `SkelRoot` rather
  than at every prim beneath it.
- Performance validators: tunable parameters on `CoincidingGeometryChecker`,
  `ColocatedVerticesChecker`, `FlatHierarchiesChecker`, `HighVertexCountChecker`,
  `PrimitiveFitChecker`, `RedundantTimeSamplesChecker`, and `RtxMeshCountChecker`.
- Performance validators: mark the rule family `@multiprocess_safe`, so
  `usd-validation-nvidia --process COUNT` can pool it.
- `summarize_csv.py`: accept a `--json-output` report as well as a CSV.
- GitHub Pages workflows for the rendered docs, plus a CI doc-link checker.

### Changed
- **Breaking:** `run-validators`: `--fix` now writes to `<stem>.fixed<ext>` (or
  `--fix-output <path>`) and leaves the source untouched. A caller that runs `--fix` and
  then re-reads the source path gets unfixed data, with no warning and exit 0; pass
  `--fix-in-place` to restore the previous in-place behaviour.
- `run-validators`: the driver exits 2 on fix-flag misuse — `--fix-output` without a path or
  without `--fix`, `--fix-in-place` without `--fix`, the two combined, zero or several
  candidate assets to protect, or `--fix-output` resolving to the source.
- Performance validators: `WindingsChecker`, `ColocatedVerticesChecker`, and
  `ZeroAreaFacesChecker` declare explicit `meshCleanup` arguments instead of inheriting
  its six enabled defect categories.
- `tools/validators/run.{sh,bat}`: stop stripping `-v` / `--verbose`, which is upstream's
  own flag.
- Package `config_presets/` in the shipped drop; the docs and skills tell readers to run
  `usdOptimize -c config_presets/<name>.json`, but the presets shipped in no package.
- Pin the wheel's `usd-exchange` to `>=2.2,<3`; unpinned, it resolved 3.0.0 (OpenUSD 26.08)
  against a wheel built for 25.05.
- Raise the `usd-validation-nvidia` floor to `1.21.0` (was `1.19.3`).
- Bump `poetry` to 2.3.4 and `poetry-core` to 2.3.2 for CVE fixes.
- `merge` / `splitMeshes`: document the `mergePoint` values.
- Shorten the README, moving runbook material into `docs/`.

### Fixed
- CUDA detection on WSL: load `libcuda.so` instead of pre-checking sysfs for a PCI display
  adapter, which WSL does not expose. GPU work no longer falls back to CPU.
- Windows packages: stage the MaterialX runtime DLLs into `extraLibs`, without which any
  shader lookup failed to load.
- `usdOptimize` CLI: run shutdown callbacks on every exit, removing the
  `CUDA error: driver shutting down` abort on GPU runs.
- `usdOptimize` CLI: exit 0 from an explicit `-h` / `--help`, and stop consuming a
  flag-shaped final argument as the input path.
- `summarize_csv.py`: raise csv's field-size limit so very large fields still parse.
- Agent skills: correct validator exit codes, the `--fix` summary and folder restriction,
  an unreachable artifact-replay path, and the unknown-argument policy.
- Agent skills: move `version` under `metadata` and drop angle brackets from `description`,
  which fail frontmatter validation.
- Agent skills and guides: the `new-operation` template returned `OperationResult::eSuccess`,
  which has never existed, so the documented scaffold could not compile. Also corrected the
  validator-class naming row, `debug-operation`'s `-r`, `repo.bat ci format` (CI-only), the
  Windows `usdOptimize.bat` launcher, a stale path and a wrong "no `--fix` mode" claim in
  `interpret-validators`, and stale `PLUGINS.md` / `AGENTS.md` paths.
- `config_presets/safe-cleanup.json` and `memory-reduction.json` passed
  `considerInstanceability` to `deduplicateGeometry`, which declares no such argument.

## [1.1.2] - 2026-08-03
### Added
- `moveMaterials`: restore the operation dropped in the Kit Scene Optimizer migration,
  reimplemented Kit-free with `Sdf` namespace edits. Fixes the three presets that silently
  skipped it. Excluded roots now match exact prim names; the root prim is no longer retyped.

### Changed
- Bump `mesh_tools_lib` to `1.0.2.518900cd`.
- `findOccludedMeshes`: drop client-side zero-extent filtering and VisChecker exception
  handling now that mesh_tools handles degenerate input internally.

## [1.1.1] - 2026-07-13
### Changed
- `THIRD_PARTY_NOTICES.md`: align entries with the packman dependency pins
  (`omnimesh_ops_usd`, `mesh_tools_lib`, autouv-core, shrinkwrap_openvdb) and
  note the USD-version-specific pins.
- Ship `LICENSE` and `THIRD_PARTY_NOTICES.md` under `PACKAGE-LICENSES/` in
  published packages.

## [1.1.0] - 2026-07-13
### Fixed
- `generateScene`: skip empty clusters to avoid writing malformed prims.
- `meshCleanup`: skip zero-extent meshes during analysis to avoid a crash.

## [1.0.8] - 2026-07-10
### Added
- Standalone Python API docs for `UsdOptimizeCore`.
- Verbose per-prim reporting in performance validators — richer validator output for ops like `fitPrimitives`, `generateNormals`, `meshCleanup`, etc.

### Changed
- CI: do not gate package publish on `test_windows_1x_gpu` (backlogged Windows GPU runners).
- `flattenHierarchy`: preserve sole root prim / `defaultPrim` when flattening.

### Fixed
- `findOccludedMeshes`: skip zero-extent meshes to avoid CPU clustered-path crash.

## [1.0.4] - 2026-06-12
### Added
- Asset Validator parameters API: expose validator rule tuning so checker thresholds (e.g. `OccludedMeshesChecker`, `SmallMeshChecker`) can be configured via parameters.
- `MergeMeshes`: new spatial merge mode that welds coincident boundary vertices across seams.
- `DeduplicateGeometry`: new "Point Instancer" mode.
- `PrunePayloads`: option to avoid pruning unloaded payloads.
- `DeduplicateHierarchies`: support for nested instancing.

### Changed
- Renamed Scene Optimizer to Usd Optimize.
- Validators now register against capability requirements (e.g. `GeometryRequirements`, `HierarchyRequirements`, `MaterialsRequirements`) via the new plugin entry point instead of rule categories.
- Migrated the asset validator dependency to `usd-validation-nvidia`.
- Reduced default logging noise across operations.
- Improved performance of `PruneLeaves`.
- Reverted `repo_usd` pin to 5.0.26 (restores the stock build; the 5.0.34 exchange build trimmed link deps).
- Pin Visual Studio to 2019.
- Auto-generated documentation for Usd Optimize lib.
- Replaced unsafe sudo/rm guidance in validators skill.

### Fixed
- `DeduplicateHierarchies`: fix value variant grouping.
- `DiceMeshes`: fix irregular multi-axis cuts.
- Fix gcc13 build issues from stricter compiler checks (DGX Spark defaults to gcc13).

## [1.0.3] - 2026-05-28
### Fixed
- `FitPrimitive`: no longer incorrectly fits a cube primitive to hollow meshes (e.g. an extruded box). Such meshes are now left unchanged instead of being replaced by a solid cube.
- Remove primvar indices when removing primvars.

### Added
- Accept JSON int literals for `float`/`double` attributes.

### Changed
- Bumped `repo_usd` to 5.0.34.
- Use symlinks for Python files where possible during builds rather than copying them.

## [1.0.2] - 2026-05-27
### Fixed
- Removed `repo_kit_tools` from public facing dependencies

## [1.0.1] - 2026-05-26
### Fixed
- `DeduplicateGeometry`: preserve `MaterialBindingAPI` schemas on instance xforms so material bindings survive deduplication.
- `DeduplicateGeometry`: correct transform/pivot handling, including flipped duplicates.
- `CMakeLists.txt` corrections for consumer-side builds.
- `usd-deps` generation no longer leaves the working tree dirty in git.

### Added
- Test runner supports running individual Python tests; documented in the `testing` skill.

### Removed
- Unused `tests/` directory and `test_skill_docs.py`.

## [1.0.0] - 2026-05-22
### Changed
- Initial version.
