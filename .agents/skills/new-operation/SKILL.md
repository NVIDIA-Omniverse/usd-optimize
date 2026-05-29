---
name: new-operation
description: Scaffold a new Scene Optimizer operation plugin (C++ source, premake, test, guide, optional validator). Use to add a new op or plugin.
version: "1.0.0"
allowed-tools: Shell, Read, Write, Glob, Grep
metadata:
  author: NVIDIA Corporation
  tags: [scaffolding, plugin, development]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# New Operation

Scaffold a complete Scene Optimizer operation plugin from scratch. The
output is a buildable, testable plugin with all the wiring in place.

## What this skill covers

- **Inputs** — what the user must provide.
- **Step 1** — create the C++ source file.
- **Step 2** — create the premake file.
- **Step 3** — create the Python test file.
- **Step 4** — create the operation guide.
- **Step 5** — update the operation index.
- **Step 6** — (optional) create a validator wrapper.
- **Step 7** — build and verify.
- **Naming conventions** — key, class name, file names.

Companion docs: `PLUGINS.md` (full plugin authoring reference),
`.agents/operations/_template.md` (guide template),
`.agents/skills/validators/SKILL.md` (validator authoring recipe).

---

## Inputs

Gather these from the user before scaffolding:

| Input | Required | Example | Notes |
|---|---|---|---|
| Operation key | yes | `removeOverlaps` | camelCase, used in JSON configs and the Python API. |
| Display name | yes | `Remove Overlaps` | Title case, shown in UI. |
| Description | yes | `Removes overlapping mesh regions` | One sentence, shown in UI tooltips. |
| Display group | yes | `Geometry` | One of: `Geometry`, `Materials`, `Stage`, `Utilities`. |
| Author | no | `Your Name` | Defaults to `NVIDIA Corporation`. |
| Arguments | no | (list of name/type/default/description) | Can be added later. |
| Supports analysis | no | `true` / `false` | Whether to scaffold `executeAnalysisImpl`. Default `false`. |
| Needs validator | no | `true` / `false` | Whether to also scaffold a validator wrapper. Default `false`. |

If the user only provides a goal (e.g. "I want to detect X"), help them
derive the key, display name, and description before proceeding.

---

## Naming conventions

| Thing | Convention | Example |
|---|---|---|
| Operation key | `camelCase` | `removeOverlaps` |
| C++ class (`<ClassName>`) | `PascalCase` + `Operation` suffix | `RemoveOverlapsOperation` |
| C++ file basename (`<FileBase>`) | `PascalCase`, typically the class name without the `Operation` suffix | `RemoveOverlaps` |
| Source directory | `source/operations/<key>/` | `source/operations/removeOverlaps/` |
| C++ file | `<FileBase>.cpp` | `RemoveOverlaps.cpp` |
| Test file | `test_operation_<snake_case>.py` | `test_operation_remove_overlaps.py` |
| Operation guide | `.agents/operations/<key>.md` | `.agents/operations/removeOverlaps.md` |
| Report category (`<CATEGORY>`) | `UPPER_SNAKE_CASE` short identifier | `REMOVE_OVERLAPS` |
| Validator class | `SceneOptimizer` + `PascalCase` + `Checker` | `SceneOptimizerRemoveOverlapsChecker` |

> **File-basename convention is a soft rule.** Premake auto-discovers any `*.cpp` under `source/operations/<key>/`, so the filename isn't enforced. A handful of existing operations diverge (e.g. `fitPrimitives/Primitive.cpp` for `PrimitiveFitOperation`, `subdivideMeshes/Subdivide.cpp` for `SubdivideOperation`). Prefer the convention for new operations; don't rename existing ones just to match.

---

## Step 1 — Create the C++ source

Create `source/operations/<key>/<FileBase>.cpp`. Use `source/operations/<key>/`
as the directory — premake auto-discovers it.

