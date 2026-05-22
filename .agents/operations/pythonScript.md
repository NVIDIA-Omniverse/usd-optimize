<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Python Script

**Key:** `pythonScript`
**Source:** `source/operations/pythonScript/__init__.py`

## Overview

Python Script executes a user-defined Python script as a Scene Optimizer operation. The script receives the current USD stage as a variable (`stage`) and can perform arbitrary modifications. This operation enables custom logic within Scene Optimizer pipelines without requiring a new C++ plugin.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `python` | string (base64) | `'print("Hello world!")'` | Python script to execute. In the UI this appears as a plain-text code editor; the value is base64-encoded before execution. |

**Encoding:** the `python` argument is always base64-decoded at execution time. When using the UI code editor, encoding is handled transparently — you write plain Python. When passing the argument via JSON (batch mode, CLI, or programmatic calls), you must provide the script as a base64-encoded string.

Example — the plain script `print(stage.GetRootLayer().identifier)` becomes `cHJpbnQoc3RhZ2UuR2V0Um9vdExheWVyKCkuaWRlbnRpZmllcik=` in JSON.

## Tuning Order

_Not applicable — single parameter (the script itself); behavior is fully determined by the script content._

## Visual Diagnosis

_Not applicable — visual effect is whatever the user's script produces. Diagnose by inspecting script output and the stage state after execution._

## Starting Configs

**Print stage info** (JSON/batch — base64-encoded):
```json
[{"operation": "pythonScript", "python": "cHJpbnQoc3RhZ2UuR2V0Um9vdExheWVyKCkuaWRlbnRpZmllcik="}]
```

**UI code editor equivalent** (plain text):
```python
print(stage.GetRootLayer().identifier)
```

## Prerequisites & Workflows

- Requires Python execution environment.
- The `stage` variable is available in the script's execution context, bound to the current USD stage.

## Known Limitations

- No sandboxing — scripts have full Python access.
- Error handling is minimal; script exceptions will cause operation failure.
- Only ASCII-compatible scripts are supported (the base64 encode/decode uses ASCII).