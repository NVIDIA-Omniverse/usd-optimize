---
name: new-validator
description: Add a new performance-validator rule that wraps a Usd Optimize analysis-mode operation. Use when creating a new validator rule class from scratch.
allowed-tools: Shell, Read, Edit, Write
metadata:
  author: NVIDIA Corporation
  version: "2.0.0"
  tags: [validation, performance, authoring]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# new-validator — Add a new performance validator rule

Creates a new `usd-validation-nvidia` rule class that wraps a Usd Optimize
analysis-mode operation, registers it, tests it, and regenerates the docs.

The backing operation must already support analysis mode. If it doesn't, add
`executeAnalysisImpl()` to the operation first (see `PLUGINS.md` § Analysis Mode).

## What this skill covers

- **How validators work** — the base class, key class variables, and lifecycle.
- **Step 1** — create the rule class.
- **Step 2** — implement `_CheckStage`.
- **Step 3** — add a fix suggestion (optional).
- **Step 4** — add user-tunable parameters (optional).
- **Step 5** — register the rule.
- **Step 6** — add tests.
- **Step 7** — build and verify.

Companion skills: [`new-operation`](../new-operation/SKILL.md) (create the
backing operation), [`run-validators`](../run-validators/SKILL.md) (run the
validator and apply fixes).

---

## How validators work

All rule classes subclass `BaseUsdOptimizeChecker` from
`source/validators/python/usd_optimize/validators/base_usd_optimize_checker.py`.

The base class handles running the operation in analysis mode via
`CheckStage()`. It calls `_AnalyzeStage()` to execute the operation, then
passes the `analysis` payload from the result to `_CheckStage()` — the one
method every subclass must implement.

Key class variables:

| Variable | Required | Purpose |
|---|---|---|
| `OPERATION_NAME` | Yes | The operation key string (matches the C++ constructor first arg). |
| `OPERATION_ARGS` | No | Static `dict` of args forwarded to the operation every run. Defaults to `{}`. |
| `PARAMETERS` | No | User-tunable args exposed through Asset Validator's parameter UI. See Step 4. |

Look at the existing checkers under
`source/validators/python/usd_optimize/validators/` as the primary reference.
`empty_leaf_checker.py` is the simplest example; `small_mesh_checker.py`
shows `PARAMETERS`; `base_duplicate_geometry_checker.py` shows a shared base
class pattern.

---

## Step 1 — Create the rule class

Create `source/validators/python/usd_optimize/validators/<snake_case>_checker.py`:

```python
# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0

from pxr import Usd
from usd_validation_nvidia import capabilities, register_requirements

from .base_usd_optimize_checker import BaseUsdOptimizeChecker


@register_requirements(capabilities.<FamilyRequirements>.<RULE_ID>)
class <Name>Checker(BaseUsdOptimizeChecker):
    """
    One-sentence description of what this rule checks.
    """

    OPERATION_NAME: str = "<operation_key>"
    OPERATION_ARGS: dict = {"argName": value, ...}  # omit if no static args needed

    def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
        ...
```

`@register_requirements` binds the rule to an Asset Validator capability ID
(from `usd_validation_nvidia.capabilities`). Pass `override=True` if
overriding an existing base rule for the same capability.

## Step 2 — Implement `_CheckStage`

`analysis_data` is the `dict` returned by the operation's analysis mode (the
`"analysis"` key from the result). Its schema is specific to each operation —
read the operation's `executeAnalysisImpl()` in the C++ source to see what it
returns.

Call `self._AddWarning()` (most common) or `self._AddFailedCheck()` for each
issue. The `at` argument takes a `Usd.Stage` or `Usd.Prim`:

```python
def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
    count = len(analysis_data.get("someKey", []))
    if count > 0:
        self._AddWarning(
            message=f"Stage contains {count} problematic prims",
            at=usdStage.GetPrimAtPath("/"),
        )
```

To also report individual prim-level issues, iterate the paths and add a
separate warning per prim with `at=usdStage.GetPrimAtPath(path)`.

## Step 3 — Add a fix suggestion (optional)

