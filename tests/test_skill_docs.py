# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

import json
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]


def read_text(path: str) -> str:
    return (REPO_ROOT / path).read_text(encoding="utf-8")


def normalized(path: str) -> str:
    return " ".join(read_text(path).split())


class SkillDocCorrectionTests(unittest.TestCase):
    def test_operation_runner_does_not_assume_helper_wrappers(self):
        doc = read_text(".agents/skills/run-operations/SKILL.md")
        self.assertIn("Optional helper scripts", doc)
        self.assertIn("Do not assume Kit, standalone package, or wheel installs", doc)
        self.assertIn("Confirm operation availability", doc)
        self.assertIn("registered operations", doc)

    def test_validator_runner_documents_standalone_fallback(self):
        doc = read_text(".agents/skills/run-validators/SKILL.md")
        self.assertIn("Standalone-path applicability", doc)
        self.assertIn("standalone Scene Optimizer without the `omni.scene.optimizer.validators`", doc)
        self.assertIn("standalone-analysis-mode", doc)
        self.assertNotIn("Linux / macOS | `tools/perf_validators/run.sh`", doc)

    def test_interpret_validators_handles_large_or_fallback_artifacts(self):
        doc = read_text(".agents/skills/interpret-validators/SKILL.md")
        self.assertIn("standalone-analysis-mode", doc)
        self.assertIn(
            "temporary stdlib-only fallback summarizer",
            normalized(".agents/skills/interpret-validators/SKILL.md"),
        )
        self.assertIn("blocked_large_artifact", doc)
        self.assertIn(
            "Do not paste the full artifact",
            normalized(".agents/skills/interpret-validators/SKILL.md"),
        )

    def test_create_proxy_safety_corrections_are_documented(self):
        doc = read_text(".agents/skills/create-proxy/SKILL.md")
        decimate_ref = read_text(".agents/skills/create-proxy/references/decimate-mode.md")
        tuning_ref = read_text(".agents/skills/create-proxy/references/parameter-tuning.md")
        self.assertIn("scene-optimizer-core/source", doc)
        self.assertIn("GetNumTimeSamples", doc)
        self.assertIn("Setting it elsewhere produces a variant", doc)
        self.assertIn("never run a config that still contains `<base64 ...>`", decimate_ref)
        self.assertIn("0.0` disables", tuning_ref)

    def test_decimate_meshes_preserves_float_and_boundary_semantics(self):
        doc = read_text(".agents/operations/decimateMeshes.md")
        self.assertIn("Use float literals", doc)
        self.assertIn("metersPerUnit", doc)
        self.assertIn("pinBoundaries: true` must appear literally", doc)
        self.assertIn("Skinned meshes", doc)
        self.assertNotIn('"reductionFactor": 0,', doc)
        self.assertNotIn('"maxMeanError": 0,', doc)

    def test_fit_primitives_ignore_flags_are_source_verified(self):
        doc = read_text(".agents/operations/fitPrimitives.md")
        self.assertIn("Source-verified semantics", doc)
        self.assertIn("allows fitting despite non-constant primvars", doc)
        self.assertIn("Set `false` to preserve", doc)
        self.assertIn("Normal primvars", doc)

    def test_mesh_cleanup_and_uv_docs_keep_corrected_guidance(self):
        mesh_doc = normalized(".agents/operations/meshCleanup.md")
        uv_doc = read_text(".agents/operations/removeUnusedUVs.md")
        self.assertIn("tolerance: 0.0", mesh_doc)
        self.assertIn("merge only exact coincident vertices", mesh_doc)
        self.assertIn('"makeManifold": false', mesh_doc)
        self.assertIn("For CAD/BIM scenes", uv_doc)

    def test_named_pipeline_keeps_decimation_boundary_guard(self):
        data = json.loads(read_text("tools/perf_operations/pipelines.json"))
        decimate = data["mesh-count-reduction"][-1]
        self.assertEqual(decimate["operation"], "decimateMeshes")
        self.assertEqual(decimate["reductionFactor"], 0.0)
        self.assertTrue(decimate["pinBoundaries"])


if __name__ == "__main__":
    unittest.main()
