# Changelog

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
