# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Performance validators that bridge Scene Optimizer analysis operations to
``omniverse-asset-validator``.

Each rule wraps a Scene Optimizer analysis-mode :class:`Operation`, parses its
JSON findings, and emits :class:`omni.asset_validator.Issue` objects under the
``Performance`` category.

Discovery: when ``omniverse-scene-optimizer`` is pip-installed, this package
publishes :class:`SceneOptimizerValidatorPlugin` via ``importlib.metadata`` in the
``omni.asset_validator`` entry-point group. A source checkout on ``PYTHONPATH``
imports the modules but **does not** expose that entry point. The validator's
``PluginManager`` loads only plugins listed in
``OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS`` among those discovered.
To skip metadata discovery entirely and register in-process,
call :func:`register_all`.
"""

from ._base import clear_analysis_cache
from ._plugin import CATEGORY, SceneOptimizerValidatorPlugin, register_all
from .coinciding_geometry import SceneOptimizerCoincidingGeometryChecker
from .colocated_vertices import SceneOptimizerColocatedVerticesChecker
from .duplicate_faces import SceneOptimizerDuplicateFacesChecker
from .duplicate_geometry import SceneOptimizerDuplicateGeometryChecker
from .duplicate_hierarchies import SceneOptimizerDuplicateHierarchiesChecker
from .duplicate_materials import SceneOptimizerDuplicateMaterialsChecker
from .empty_leaves import SceneOptimizerEmptyLeafChecker
from .find_overlapping_meshes import SceneOptimizerFindOverlappingMeshesChecker
from .flat_hierarchies import SceneOptimizerFlatHierarchiesChecker
from .flatten_hierarchy import SceneOptimizerFlattenHierarchyChecker
from .fuzzy_duplicate_geometry import SceneOptimizerFuzzyDuplicateGeometryChecker
from .indexed_primvars import SceneOptimizerIndexedPrimvarChecker
from .invisible_prims import SceneOptimizerInvisiblePrimsChecker
from .isolated_vertices import SceneOptimizerIsolatedVerticesChecker
from .mesh_density import SceneOptimizerMeshDensityChecker
from .non_manifold import SceneOptimizerNonManifoldChecker
from .normals import SceneOptimizerNormalsChecker
from .occluded_meshes import SceneOptimizerOccludedMeshesChecker
from .primitive_fit import SceneOptimizerPrimitiveFitChecker
from .redundant_time_samples import SceneOptimizerRedundantTimeSamplesChecker
from .rtx_mesh_count import SceneOptimizerRtxMeshCountChecker
from .small_mesh import SceneOptimizerSmallMeshChecker
from .sparse_meshes import SceneOptimizerSparseMeshChecker
from .unused_uvs import SceneOptimizerUnusedUVsChecker
from .windings import SceneOptimizerWindingsChecker
from .zero_area_faces import SceneOptimizerZeroAreaFacesChecker
from .zero_extent import SceneOptimizerZeroExtentChecker

__all__ = [
    "CATEGORY",
    "SceneOptimizerValidatorPlugin",
    "SceneOptimizerCoincidingGeometryChecker",
    "SceneOptimizerColocatedVerticesChecker",
    "SceneOptimizerDuplicateFacesChecker",
    "SceneOptimizerDuplicateGeometryChecker",
    "SceneOptimizerDuplicateHierarchiesChecker",
    "SceneOptimizerDuplicateMaterialsChecker",
    "SceneOptimizerEmptyLeafChecker",
    "SceneOptimizerFindOverlappingMeshesChecker",
    "SceneOptimizerFlatHierarchiesChecker",
    "SceneOptimizerFlattenHierarchyChecker",
    "SceneOptimizerFuzzyDuplicateGeometryChecker",
    "SceneOptimizerIndexedPrimvarChecker",
    "SceneOptimizerInvisiblePrimsChecker",
    "SceneOptimizerIsolatedVerticesChecker",
    "SceneOptimizerMeshDensityChecker",
    "SceneOptimizerNonManifoldChecker",
    "SceneOptimizerNormalsChecker",
    "SceneOptimizerOccludedMeshesChecker",
    "SceneOptimizerPrimitiveFitChecker",
    "SceneOptimizerRedundantTimeSamplesChecker",
    "SceneOptimizerRtxMeshCountChecker",
    "SceneOptimizerSmallMeshChecker",
    "SceneOptimizerSparseMeshChecker",
    "SceneOptimizerUnusedUVsChecker",
    "SceneOptimizerWindingsChecker",
    "SceneOptimizerZeroAreaFacesChecker",
    "SceneOptimizerZeroExtentChecker",
    "clear_analysis_cache",
    "register_all",
]
