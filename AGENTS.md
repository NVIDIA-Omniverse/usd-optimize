<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# AGENTS.md

Entry point for any coding agent (Claude Code, Codex/GPT, etc.) working in this repository. `CLAUDE.md` is a symlink to this file so Claude Code reads it automatically; non-Claude agents read `AGENTS.md` directly. Either way, this is the single source of truth.

## Project Overview

Scene Optimizer is a standalone C++ library providing 45+ USD scene optimization operations (geometry, materials, hierarchy, analysis) with a plugin system and Python bindings.

## Build & Test Commands

The repo ships two equivalent entry scripts: `./repo.sh <command>` for Linux/bash-compatible shells, and `repo.bat <command>` for Windows `cmd.exe`/PowerShell. They accept the same arguments — pick whichever matches the active shell.

```bash
# Build
./repo.sh build      # Linux / bash
repo.bat build       # Windows cmd.exe / PowerShell

# Run all tests (cpp, python)
./repo.sh test
repo.bat test

# Check code format (CI)
./repo.sh ci format
repo.bat ci format
```

## Code Style

- **C++:** Allman braces, 4-space indent, 120 column limit (`.clang-format`)
- **Python:** Flake8, 120 column limit, max complexity 18 (`.flake8`)
- **C++ standard:** C++17

## Architecture

### Plugin System

The core is a C++ plugin architecture. Each optimization operation is a shared library that subclasses `omni::scene::optimizer::Operation` (in `source/core/src/Operation.h`) and registers itself with `SO_PLUGIN_INIT`. Plugins in `source/operations/` are auto-discovered at build time via premake.

### Key Layers

1. **Public API** — `include/omni/sceneoptimizer/ISceneOptimizer.h` — C++ interface consumed by external callers
2. **Core Library** (`source/core/`) — Operation manager, Python bindings via pybind11 (`source/core/bindings/BindingsPython.cpp`)
3. **Operations** (`source/operations/`) — 45+ plugins; each is a standalone `.cpp` file (and optional headers) that registers one operation

### Operation Lifecycle

Arguments are declared in the constructor via `addArgument()`; their bound member variables are auto-populated from JSON/UI before `executeImpl()` is called. After execution, arguments reset to defaults. Access the USD stage via `getUsdStage()`.

### Test Organization

```text
source/tests/
├── test.cpp/omni.scene.optimizer.core/   # C++ unit tests (Doctest)
├── test.python/                          # Python binding tests
└── test.cuda.utils/                      # Helper shared lib for the cpp suite (not its own runnable suite)
```

## Writing a New Operation

1. Create a `.cpp` (and optional `.h`) under `source/operations/`
2. Subclass `omni::scene::optimizer::Operation`; call base constructor with `(key, displayName, description)`
3. Declare arguments in the constructor with `addArgument()`
4. Implement `executeImpl()` returning `OperationResult::eSuccess`
5. Register at the bottom: `SO_PLUGIN_INIT(omni::scene::optimizer::MyPlugin);`
6. To support analysis mode, override `getSupportsAnalysis()` → `true` and implement `executeAnalysisImpl()`
7. Add docs entry in `operations.rst`

Display group constants: `s_displayGroupGeometry`, `s_displayGroupMaterials`, `s_displayGroupStage`, `s_displayGroupUtilities`

See `PLUGINS.md` for the full plugin authoring guide and `source/core/src/Argument.h` for all display types and argument configuration methods.

## Code Coverage (Linux only)

Coverage tooling is Linux-only (gcov), so use `./repo.sh` only:

```bash
./repo.sh build --rebuild --config "release" --enable-gcov
./repo.sh test
./repo.sh cxx_coverage --collect --generate-html --remove --report
# Output: ./_build/coverage/
./repo.sh cxx_coverage --zero-coverage  # Reset counters
```

## Performance Validators

Scene Optimizer ships an `omniverse-asset-validator` integration: a set of rules under the `Performance` category that wrap analysis-mode operations. Most register by default; a few are opt-in because they're slow on large stages — see `_default_rule_classes()` and `_expensive_rule_classes()` in `source/core/python/omni/scene/optimizer/validators/_plugin.py` for the authoritative lists. Tests at `source/tests/test.python/test_validators_*.py`.

Two perf-relevant features in `_base.py`: an analysis-result cache keyed by root-layer identifier (so e.g. the mesh-cleanup-family rules share one `meshCleanup` analysis), and a `REQUIRES_MESH` short-circuit that skips mesh-only rules on stages without `UsdGeomMesh` prims.

