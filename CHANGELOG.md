# Changelog

## [1.0.2] - 2026-05-27
### Fixed
- Removed `repo_kit_tools` from public facing dependencies

## [1.0.1] - 2026-05-26
### Fixed
- `DeduplicateGeometry`: preserve `MaterialBindingAPI` schemas on instance xforms so material bindings survive deduplication (OMPE-94963).
- `DeduplicateGeometry`: correct transform/pivot handling, including flipped duplicates (OMPE-94933).
- `CMakeLists.txt` corrections for consumer-side builds.
- `usd-deps` generation no longer leaves the working tree dirty in git.

### Added
- Test runner supports running individual Python tests; documented in the `testing` skill.

### Removed
- Unused `tests/` directory and `test_skill_docs.py`.

## [1.0.0] - 2026-05-22
### Changed
- Initial version.
