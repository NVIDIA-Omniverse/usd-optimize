---
name: writing-skills
description: Author or revise a Usd Optimize skill (.agents/skills/{name}/SKILL.md). Use when adding, restructuring, or reviewing a skill so it matches house conventions and is registered in the index.
license: Apache-2.0
allowed-tools: Shell, Read, Write, Edit, Glob, Grep
metadata:
  author: NVIDIA Corporation
  version: "1.0.0"
  tags: [meta, authoring, documentation, skills]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Writing Skills

Author a new skill, or revise an existing one, so it matches the conventions
every other skill in this repo follows and is discoverable by agents.

## What this skill covers

- **When to write a skill** — and when *not* to (use a reference doc instead).
- **General Guidelines** — the rules that keep skills consistent and readable.
- **Inputs** — what to gather before writing.
- **Step 1** — create the skill directory and `SKILL.md`.
- **Step 2** — write the frontmatter (required + optional fields).
- **Step 3** — write the body (the "What this skill covers" block, sections, length).
- **Step 4** — register the skill in `.agents/skills/README.md`.
- **Step 5** — verify (frontmatter, links, invocation).
- **Frontmatter reference** — every field, with examples.
- **House conventions** — the rules that keep skills consistent.
- **Anti-patterns** — what reviewers will flag.
- **Troubleshooting** — why a skill fails to fire, and what to do.
- **Purpose / Prerequisites / Limitations** — scope of this skill.

Companion docs: [`.agents/skills/README.md`](../README.md) (the cross-skill
index you must update), and the existing skills themselves — `new-operation`
and `inspect-asset` are good full-length references; `testing` is a good
shorter one.

---

## When to write a skill

Write a **skill** when you are capturing a *workflow* — a repeatable,
step-by-step task an agent performs ("run an operation", "validate an asset",
"diff two stages"). Per the repo philosophy: **skills are workflows, not
references.**

Do **not** write a skill when the content is reference material — facts,
schemas, parameter tables, API surfaces. These belong in the docs under `docs/`
and under skill references in `.agents/skills/<name>/references/`.

If you find yourself writing a skill that is mostly a lookup table with no
"do this, then that", then it should be a reference doc instead.

---

## General Guidelines

A lot of information exists in the public facing docs under `docs/`. Before
writing a skill, check if the content already exists in a reference doc.
If it does, link to it instead of duplicating it. This keeps the information
consistent and avoids drift. For example, if you are writing a skill that uses 
a specific operation, link to the operation's rst doc file under
`docs/operations/<operation>.rst`.

Likewise if writing information that should be public facing and not necessarily
just for agents this information can be added to the docs and linked to from the
skill. 

Skills should be focused on a single workflow. Avoid adding unrelated information
and overly verbose explanations. They should read more like a technical manual
than a tutorial.

Avoid making sweeping changes to the skills. If you find yourself wanting to
make a lot of changes to multiple skills, consider whether the changes are
necessary and if they can be made in a more focused way.

Punctuation is not policed. Em dashes are fine; so are colons and
parentheses. Prefer whichever keeps the sentence readable.

---

## Inputs

Gather these before writing:

| Input | Required | Example | Notes |
|---|---|---|---|
| Skill name | yes | `compare-stages` | `kebab-case`. Becomes the directory name and the `/<name>` slash command. |
| One-line purpose | yes | "Structured diff between two USD stages" | Becomes the basis of the `description` and the README index row. |
| When to use it | yes | "before/after optimization" | The trigger condition — drives the `description` (agents match on it). |
| Section list | yes | (the steps) | Each becomes a "What this skill covers" bullet and a body section. |
| Tools used | no | `Shell, Read, Write` | For `allowed-tools`. |
| Related skills / docs | no | `run-operations`, `docs/<doc>.rst` | For cross-references in the body and README. |

If the user only gives a goal, help them name the skill and decompose the
workflow into ordered steps first.

---

## Step 1 — create the directory and file

