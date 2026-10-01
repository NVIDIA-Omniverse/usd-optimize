---
name: run-validators
description: Validate a USD asset with Usd Optimize's performance validators and auto-apply fixes with --fix. Use when checking or repairing a USD.
allowed-tools: Bash
metadata:
  author: NVIDIA Corporation
  version: "4.1.1"
  tags: [usd, validation, performance]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# run-validators — Validate and auto-fix a USD asset

Runs the `usd-validation-nvidia` engine over a USD stage through
`tools/validators/run.sh` (POSIX) or `run.bat` (Windows). The wrapper
registers the `UsdOptimize*` performance rules and then delegates to the
official `usd_validation_nvidia` CLI entry point, so one run covers both
the Usd Optimize rules and the base `usd_validation_nvidia` rules.

The validators are reference-documented in
[`docs/performance-validators.rst`](../../../docs/performance-validators.rst) —
that is the source of truth for the rule list and what each rule checks. Many
rules ship a fix that `usd-validation-nvidia`'s `IssueFixer` can apply
automatically; pass `--fix` to enable it.

## What this skill covers

- **Usage** — flags and positional args.
- **Step 1** — validate the input path.
- **Step 2** — invoke the driver, including `--process` for parallel validation on large scenes.
- **Step 3** — summarize results and hand off.
- **Errors to handle** — failure modes.
- **Auto-fix model** — how `IssueFixer` / `--fix` applies fixes, where it writes, and what it cannot.
- **Programmatic invocation** — `register_all()` + `ValidationEngine`.
- **CLI invocation** — `nvidia_usd_validate` setup, the wheel/entry-point requirement, and the `libusd` alignment gotcha.
- **Adding a new validator** — pointer to the `new-validator` skill.

Companion skills: `interpret-validators` (read the report and decide what to do
about issues that could **not** be auto-fixed), `run-operations` (run
operations / preset configs when a manual fix is needed).

---

## Usage

One positional argument (the asset) plus optional flags:

| Flag | Meaning |
|---|---|
| `ASSET` | Required. `.usd` / `.usda` / `.usdc` / `.usdz`, or a folder of them (read-only runs only — see Limitations). |
| `-f` / `--fix` | **Opt-in.** Run `IssueFixer` on every fixable issue. Non-destructive by default: writes fixes to a **new file** (`<stem>.fixed<ext>`, or `--fix-output`); the source is untouched unless `--fix-in-place` (or `USD_OPTIMIZE_FIX_IN_PLACE=1`) is set. |
| `--fix-output <PATH>` | Where `--fix` writes the fixed stage (default `<stem>.fixed<ext>` beside the source). **Only the root layer is copied**, so a path outside the source's directory leaves relative sublayers, references and payloads pointing at their old locations — keep it beside the source unless the asset is self-contained. A relative `<PATH>` resolves against the **current directory**, not the asset's. |
| `--fix-in-place` | Make `--fix` overwrite the **source** (destructive — confirm first). Env: `USD_OPTIMIZE_FIX_IN_PLACE=1`. |
| `--fix-overwrite` | Allow `--fix` to replace an existing output file. Without it an existing destination is refused (exit 2) rather than overwritten, so a second `--fix` run over the same default path is an error until the first result is moved or removed. Rejected with `--fix-in-place`, which has no destination. |
| `--csv-output <CSV>` | Write a per-issue CSV (all rules). |
| `--json-output <JSON>` | Write the full validation result as JSON (`rules[].issues[]`). Carries the same issues as the CSV; `summarize_csv.py` reads either. |
| `-r RULE` | Enable only a specific rule (repeatable). |
| `-D RULE` | Disable a specific rule (repeatable). |
| `-c CATEGORY` | Enable only a specific category (repeatable). |
| `--parameter NAME=VALUE` | Override a rule parameter (repeatable). |
| `-v` / `--verbose` | Upstream's verbosity flag. Raises upstream's log level **and** makes count-only rules emit one issue per failing prim (see below). Repeatable — `-vv` for DEBUG logging. |
| `-p PREDICATE` | Filter output by severity: `IsError`, `IsFailure`, `IsWarning`. |
| `--group-by requirement\|rule_name` | Group output by requirement or rule. |