Use the `validators` skill (`.agents/skills/validators/SKILL.md`) for invocation, file logging, and adding new validators. The asset-validator discovers plugins via `importlib.metadata` entry points, so **`omni_asset_validate` only sees Scene Optimizer after the `omniverse-scene-optimizer` wheel is pip-installed** (source-tree `PYTHONPATH` alone registers no entry point metadata). Third-party callers must either invoke `register_all()` programmatically (use `include_expensive=True` to include `FindOverlappingMeshes`) or pip-install the wheel and set `OMNI_ASSET_VALIDATOR_ISOLATE_ENTRYPOINTS` for CLI allow-listing.

To validate a USD asset and present a structured report, use the `run-validators` and `interpret-validators` skills (Claude aliases `/run-validators` and `/interpret-validators`). The skill files live at `.agents/skills/run-validators/SKILL.md` and `.agents/skills/interpret-validators/SKILL.md` — agents that don't auto-discover `.agents/skills/` (e.g. Codex) can be pointed at those paths directly. They drive `tools/perf_validators/run.sh` (POSIX) or `run.bat` (Windows) and reuse the cross-platform Python helpers in `tools/perf_validators/`.

## Skills

Reusable, task-specific instructions live under `.agents/skills/<name>/SKILL.md`. They are plain Markdown — any agent can read and follow them.

The repo root also contains `.claude` and `.codex` as symlinks to `.agents` (Claude Code and Codex defaults); **`.agents/` is the canonical path** in docs and tooling references.

`.agents/skills/README.md` is a one-screen cross-skill index — when to use each skill, which ones compose, and which cross-references are load-bearing. Read it before opening individual `SKILL.md` files; it's the fastest way to know what's where without reading every skill in full.

When a task could match a skill, inventory `.agents/skills/` and read each `SKILL.md`'s YAML frontmatter `description` field to decide which (if any) applies. Don't rely on a memorized list — the directory is the source of truth, so this stays correct as skills are added or removed.

Each `SKILL.md` opens with a "What this skill covers" block listing every section. **Skills can be 100s of lines long; load-bearing details often live past line ~50.** Search for keywords (e.g. `REQUIRES_MESH`, `family`, `analysisMode`, `pipeline`, `Tier`) before concluding info is missing.

Claude Code invokes skills via the `/<skill-name>` slash command or the `Skill` tool. Other agents simply open the relevant `SKILL.md` and follow its steps directly.

The end-to-end optimization loop (`run-validators` → `interpret-validators` → `run-operations` → re-validate) is documented in `.agents/skills/README.md` and `.agents/operations/PIPELINES.md`.

## Operation Guides and Tuning

For tuning or understanding any Scene Optimizer operation, the per-operation guides under `.agents/operations/` are the primary reference:

- `.agents/operations/INDEX.md` — operation list and guide status.
- `.agents/operations/INVOCATION.md` — canonical Python / shell invocation reference (`SceneOptimizerCore.executeOperation`, `executeConfig`, `standalone.execute_commands_from_json`, the `run-operations` driver).
- `.agents/operations/PIPELINES.md` — curated multi-op chains organized by bottleneck (memory, load time, mesh count, data quality), with critical caveats (don't merge if instanced; deduplicator is mesh-level only) and an upstream-authoring sidebar.
- `.agents/operations/<key>.md` — the guide for a specific operation. Each guide's `Source:` line is the authoritative pointer to the C++ implementation.
- `.agents/operations/_template.md` — template for adding a new operation guide.

If a guide is missing for an operation, fall back to reading the C++ source. The operation key is the first string argument passed to the `Operation(...)` constructor inside the relevant `*Operation.cpp` file (not `SO_PLUGIN_INIT`, which takes the class type).

Claude Code starts an interactive parameter tuning session with `/tune-parameters`. Other agents can run the same workflow by reading the operation's guide (or the underlying skill at `.agents/skills/tune-parameters/SKILL.md`) and iterating with the user.

Developers: to add tuning support for a new operation, copy `_template.md` and fill in the sections. See `shrinkwrap.md` for a completed example.

## Tool name translation (for non-Claude agents)

Skill bodies and frontmatter use Claude Code's tool vocabulary. Map to your equivalents:

| SKILL.md says | Codex / generic equivalent |
|---|---|
| `Bash`, `Shell` | `shell_command` (run in the active shell) |
| `Read` | open / cat the file |
| `Glob` | `find` / shell glob |
| `Grep` | `rg` / `grep` |
| `Edit` (tracked file) | `apply_patch` |
| `Write` (new file) | `apply_patch` for tracked paths; otherwise the agent's file-write equivalent |
| `Skill(<name>)` | open `.agents/skills/<name>/SKILL.md` and follow its steps |

Translate shell syntax (env vars, command chaining, path separators) to match your shell — that's standard cross-shell handling, not Scene Optimizer-specific.