```cpp
// SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
// SPDX-License-Identifier: Apache-2.0

#include <omni/scene.optimizer/core/Core.h>
#include <omni/scene.optimizer/core/Operation.h>

namespace omni::scene::optimizer
{

constexpr const char* s_category = "<CATEGORY>";

class <ClassName> : public Operation
{
public:
    <ClassName>()
        : Operation("<key>", "<Display Name>", "<Description>")
    {
        // Declare arguments here via addArgument().
        // Example:
        // addArgument("threshold", "Threshold", kDisplayTypeFloat,
        //             "Distance threshold in stage units", m_threshold);
    }

    std::string getAuthor() const override
    {
        return "<Author>";
    }

    SOPluginVersion getVersion() const override
    {
        return { 1, 0, 0 };
    }

    std::string getCategory() const override
    {
        return s_category;
    }

    std::string getDisplayGroup() const override
    {
        return <display_group_constant>;
    }

    // Uncomment if the operation supports analysis mode:
    // bool getSupportsAnalysis() const override { return true; }

protected:
    OperationResult executeImpl() override
    {
        // TODO: implement
        return OperationResult::eSuccess;
    }

    // Uncomment if the operation supports analysis mode:
    // OperationResult executeAnalysisImpl() override
    // {
    //     // TODO: implement analysis (read-only inspection)
    //     return OperationResult::eSuccess;
    // }

private:
    // Member variables bound to arguments:
    // float m_threshold = 0.001f;
};

} // namespace omni::scene::optimizer

// Register plugin after the class declaration so this single-file template compiles.
SO_PLUGIN_INIT(omni::scene::optimizer::<ClassName>);
```

Map `<display_group_constant>` from the user's display group choice:

| Display group | Constant |
|---|---|
| Geometry | `s_displayGroupGeometry` |
| Materials | `s_displayGroupMaterials` |
| Stage | `s_displayGroupStage` |
| Utilities | `s_displayGroupUtilities` |

> **`getCategory()` is not the display group.** `getCategory()` returns an
> UPPER_SNAKE_CASE report-bucket identifier used for grouping log/report
> output (e.g. existing operations return `"COINCIDING"`, `"FIT_PRIMITIVES"`,
> `"SHRINKWRAP"`, `"OUTPUT"`). `getDisplayGroup()` is a separate concern —
> it returns one of the four `s_displayGroup*` constants above and controls
> UI grouping. Pick a value for each independently; don't substitute one
> for the other.

If the user provided arguments, add `addArgument()` calls in the
constructor and corresponding member variables. See `PLUGINS.md` for
display types, enum arguments, grouping, and further configuration.

If `supportsAnalysis` is true, uncomment the analysis stubs.

## Step 2 — Create the premake file

Create `source/operations/<key>/premake5.lua`:

```lua
project_with_location("<key>")
    so_build.operation_plugin({ "*.cpp" })
```

This is the standard one-liner. The parent `source/operations/premake5.lua`
auto-discovers it via `os.matchfiles("**premake5.lua")`.

If the operation needs additional libraries beyond the core SDK, add
`links` or `includedirs` calls after `operation_plugin`. Look at existing
operations (e.g. `decimateMeshes`, `shrinkwrap`) for examples of linking
third-party deps.

## Step 3 — Create the Python test file

Create `source/tests/test.python/test_operation_<snake_case>.py`:

```python
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

from pxr import Usd, UsdGeom

from .test_utils import Test_Operation


class Test_Operation_<PascalCase>(Test_Operation):

    OPERATION = "<key>"

    async def test_basic(self):
        """Verify the operation runs without error on a trivial stage."""
        stage = Usd.Stage.CreateInMemory()
        UsdGeom.Xform.Define(stage, "/World")
        UsdGeom.Mesh.Define(stage, "/World/Mesh")

        # Set up minimal mesh data so the operation has something to work with
        mesh = UsdGeom.Mesh(stage.GetPrimAtPath("/World/Mesh"))
        mesh.CreatePointsAttr([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)])
        mesh.CreateFaceVertexCountsAttr([4])
        mesh.CreateFaceVertexIndicesAttr([0, 1, 2, 3])

        _, (success, error, _) = self._execute_command({})
        self.assertTrue(success, f"Operation failed: {error}")
```

The test uses `Test_Operation` as the base class, which handles stage
caching and teardown. Tests are async by convention (the metaclass wraps
them for sync execution). Use `self._execute_command(args_dict)` to run
the operation.

For operations that need fixture files, place them in
`source/tests/data` and load with `self._open_stage("fixture.usda")`.

## Step 4 — Create the operation guide

Copy `.agents/operations/_template.md` to `.agents/operations/<key>.md`
and fill in the header:

```markdown
# <Display Name>

**Key:** `<key>`
**Source:** `source/operations/<key>/<FileBase>.cpp`
```

Fill in the Parameters table from the `addArgument()` calls. Leave the
Overview, Tuning Order, Visual Diagnosis, Starting Configs, and Known
Limitations sections with &lt;!-- TODO(developer) --&gt; markers (HTML
comment, literal text `TODO(developer)` between the comment delimiters)
if the operation logic isn't implemented yet.

Alternatively, use the `tune-parameters` skill's **Guide Authoring Mode**
(`/tune-parameters create a guide for <key>`) to auto-generate the guide
from the C++ source.

## Step 5 — Update the operation index

Add a row to `.agents/operations/INDEX.md` in the appropriate position
(sorted by argument count descending):

