---
name: personas
description: "User personas that shaped the tune-parameters skill design."
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# /tune-parameters User Personas

Four personas that shaped the design of the `/tune-parameters` skill and its supporting files.

## Persona A: Artist / Scene Author (primary user)

**Goal:** Get a visually correct result on their specific USD scene.

**Interaction:** Runs `/tune-parameters`, names the operation, provides screenshots, iterates until happy.

**Needs:** Plain-language explanations, visual diagnosis, conservative starting configs, batch CLI command to copy-paste.

**Pain point:** Most operations have no guide, so they get the Tier 3 fallback path.

---

## Persona B: Operation Developer (guide author)

**Goal:** Write a tuning guide for an operation they built or deeply understand.

**Needs:** Tooling to scaffold a guide from C++ source (`addArgument()` calls).

**Entry point:** `/tune-parameters create a guide for <operation>` → Guide Authoring Mode.

---

## Persona C: QA / Regression Tester

**Goal:** Confirm an operation still produces expected results after code changes.

**Needs:** Per-operation session logs with known-good configs and expected outputs.

**Entry point:** `.agents/docs/sessions/<operation>.md` — real-world configs and iteration history.

---

## Persona D: New Team Member / Evaluator

**Goal:** Understand what Scene Optimizer can do and which operations are well-supported.

**Needs:** An index to discover operations without already knowing their names.

**Entry point:** `.agents/operations/INDEX.md` — all 45 operations with guide status, arg count, and priority.

---

## Implementation Status

| Feature | Persona | Status |
|---|---|---|
| Three-tier fallback (guide > sessions > source) | A | Done |
| Plain-language parameter explanations | A | Done |
| Platform-aware batch CLI in Step 4 | A | Done |
| Guide authoring mode | B | Done |
| Per-operation session files | C | Done |
| Operation INDEX.md | D | Done |
| Operation-specific screenshot guidance | A | Done |