Run `tools/validators/run.sh --help` for the full flag list. `-v` / `--verbose`
is upstream's own flag: the wrapper reads it *without* consuming it, so one flag
raises upstream's log level and turns on per-prim reporting.

### Verbose per-prim reporting

By default several rules report a single aggregate issue with a count (e.g.
"Found 2 nonManifold meshes to fix") anchored at the stage root, so the per-prim
paths never reach the CSV `Location` column. Verbose mode makes those rules also
emit one issue per failing prim (full path in `Location`), which is what you want
for CLI/batch debugging. The aggregate summary is still emitted.

Three ways to enable it, all read at validation time so they work no
matter how the rules are driven (our wrapper, the upstream CLI, or a direct
`ValidationEngine`):

- `tools/validators/run.{sh,bat} asset.usd -v` (or `--verbose`) — this is
  upstream's verbosity flag, so it also raises upstream's log level to INFO
  (`-vv` for DEBUG). The other two forms change reporting only.
- `USD_OPTIMIZE_VALIDATOR_VERBOSE=1` in the environment
- `--parameter VERBOSE=true` (or `--parameter <RuleName>.VERBOSE=true`),
  advertised through the standard usd-validation-nvidia parameter system; or
  programmatically `usd_optimize.validators.set_verbose(True)`.

Rules that already emit per-prim issues (e.g. `SmallMeshChecker`,
`InvisiblePrimsChecker`, `WindingsChecker`) are unaffected by the flag.

If no path is provided, ask:
> "Which USD file should I validate? Provide the full path."

## Step 1 — Validate the input

Reject a path that does not exist or is not a `.usd*` file before doing
anything else.

## Step 2 — Invoke the driver

Use the wrapper for the host OS. It sets `LD_LIBRARY_PATH` / `PATH` and
`PYTHONPATH` for the build's bundled Python, registers the Usd Optimize
rules, and delegates to the `usd_validation_nvidia` CLI. **Always go through
the wrapper** — invoking the bundled interpreter directly inherits the user's
`PYTHONPATH` and fails with USD-version mismatches.

**Validation is read-only by default — add `--fix` only when the user asks for
repairs.** `--fix` is non-destructive: it writes fixes to a **new file**
(`<stem>.fixed<ext>`, or `--fix-output`) and leaves the source untouched. Pass
`--csv-output` to save a per-issue CSV for later analysis with
`interpret-validators`.

**Get the artifact directory from `resolve_artifacts.py` — don't invent one.**
The driver has no default `--csv-output`, so artifacts land wherever you put
them. `interpret-validators` looks them up at a path derived from the asset, so
writing anywhere else makes its replay lookup report `missing` and every asset
re-validates from scratch. Pass `--logs-dir` to both scripts if the user wants a
specific location.

POSIX (validate read-only, then opt into `--fix` to repair into a new file):

```bash
ASSET="<path/to/asset.usd>"

ARTIFACT_DIR=$(python3 tools/validators/resolve_artifacts.py "$ASSET" \
    | python3 -c 'import json,sys; print(json.load(sys.stdin)["artifact_dir"])')
CSV="$ARTIFACT_DIR/issues.csv"
mkdir -p "$ARTIFACT_DIR"

# validate (read-only)
tools/validators/run.sh "$ASSET" --csv-output "$CSV" > "$ARTIFACT_DIR/run.log" 2>&1

# to repair: --fix writes fixes to a new file (<stem>.fixed<ext>); $ASSET is kept
tools/validators/run.sh "$ASSET" --fix --csv-output "$CSV" >> "$ARTIFACT_DIR/run.log" 2>&1
```

Windows (PowerShell):

