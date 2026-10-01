# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Wrapper that registers the Usd-Optimize validators, then delegates to the
``usd_validation_nvidia`` CLI. Two behaviours are layered on the upstream CLI:

- ``--verbose`` / ``-v``: make count-only rules also emit one issue per failing
  prim (equivalently ``USD_OPTIMIZE_VALIDATOR_VERBOSE=1`` or ``--parameter
  VERBOSE=true``). This is upstream's own logging-verbosity flag, so it is read
  here but deliberately *not* consumed: upstream still raises its log level, and
  one flag gives both more logging and more granular findings.
- Non-destructive ``--fix``: opt-in and, by default, writes fixes to a new file
  (``--fix-output <path>`` or ``<stem>.fixed<ext>``) by copying the source and
  pointing the upstream in-place fixer at the copy. ``--fix-in-place`` (or
  ``USD_OPTIMIZE_FIX_IN_PLACE=1``) mutates the source instead. An existing
  destination is never replaced unless ``--fix-overwrite`` says so.

The guards below all fail closed with exit 2. The rule they share: this wrapper
does not quietly decide things about the user's filesystem -- it either does the
safe thing or stops and says why.
"""

import os
import shutil
import sys

_USD_EXTS = (".usd", ".usda", ".usdc", ".usdz")

# Upstream flags that consume the next token as their value, so a flag *value* is
# never mistaken for the asset positional (keep in sync with run-validators/SKILL.md).
_VALUE_FLAGS = frozenset({"--csv-output", "--json-output", "--group-by", "--parameter", "-r", "-D", "-c", "-p"})


def _err(msg):
    print(f"error: {msg}", file=sys.stderr)
    return 2


def _env_truthy(value):
    return str(value).strip().lower() in {"1", "true", "yes", "on"}


def _find_assets(argv):
    """Return the ``[(index, token), ...]`` ``.usd*`` positionals in *argv*.

    Skips options and the values consumed by ``_VALUE_FLAGS``. A well-formed
    command yields exactly one candidate; the caller rejects zero or >1 rather
    than guess, so a future ``.usd*``-valued flag surfaces as ambiguity instead
    of being copied in place of the real asset.
    """
    candidates, i = [], 0
    while i < len(argv):
        tok = argv[i]
        if tok in _VALUE_FLAGS:
            i += 2
        elif tok.startswith("-"):
            i += 1
        else:
            if tok.lower().endswith(_USD_EXTS):
                candidates.append((i, tok))
            i += 1
    return candidates


def _is_verbose_flag(tok):
    """True for upstream's verbosity flag in any spelling: ``--verbose``, ``-v``, ``-vv``...

    Upstream defines it as a repeatable count (``-v`` INFO, ``-vv`` DEBUG), so
    every spelling has to be recognized or the short and long forms would mean
    different things here than they do upstream.
    """
    return tok == "--verbose" or (len(tok) > 1 and tok[0] == "-" and set(tok[1:]) == {"v"})


def _default_fixed_path(asset):
    root, ext = os.path.splitext(asset)
    return f"{root}.fixed{ext}"


def _discard_fix_copy(out):
    """Remove a ``--fix`` destination we created when the run failed before saving."""
    try:
        os.remove(out)
    except OSError:
        return
    print(
        f"error: validation failed before any fixes were saved; removed the unfixed copy at {out}",
        file=sys.stderr,
    )


def main() -> int:
    import usd_optimize.validators
    from usd_validation_nvidia import cli_main

    usd_optimize.validators.register_all()
    argv = list(sys.argv[1:])

    # Verbosity is upstream's flag; read it without consuming it so it keeps
    # raising upstream's log level as well as turning on per-prim emission.
    if any(_is_verbose_flag(a) for a in argv):
        usd_optimize.validators.set_verbose(True)

    fixing = "--fix" in argv or "-f" in argv

    # Strip our fix flags (upstream does not know them).
    in_place_flag = "--fix-in-place" in argv
    if in_place_flag:
        argv = [a for a in argv if a != "--fix-in-place"]
    overwrite = "--fix-overwrite" in argv
    if overwrite:
        argv = [a for a in argv if a != "--fix-overwrite"]
    fix_output = None
    if "--fix-output" in argv:
        idx = argv.index("--fix-output")
        if idx + 1 >= len(argv):
            return _err("--fix-output requires a path argument")
        fix_output = argv[idx + 1]
        del argv[idx : idx + 2]

    # Reject misused combinations loudly rather than ignoring them silently.
    if fix_output is not None and not fixing:
        return _err("--fix-output requires --fix")
    if in_place_flag and not fixing:
        return _err("--fix-in-place requires --fix")
    if overwrite and not fixing:
        return _err("--fix-overwrite requires --fix")
    if fix_output is not None and in_place_flag:
        return _err("--fix-output cannot be combined with --fix-in-place")

    # In-place if explicitly asked; an explicit --fix-output overrides the env default.
    if in_place_flag:
        in_place = True
    elif fix_output is not None:
        in_place = False
    else:
        in_place = _env_truthy(os.environ.get("USD_OPTIMIZE_FIX_IN_PLACE"))

    # Resolved mode, not the flag: in-place also arrives from the environment, where
    # --fix-overwrite would otherwise be ignored while the source was mutated.
    if overwrite and in_place:
        return _err(
            "--fix-overwrite cannot be combined with in-place fixing (--fix-in-place or "
            "USD_OPTIMIZE_FIX_IN_PLACE=1), which writes to the source and has no destination file"
        )

    fix_copy = None
    if fixing and not in_place:
        assets = _find_assets(argv)
        if not assets:
            return _err(
                "--fix requested but no .usd/.usda/.usdc/.usdz asset was found to "
                "protect; pass an asset, or use --fix-in-place to modify it directly"
            )
        if len(assets) > 1:
            found = ", ".join(tok for _, tok in assets)
            return _err(
                f"--fix could not identify a single source asset (candidates: {found}); "
                "re-run with one asset, or use --fix-in-place"
            )
        asset_idx, asset = assets[0]

        out = fix_output or _default_fixed_path(asset)
        # An existing-directory --fix-output means <dir>/<basename>; resolve it so the
        # collision guard and argv redirect use the real file, not the directory.
        if os.path.isdir(out):
            out = os.path.join(out, os.path.basename(asset))
        # realpath (not abspath) so a symlink --fix-output pointing at the source
        # is caught here instead of raising SameFileError inside shutil.copy2.
        if os.path.realpath(out) == os.path.realpath(asset):
            return _err(
                "--fix-output resolves to the source asset; choose another path or "
                "pass --fix-in-place to overwrite it deliberately"
            )
        # Source first, so a refusal leaves no destination behind. os.access is False for a
        # missing path too, so name that case rather than blaming permissions.
        if not os.path.exists(asset):
            return _err(f"--fix source does not exist: {asset}")
        # copy2 preserves mode: a read-only source yields a copy upstream cannot save into,
        # while still reporting the fix as applied.
        if not os.access(asset, os.W_OK):
            return _err(
                f"--fix source is read-only: {asset}; the copy would inherit that and the "
                "fix could not be saved. Make the source writable and re-run"
            )

        os.makedirs(os.path.dirname(os.path.abspath(out)), exist_ok=True)
        # O_EXCL also reserves the name; a bare exists() check lets a racing run erase us.
        reserved = False
        if not overwrite:
            try:
                os.close(os.open(out, os.O_CREAT | os.O_EXCL | os.O_WRONLY))
                reserved = True
            except FileExistsError:
                return _err(
                    f"--fix destination already exists and would be overwritten: {out}; "
                    "choose another --fix-output, remove it, or pass --fix-overwrite"
                )
        try:
            shutil.copy2(asset, out)
        except OSError:
            if reserved:
                os.remove(out)  # no empty placeholder blocking the re-run
            raise
        fix_copy = out
        argv[asset_idx] = out  # redirect the in-place fixer at the copy; source preserved
        print(f"[run-validators] --fix writing fixes to a new file: {out}")
        print(f"[run-validators] original left untouched: {asset}")
    elif fixing and in_place:
        assets = _find_assets(argv)
        target = assets[0][1] if assets else "the input asset"
        print(f"[run-validators] --fix-in-place: modifying {target} directly (no backup made)")

    sys.argv = [sys.argv[0], *argv]
    try:
        return cli_main()
    except Exception:
        # Not SystemExit: upstream raises that for "issues found", a completed run.
        if fix_copy is not None:
            _discard_fix_copy(fix_copy)
        raise


if __name__ == "__main__":
    sys.exit(main())