Rules that can auto-fix attach a `Suggestion` to the warning. The suggestion's
`callable` receives `(usdStage, at_prim)` and applies the fix via
`analysis.optimize()`. Use `functools.partial` to bind extra args:

```python
from functools import partial
from usd_optimize.core import analysis
from usd_validation_nvidia import Suggestion

@classmethod
def _fix_stage(cls, usdStage: Usd.Stage, _: Usd.Prim) -> None:
    analysis.optimize(usdStage, [analysis.OperationConfig(cls.OPERATION_NAME, args=cls.OPERATION_ARGS)])

def _CheckStage(self, usdStage: Usd.Stage, analysis_data: dict):
    ...
    self._AddWarning(
        message="...",
        at=usdStage.GetPrimAtPath("/"),
        suggestion=Suggestion(
            message="Fix using Usd Optimize",
            callable=self._fix_stage,
        ),
    )
```

When the fix should use the operation configs derived from the analysis result
(rather than static args), use `self.suggested_operations` — it is populated
by `_AnalyzeStage` before `_CheckStage` is called. See `small_mesh_checker.py`
and `zero_extent_checker.py` for this pattern:

```python
suggestion=Suggestion(
    message="Fix using Usd Optimize",
    callable=partial(self._fix_stage, operation_configs=self.suggested_operations),
)
```

## Step 4 — Add user-tunable parameters (optional)

Declare `PARAMETERS` to expose per-rule settings in Asset Validator's parameter
UI. Each entry maps a display name to a `Parameter` that names the underlying
operation arg and its default:

```python
from .base_usd_optimize_checker import BaseUsdOptimizeChecker, Parameter, ParameterFromOpArg

class MyChecker(BaseUsdOptimizeChecker):
    PARAMETERS = {
        "SIZE_THRESHOLD": Parameter(op_arg="threshold", default=0.001,
                                    description="Minimum size before a mesh is flagged."),
    }
```

Use `ParameterFromOpArg` instead of `Parameter` to pull the default and
description directly from the operation's argument metadata (avoids
duplication):

```python
    PARAMETERS = {
        "SIZE_THRESHOLD": ParameterFromOpArg("threshold"),
    }
```

In `_CheckStage`, read the effective value (with any user override applied) via
`self._effective_args()["threshold"]` rather than `OPERATION_ARGS` directly.

## Step 5 — Register the rule

In `source/validators/python/usd_optimize/validators/__init__.py`:

1. Add an import at the top:
   ```python
   from .<snake_case>_checker import <Name>Checker
   ```
2. Add to `_RULE_CATEGORIES`:
   ```python
   (<Name>Checker, "Usd:Performance"),   # or "Omni:Geometry"
   ```
   Use `"Usd:Performance"` for stage/scene-level conditions; `"Omni:Geometry"`
   for low-level geometric defects.
3. Add to `__all__`.

## Step 6 — Add tests

Add test cases for the new rule to the existing test files in
`source/tests/test.python/`:

- **`test_checkers.py`** — functional tests using `ValidationTestCaseMixin.assertRule()`.
  Add a test method that supplies a `.usda` fixture and asserts the expected
  warnings. Add the required fixtures to the test data directory.
- **`test_validator_plugin_entry_point.py`** — no changes needed; it imports
  from `_RULE_CATEGORIES` and covers all registered rules automatically.
- **`test_checker_parameters.py`** — add a test only if the rule declares
  `PARAMETERS` and has non-trivial parameter logic to verify.

## Step 7 — Build and verify

```bash
./repo.sh build
./repo.sh test -s python
./repo.sh docs_gen --autogen_only
```

After `docs_gen`, the rule appears in
[`docs/performance-validators.rst`](../../../docs/performance-validators.rst).

Verify the rule is registered:

```python
import usd_validation_nvidia          # import first
from usd_optimize.validators import register_all
rules = register_all()
# register_all() renames each rule to UsdOptimize<Name>Checker before returning it,
# so assert against the registered name, not the class name as written.
assert any(r.__name__ == "UsdOptimize<Name>Checker" for r in rules)
```
