<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

<!--
  OPERATION TUNING GUIDE TEMPLATE
  ================================
  Copy this file to .agents/operations/<operation-key>.md and fill in each section.
  See shrinkwrap.md for a completed example.

  TODO(developer): ... marks are prompts for you to fill in.
  Leave them if unsure — Claude will flag them as low-confidence to users.
  Grep: grep -r "TODO(developer)" .agents/operations/

  IMPORTANT — KEEP ALL STANDARD HEADINGS:
  Tools (and other agents) read these guides expecting the same set of section
  headings on every guide. If a section does not apply to your operation,
  KEEP THE HEADING and replace its body with a one-line stub:

      _Not applicable — <short reason>._

  Do NOT delete the heading. Examples of acceptable stubs:
  - "_Not applicable — no parameters to tune._" (0-arg ops)
  - "_Not applicable — no rendered output to diagnose._" (analysis/stats ops)
  - "_Not applicable — single parameter; nothing to order._" (1-arg ops)
-->

# <Operation Display Name>

**Key:** `<operation-key>`
**Source:** `source/operations/<operation-key>/<OperationClass>.cpp`

## Overview

<!-- TODO(developer): Explain what the operation does, when to use it, and how it
     works in intuitive terms. Use analogies and map each step of the algorithm to
     the parameter that controls it. Highlight which parameter matters most.
     The goal: a user who reads only this section should have enough mental model
     to adjust parameters on their own. See shrinkwrap.md for an example. -->

## Quick Start

<!-- TODO(developer): Show the canonical invocation. Most users want to copy-paste
     a starting JSON config; the snippet below is the JSON-driven path. For the
     full API surface (executeOperation, executeConfig, analysisMode, output
     inspection), point at .agents/operations/INVOCATION.md — don't duplicate it
     here. -->

```bash
# Drive via the run-operations skill (recommended)
tools/perf_operations/run.sh run path/to/asset.usd \
    --config '[{"operation": "<key>", "<arg1>": <value>}]' \
    --output out.usda
```

```python
# Or call directly via the Python bindings
from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
from pxr import Usd

stage = Usd.Stage.Open("path/to/asset.usd")
context = ExecutionContext()
context.set_stage(stage)

success, error, output = SceneOptimizerCore.getInstance().executeOperation(
    "<key>", context, {"<arg1>": <value>}
)
stage.GetRootLayer().Save()
```

For chained operations, output inspection in analysis mode, and the full
invocation reference, see `.agents/operations/INVOCATION.md`. For curated
multi-op pipelines by bottleneck, see `.agents/operations/PIPELINES.md`.

## Parameters

<!-- TODO(developer): Copy from addArgument() calls in the constructor.
     Include ALL parameters, even hidden ones (mark them as hidden).
     Note which parameters are in world units. -->

| Parameter | Type | Default | Description |
|---|---|---|---|
| `param1` | type | `default` | Description |
| `param2` | type | `default` | Description |

## Tuning Order

<!-- TODO(developer): Number the parameters in the order users should tune them.
     Explain *why* each comes before the next — this encodes developer intuition
     that users lack. Include key interactions inline (e.g., "X must be set before
     Y because Y has no effect if X is too coarse"). -->

1. **`param1` first** — Rationale.
2. **`param2` second** — Rationale.

## Visual Diagnosis

<!-- TODO(developer): Map visual symptoms to parameter adjustments.
     Users describe what they *see* — this table tells them what to *do*. -->

| Symptom | Parameter | Direction | Notes |
|---|---|---|---|
| Description of visual problem | `param` | Increase / Decrease | Context |

## Starting Configs

<!-- TODO(developer): 3-4 named configs for common use cases. -->

**Config Name** (short explanation):
```json
[{"operation": "<key>", "param1": value1, "param2": value2}]
```

## Prerequisites & Workflows

<!-- TODO(developer): Does this operation work standalone on any USD file, or does
     it require preconditions (e.g., manifold mesh, preceding merge)?
     List common workflows showing this operation in context. Examples:
     - "Works standalone on any mesh."
     - "Recommended after merge for multi-mesh scenes."
     - "Common pipeline: merge → shrinkwrap → meshCleanup" -->


## Known Limitations

<!-- TODO(developer): What doesn't the operation handle? Input requirements?
     Edge cases? -->

## Interactive Tuning Notes

<!-- Optional: note if this operation benefits from interactive viewport viewing,
     recommended viewport settings (e.g., wireframe overlay), or anything specific
     to live preview behavior. Delete this section if not applicable. -->