Skills live one-per-directory:

```
.agents/skills/<name>/SKILL.md
```

```bash
mkdir -p .agents/skills/<name>
```

---

## Step 2 — write the frontmatter

Every `SKILL.md` opens with YAML frontmatter, then the SPDX header comments.
Copy this shape (see the full **Frontmatter reference** below for each field):

```markdown
---
name: <name>
description: {what it does}. Use when {trigger}.
license: Apache-2.0
allowed-tools: Shell, Read, Write, Glob, Grep
metadata:
  author: NVIDIA Corporation
  version: "1.0.0"
  tags: [<tag>, <tag>]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->
```

The `description` is **load-bearing**: agents that don't auto-run the skill
inventory `.agents/skills/` and read each frontmatter `description` to decide
which skill applies. Write it as *what it does* + *when to use it*, in one
sentence. Lead with the action.

---

## Step 3 — write the body

After the frontmatter and SPDX comments:

1. **An H1 title** in Title Case (`# Writing Skills`).
2. **A one-to-three sentence summary** of what the skill accomplishes.
3. **A `## What this skill covers` block** — a bulleted list where each bullet
   names a section of the body in **bold** followed by a short gloss. This is
   the single most important structural convention: it lets an agent scan one
   block and know what's where without reading the whole file. List **every**
   section, including "Inputs" and any reference table at the end.
4. **Companion docs** line(s) right after the block, linking related skills and
   reference docs with relative paths.
5. **A `---` rule, then the sections**, in the order the "covers" block lists
   them. Use `## Step N — <verb>` for ordered workflows.

Length rules:

- **Be as concise as possible** while still being clear and complete. Long skills
  are only ok if the workflow is genuinely complex and requires it.
- **Put load-bearing detail where it belongs, even if that's past line ~50.**
  The README explicitly tells readers to read past the first 50 lines, so don't
  contort the structure to front-load everything — but do make the "covers"
  block point at it.
- Prefer tables for inputs, naming conventions, and option/flag references.
- Use fenced code blocks for every command and snippet. Show the exact
  invocation, not a paraphrase.

---

## Step 4 — register the skill in README.md

A skill that isn't in [`.agents/skills/README.md`](../README.md) is invisible
to the per-session index. Add it in **three** places (skip any that don't
apply):

1. **The "When to use which skill" table** — one row: first column a markdown
   link labelled with the skill name in backticks and pointing at
   `<name>/SKILL.md` (relative to `README.md`, so no leading path), second
   column a `Use when …` gloss. Place it near related skills, not necessarily
   at the end.
2. **The end-to-end loop diagram** — only if the skill is part of the
   inspect → validate → interpret → operate → compare flow.
3. **"Cross-references at a glance"** — a bullet per load-bearing pointer to or
   from another skill or reference doc.

Match the existing terse, action-first phrasing ("Use when …").

---

## Step 5 — verify

Run these checks before declaring done:

```bash
# Frontmatter present and parseable; name matches the directory.
head -12 .agents/skills/<name>/SKILL.md

# The skill is registered in the index.
grep -n "<name>" .agents/skills/README.md

# Relative links resolve (spot-check the paths you used).
ls .agents/skills/<name>/SKILL.md
```

Manual checklist:

- [ ] `name:` equals the directory name.
- [ ] `description:` says what it does **and** when to use it, with no angle brackets.
- [ ] SPDX header comments present (copyright + license).
- [ ] "What this skill covers" block lists every section.
- [ ] All commands are in fenced code blocks and are runnable as written.
- [ ] Cross-references use relative paths and point at real files.
- [ ] Registered in `README.md` (table row + any cross-reference bullets).
- [ ] If it composes with other skills, those skills' cross-reference bullets
      mention it too (links should be bidirectional where it matters).

---

## Frontmatter reference