```powershell
$Asset = "<path\to\asset.usd>"

$ArtifactDir = (& py -3 tools\validators\resolve_artifacts.py $Asset | ConvertFrom-Json).artifact_dir
$Csv = Join-Path $ArtifactDir "issues.csv"
New-Item -ItemType Directory -Force -Path $ArtifactDir | Out-Null

# validate (read-only)
& tools\validators\run.bat $Asset --csv-output $Csv *> (Join-Path $ArtifactDir "run.log")

# to repair: --fix writes fixes to a new file (<stem>.fixed<ext>); $Asset is kept
& tools\validators\run.bat $Asset --fix --csv-output $Csv *>> (Join-Path $ArtifactDir "run.log")
```

Redirect to a log file rather than piping through `tail` (pipes buffer until
the process exits).

**Output location:** `--fix` writes to a new file by default — `--fix-output
<path>`, or `<stem>.fixed<ext>` beside the source — so the original is preserved.
`--fix-in-place` (or `USD_OPTIMIZE_FIX_IN_PLACE=1`) overwrites the source
instead; it's destructive, so confirm with the user before using it.

The **destination** is protected too: if it already exists the run stops with
exit 2 before copying anything, because the copy happens up front and would
otherwise destroy that file even on a run that fixes nothing. Move or remove the
old output, pick a different `--fix-output`, or pass `--fix-overwrite` to replace
it deliberately. Only the root layer is copied — see the `--fix-output` row in
Usage before sending output to another directory.

If the run itself fails before any fixes are saved, the copy is removed rather
than left behind, so a `<stem>.fixed<ext>` that exists is one the fixer actually
wrote. A run that completes and still reports issues is not a failure: its output
is kept, because `--fix` cannot repair every rule.

### Long-running execution

Validation on real assets takes minutes (occlusion and overlap checks are the
slowest). Launch the driver as a long-running command and return control:

- **Claude Code**: set `run_in_background: true` on the Bash call.
- **Generic shells**: `nohup ... &` (POSIX) or `Start-Process ... -NoNewWindow`
  (PowerShell), then read bounded snapshots of `run.log`.

Tell the user it is running and that you will report results when done.
For a status snapshot: `tail -n 80 "<artifact_dir>/run.log"`.

### Parallel validation on large scenes (`--process`)

`usd-validation-nvidia` runs rules across a process pool when given
`--process COUNT`; the driver passes unknown flags straight through, so it
works with no extra tooling:

```bash
tools/validators/run.sh "$ASSET" --process 4 \
    --no-defaultRules -c Usd:Performance -c Omni:Geometry --csv-output "$CSV"
```

**Start at 4 and only raise it if you have headroom to spare.** Each worker opens
its own copy of the stage, so memory — not core count — is the binding constraint,
and upstream caps workers with a flat count that knows nothing about available RAM.
Worker count saturates early, partly because the engine is already internally
threaded, so past ~4 the extra copies rarely pay for themselves.

Only rules marked `@multiprocess_safe` are pooled — every Usd Optimize rule is,
via `BaseUsdOptimizeChecker.CheckStage`. Unmarked rules still run inline, so
coverage is unchanged; the scope flags above are what make the pooling pay off.

**Expect a modest win, and always pair `--process` with a scope flag.** The
speedup is set by how much of the enabled rule set is poolable: the Usd Optimize
rules are all marked, but almost none of the base rules are, so a default run
leaves the base rules — which dominate the wall clock — inline and gains close to
nothing. It also varies by stage, since one long-pole rule cannot be subdivided
across workers. Findings are identical either way.