```markdown
| <Display Name> | `<key>` | <arg_count> |
```

## Step 6 — (Optional) Create a validator wrapper

If the operation supports analysis mode and a validator is requested,
follow the recipe in `.agents/skills/validators/SKILL.md` §
"Adding a new performance validator":

1. Create `source/core/python/omni/scene/optimizer/validators/<snake_case>.py`
   subclassing `SceneOptimizerRuleBase`.
2. Set `OPERATION_NAME = "<key>"` and `DEFAULT_ARGS = {<default_args>}`.
3. Override `_translate(stage, analysis)` to convert the analysis JSON
   into `_AddWarning` / `_AddFailedCheck` calls.
4. Set `REQUIRES_MESH = False` if the operation targets hierarchy /
   materials / animation rather than meshes.
5. Re-export from `validators/__init__.py`.
6. Add to `_default_rule_classes()` or `_expensive_rule_classes()` in
   `validators/_plugin.py`.
7. Add a test in `source/tests/test.python/test_validators_<snake_case>.py`.
8. Update `test_validators_smoke.py` `EXPECTED_RULES` tuple.

## Step 7 — Build and verify

```bash
./repo.sh build
./repo.sh test -s cpp
./repo.sh test -s python
```

The `python` suite covers any `test_operation_<snake_case>.py` and
`test_validators_<snake_case>.py` files you added. The repo's Python test
wrapper currently runs the full suite; it does not forward per-file filters to
`source/tests/test.python/run_discover.py`.

Verify the operation is registered:

```bash
# Should print True
python3 -c "from omni.scene.optimizer.core import SceneOptimizerCore; print('<key>' in SceneOptimizerCore.getInstance().getOperations())"
```

---

## See also

- `PLUGINS.md` — full plugin authoring guide (arguments, display types,
  enums, groups, analysis mode, registration).
- `.agents/operations/_template.md` — operation guide template.
- `.agents/operations/INVOCATION.md` — how to call operations from Python.
- `.agents/skills/validators/SKILL.md` — validator infrastructure reference.
- `.agents/skills/tune-parameters/SKILL.md` — guide authoring mode for
  auto-generating the `.agents/operations/<key>.md` from C++ source.

## Purpose

Stand up a complete, buildable, testable operation plugin from
scratch — the C++ source, premake build entry, Python binding test,
operation guide, optional validator wrapper, and the index update.
The output compiles, registers, and runs after `./repo.sh build` even
before any operation logic is implemented (the stub `executeImpl`
returns success).

## Prerequisites

- A built repo (`./repo.sh build` or `repo.bat build`) so the
  scaffolded plugin links and its tests can run.
- The user's intent in concrete form: operation key, display name,
  description, display group, and optionally arguments + analysis-mode
  flag. The skill prompts for any missing inputs before generating
  files.
- Write access to the source tree: `source/operations/`,
  `source/tests/test.python/`, `.agents/operations/`.

## Limitations

- The scaffold compiles but does **not** implement the operation logic
  — `executeImpl()` returns `eSuccess` as a stub. The author wires up
  the actual algorithm in a follow-up pass.
- The generated guide leaves Overview / Tuning Order / Visual Diagnosis
  sections marked with HTML-comment TODO markers (literal text
  `TODO(developer)` wrapped in an HTML comment) until the author fills
  them in or runs the `tune-parameters` Guide Authoring Mode.
- The validator step is optional and only meaningful when the operation
  supports analysis mode.
- Cross-platform compilation (Windows + Linux) and any third-party
  dependency wiring beyond the SDK is the author's responsibility.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `error: unknown operation '<key>'` after build | Premake didn't pick up the new `source/operations/<key>/`. | Confirm `premake5.lua` exists in the new dir; re-run `./repo.sh build --rebuild`. |
| Plugin builds but `getOperations()` doesn't list `<key>` | Missing `SO_PLUGIN_INIT(...)` macro or wrong namespace. | Ensure the macro at the bottom of the `.cpp` (after the closing `} // namespace omni::scene::optimizer` line) matches the class's fully-qualified name (see the template in Step 1). |
| Test file errors `ImportError: test_utils` | Test placed in wrong directory. | Tests must live under `source/tests/test.python/` (alongside `test_utils.py`). |
| `omni_asset_validate --listChecks` doesn't show the new validator | Validator class not re-exported from `validators/__init__.py` or not added to `_default_rule_classes()`. | Complete Step 6 sub-steps 5 & 6 of this skill. |
| `getCategory()` and `getDisplayGroup()` confused | Category is a report-bucket string; display group is a UI grouping constant. | See the callout in Step 1 — they're independent and use different value spaces. |