| Field | Required | Example | Notes |
|---|---|---|---|
| `name` | yes | `compare-stages` | `kebab-case`; **must** equal the directory name. Drives `/<name>`. |
| `description` | yes | `Structured diff between two USD stages. Use when comparing before/after optimization.` | What + when, one sentence. This is how agents pick the skill. No angle brackets: a `<placeholder>` parses as an XML tag and fails validation. |
| `license` | yes | `Apache-2.0` | Same SPDX ID as the header comment; skill catalogs read the license from the frontmatter. |
| `allowed-tools` | recommended | `Shell, Read, Write, Glob, Grep` | Comma-separated Claude Code tool names the skill needs. |
| `metadata.author` | recommended | `NVIDIA Corporation` | Default author unless the user specifies otherwise. |
| `metadata.version` | recommended | `"1.0.0"` | Quoted string, **under `metadata`**: a root-level `version` is not in the schema and is ignored. Start at `1.0.0`; bump on meaningful change. |
| `metadata.tags` | optional | `[meta, authoring]` | Short topical tags for discovery. |

The README index uses a slightly different frontmatter (`name` + `description`
only) because it is an index, not a workflow — don't copy `allowed-tools` etc.
into it.

---

## House conventions

- **Workflows, not references** — see "When to write a skill".
- **The "covers" block is mandatory** and must map every section.
- **Skills cite each other deliberately** — when the canonical answer lives in
  another skill or reference doc, link it instead of duplicating it.
- **One source of truth** — if two skills need the same facts, factor them into
  a doc or reference doc.
- **Terse, imperative, action-first** — "Run …", "Create …", "Use when …".

---

## Anti-patterns

Reviewers will push back on these:

- **A skill that's really a reference doc** — a wall of tables with no steps.
  Put it under `docs/` (operation facts belong in `docs/operations/*.rst`) and
  point a skill at it.
- **Missing or vague `description`** — "Helps with stages" tells the dispatcher
  nothing. Say what it does and when to use it.
- **No "What this skill covers" block**, or one that omits later sections — the
  index promise ("scan one block to know what's where") breaks.
- **Not registered in `README.md`** — the skill won't surface in the
  per-session index and effectively doesn't exist for routing.
- **Duplicated content** — copying a parameter table or invocation snippet that
  already lives in a reference doc. Link instead; copies drift.
- **Repeating the tool-translation table** — it lives once in the root
  `AGENTS.md`; just use canonical tool names.

---

## Troubleshooting

| Symptom | Cause | Fix |
|---|---|---|
| The skill never fires for an obviously matching request | `description` is vague, or describes the topic rather than the trigger | Rewrite it as "does X; use when Y" (Step 2); the dispatcher reads only the frontmatter. |
| Two skills both fire for the same request | Overlapping `description` scope | Narrow the weaker one and cross-link the pair, as `run-operations` and `config-presets` do. |
| An agent follows the skill but misses a later section | The "What this skill covers" block omits it | Add the section to the block; agents scan it to decide how far to read. |
| The skill is correct but nobody finds it | Not registered in `README.md` | Complete Step 4. |
| A documented command fails for a reader but works for you | The command is source-tree-only and the reader is in a drop or the public mirror | State which surface it needs; `tools/ci`, `source/` and `repo.bat` are not present everywhere. |

## Purpose

Author a new skill under `.agents/skills/<name>/`, or restructure an existing
one, so it is discoverable by the dispatcher, readable end to end by an agent,
and consistent with the skills already in this repo.

## Prerequisites

- A task worth a skill rather than a doc page — see "When to write a skill".
- Write access to `.agents/skills/` and `.agents/skills/README.md`.
- A nearby skill to model: `run-operations` for a workflow, `config-presets`
  for a chooser, `new-operation` for a scaffold.

## Limitations

- Covers authoring only. It does not run, evaluate, or score skills.
- It cannot verify that what a skill claims about the product is true. Every
  statement about shipped behaviour has to be checked against the build; this
  is the most common source of defects in shipped skill text.
- Frontmatter keys beyond the reference above are passed through untouched;
  this skill does not validate them.