> `--process` is upstream-**experimental** and defaults to `0` (disabled).
> Combining it with `--fix` is untested — `IssueFixer` mutates a single
> in-memory stage, so use the serial path when you need a repaired asset.
>
> **On Linux the pool forks** — upstream omits `mp_context`, so the platform
> default applies (fork on Linux, spawn on Windows). If the parent process has
> already initialized CUDA, a forked worker inherits a dead context and GPU-backed
> rules fail with `CUDA error: initialization error`, surfacing as FAILURE issues
> instead of findings. `isCudaAvailable()` caches behind `std::call_once`, so the
> worker returns the parent's stale verdict and the CPU fallback never fires.
> Verified on `findOverlappingMeshes`; rules with the same guard (e.g.
> `deduplicateGeometry`) are exposed the same way. A plain one-shot CLI run does
> not trip it, because the parent never touches CUDA before forking — but any
> process that does GPU work and then validates with `--process` will.
>
> Spawned workers re-import the rule modules and see them as written, so rule
> tuning applied by assigning to a class attribute in the driving process never
> reaches them (verified: a threshold lowered in-process gives 4 issues inline, 1
> pooled). `--verbose` works that way — do not rely on it under `--process`.

## Step 3 — Summarize and hand off

When the run finishes, show the last ~40 lines of `run.log` (the per-rule issue
counts and the severity summary) and append:

```
Validation complete.
  Artifacts: <artifact_dir>/
    issues.csv  — every issue, one row per rule hit
    run.log     — full driver output

<N> issues found. <M> were auto-fixed (--fix).
```

**Where those two numbers come from.** The driver prints **no aggregate fix
line**, and the `Summary per Severity:` block at the end of the log is the
**pre-fix** count — byte-identical whether or not `--fix` ran. So take `<N>`
from that summary, and get `<M>` by counting the per-issue statuses the fixer
logs individually as it works (`<message>........FixStatus.SUCCESS`):

```bash
grep -c 'FixStatus.SUCCESS' "<artifact_dir>/run.log"
```

**Most issues are fixed automatically.** What remains needs a decision
(which prims, how aggressive, an accepted trade-off). For those, hand off:

> For the remaining issues, run `/interpret-validators <asset>` — they
> typically need a decision before an operation or preset config can address
> them.

If `--fix` was used, re-validate the file the fixes landed in — the
`--fix-output` path (`<stem>.fixed<ext>` by default), or the source itself for
`--fix-in-place` — to see what the targeted rules report now. Expect them to
drop, but **the total can rise**: a fix changes the stage, and the rewritten
geometry can legitimately trip rules that had nothing to report before. A higher
count is a result to read, not a sign the run failed:

```bash
tools/validators/run.sh "<the file you fixed>" --csv-output "$CSV"
```

Don't interpret the remaining issues here — that's `interpret-validators`.

## Errors to handle

| Symptom | Cause | What to tell the user |
|---|---|---|
| `Build not found at _build/...` | The repo isn't built | Point at the `build` skill; build first. |
| Driver exits **1** and the log ends with a `Summary per Severity:` block | **Normal** — the run completed and found at least one issue. Warnings alone are enough; only a genuinely clean asset exits 0. | Not a failure. The CSV is complete — parse it as usual. |
| Driver exits **2** | **Nothing was validated.** Either a wrapper flag guard rejected the command — a misused flag combination, no single usable asset, a missing or read-only source, or an existing output file — or the `usd-validation-nvidia` install failed | The `error:` line on stderr names the problem. For a command problem, fix it and re-run; for a failed install, check network or proxy access (`HTTPS_PROXY`). Nothing was written, so there is no partial output to clean up. **Do not parse this as a result** — exit 1 is the "validated, found issues" case. |
| Driver exits non-zero with **no** severity summary in the log | USD open error or plugin import error | Surface the last lines of `run.log`; don't parse a partial CSV. |
| `usd-validation-nvidia` install fails in the wrapper | First-run pip install behind a proxy | Set `HTTPS_PROXY` / `HTTP_PROXY` and re-run. |
| 0 Usd Optimize issues but base-rule issues present | The asset has no `UsdGeomMesh` content (references-only / materials-library / layout stage), so mesh rules find nothing | Expected — not a registration failure. Confirm with `inspect-asset` if unsure. |

If the repo isn't built, **don't build it yourself** — point at the `build`
skill and stop.

