<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Print Stats

**Key:** `printStats`
**Source:** `source/operations/printStats/PrintStats.cpp`

## Overview

Print Stats is a hidden diagnostic operation that outputs scene statistics including prim counts, mesh counts, vertex/face totals, and optionally primvar and timing information.

## Parameters

| Parameter | Type | Default | Description |
|---|---|---|---|
| `countPrimvars` | bool | `false` | Include primvar statistics in the output. |
| `splitCollocatedPoints` | bool | `false` | Count collocated points as separate when reporting. |
| `time` | bool | `false` | Include timing information. |

## Tuning Order

_Not applicable — boolean toggles for what to include in the report; no interaction order._

## Visual Diagnosis

_Not applicable — diagnostic operation; output is a stats report to the log, not a stage modification._

## Starting Configs

_Not applicable — hidden internal operation; toggles are set programmatically by the caller._

## Prerequisites & Workflows

- Hidden operation (`getVisible()` returns false).
- Used internally for scene analysis, debugging, and benchmarking.

## Known Limitations

- Hidden operation — not shown in the UI.
- Output is to the log; no stage modifications.