---

## Auto-fix model

Each rule reports issues; many also attach a **suggestion** describing the
operation that would fix the issue. `usd-validation-nvidia`'s `IssueFixer`
applies those suggestions when the user opts into `--fix`. When opted in, most
issues are resolved without any manual step.

What `IssueFixer` **cannot** resolve automatically is any issue whose fix needs
a decision — which prims to act on, how aggressive to be, or a trade-off the
user must accept (e.g. decimation amount, whether to merge instanced meshes).
Those remain after `--fix` and are the input to `interpret-validators`.

## Programmatic invocation

```python
from usd_validation_nvidia import ValidationEngine, IssueFixer
from usd_optimize.validators import register_all
from pxr import Usd

register_all()                         # register every Usd Optimize rule

stage = Usd.Stage.Open("asset.usd")
results = ValidationEngine().validate(stage)

# example of printing the issues
for issue in results.issues():
    print(issue.severity, issue.rule.__name__, issue.message)

# Apply the auto-fixable suggestions
fix_results = IssueFixer(stage).fix(results.issues())
stage.Export("asset.fixed.usdc")
```

`register_all()` is idempotent.

## CLI invocation (`nvidia_usd_validate`)

The upstream `nvidia_usd_validate` CLI can also drive the rules, but it
discovers plugins via `importlib.metadata` entry points, so **it only sees Usd
Optimize once the `usd-optimize` wheel is pip-installed** (a source-tree
`PYTHONPATH` registers no entry-point metadata). There is no allow-list env
var — once the wheel is installed the `registrant` entry point auto-loads.

```bash
./repo.sh build && ./repo.sh py_package        # produces _build/packages/usd_optimize-*.whl
python3.12 -m pip install _build/packages/usd_optimize-*.whl
nvidia_usd_validate asset.usd
nvidia_usd_validate --help | grep UsdOptimize   # verify rules are discovered
```

**`libusd` alignment gotcha.** `pxr` (often from `usd-exchange`) and Usd
Optimize's C++ core must bind the *same* `libusd` image, or `UsdUtilsStageCache`
splits and the validator errors with missing stage IDs. The `run.sh` wrapper
aligns `PYTHONPATH` / `LD_LIBRARY_PATH` automatically; the raw CLI does not. If
you hit stage-id / cache errors on a dev-tree CLI run, export the build's USD
first:

```bash
export PYTHONPATH=_build/$platform/release/python:_build/target-deps/usd/release/lib/python:$PYTHONPATH
export LD_LIBRARY_PATH=_build/$platform/release/lib:_build/$platform/release/extraLibs:$LD_LIBRARY_PATH
```

Note: the entry-point plugin registers the default rule set; to run a rule that
is not in the default set, register it programmatically with `register_all()`
first.

## Adding a new validator

See [`new-validator`](../new-validator/SKILL.md) for the full recipe.

## Prerequisites

- A built repo (`./repo.sh build` / `repo.bat build`) so the wrappers and
  bundled Python exist.
- A USD asset (`.usd` / `.usda` / `.usdc` / `.usdz`).
- Network access on first run (the wrapper pip-installs `usd-validation-nvidia`
  into the bundled Python).

## Limitations

- `--fix` is opt-in and writes to a new file by default (`--fix-in-place` to
  overwrite the source); it does not decide how to resolve issues that have no
  automatic fix — that's `interpret-validators`.
- **`--fix` needs a single file, not a folder.** Upstream accepts a folder as
  `ASSET` and read-only validation of one works, but `--fix` has to copy the
  source to protect it, so a folder exits 2 with `--fix requested but no
  .usd/.usda/.usdc/.usdz asset was found to protect`. Pass files individually,
  or use `--fix-in-place` on the folder.
- Occlusion / overlap rules can take tens of minutes to potential hours on
  large stages.
- The raw `nvidia_usd_validate` CLI needs the wheel installed and may need the
  `libusd` alignment exports above.
