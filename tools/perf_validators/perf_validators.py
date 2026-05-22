"""Run Scene Optimizer performance validators on a USD asset and diff runs.

Two subcommands:

  run <asset> [--csv path] [--json path] [--summary path] [--fix path]
      Open the stage, run all default Scene Optimizer validator rules, print a
      by-severity / by-rule summary, and optionally write per-issue CSV, the
      full asset-validator JSON, or a small summary JSON suitable for compare.
      When --fix is provided, run IssueFixer on the Scene Optimizer issues and
      write the fixed stage to the given USD path.

  compare <a.json> <b.json>
      Diff two summary JSONs (produced by `run --summary`) by rule count and
      total. Use this to see how a change in operation parameters or a stage
      edit shifted the issue distribution.

This script must run under the build's bundled Python with the right
LD_LIBRARY_PATH / PYTHONPATH set. Use the `run.sh` wrapper next to it.
"""

import argparse
import json
import sys
import time
from collections import Counter
from pathlib import Path


def _status_name(status) -> str:
    return str(status).split(".")[-1]


def _run(args: argparse.Namespace) -> int:
    from omni.asset_validator import IssueCSVData, IssueFixer, ValidationEngine, export_json_file
    import omni.scene.optimizer.validators
    from pxr import Usd

    print(f"Opening: {args.asset}", flush=True)
    t0 = time.time()
    stage = Usd.Stage.Open(args.asset)
    open_secs = time.time() - t0
    if stage is None:
        print(f"error: failed to open stage: {args.asset}", file=sys.stderr)
        return 1
    print(f"  opened in {open_secs:.1f}s", flush=True)

    print("Running ValidationEngine().validate(stage) ...", flush=True)
    t0 = time.time()
    results = ValidationEngine().validate(stage)
    validate_secs = time.time() - t0
    print(f"  finished in {validate_secs:.1f}s", flush=True)

    issues = [
        i
        for i in results.issues()
        if i.rule and i.rule.__module__.startswith("omni.scene.optimizer.validators")
    ]
    print(f"\n=== {len(issues)} Scene-Optimizer issues ===")

    by_rule: Counter = Counter(i.rule.__name__ for i in issues)
    by_severity: Counter = Counter(str(i.severity).split(".")[-1] for i in issues)

    print("\nBy severity:")
    for sev, count in by_severity.most_common():
        print(f"  {sev:10s}  {count}")

    print("\nBy rule:")
    for rule, count in sorted(by_rule.items()):
        print(f"  {rule:55s}  {count}")

    fix_summary = None
    if args.fix:
        fixed_path = Path(args.fix).resolve()
        fixed_path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)

        print("\nRunning IssueFixer on Scene-Optimizer issues ...", flush=True)
        t0 = time.time()
        fix_results = IssueFixer(stage).fix(issues)
        fix_secs = time.time() - t0

        by_fix_status: Counter = Counter(_status_name(r.status) for r in fix_results)
        print(f"  finished in {fix_secs:.1f}s", flush=True)
        print("\nBy fix status:")
        if by_fix_status:
            for status, count in sorted(by_fix_status.items()):
                print(f"  {status:16s}  {count}")
        else:
            print("  (no issues to fix)")

        not_fixed = [r for r in fix_results if _status_name(r.status) != "SUCCESS"]
        if not_fixed:
            print("\nUnfixed issues:")
            for result in not_fixed[:10]:
                rule = result.issue.rule.__name__ if result.issue.rule else "(unknown rule)"
                status = _status_name(result.status)
                exception = f": {result.exception}" if result.exception else ""
                print(f"  {status:16s}  {rule}: {result.issue.message}{exception}")
            if len(not_fixed) > 10:
                print(f"  ... {len(not_fixed) - 10} more")

        print(f"\nWriting fixed stage: {fixed_path}", flush=True)
        t0 = time.time()
        export_ok = stage.Export(str(fixed_path))
        if export_ok is False:
            print(f"error: failed to write fixed stage: {fixed_path}", file=sys.stderr)
            return 1
        save_secs = time.time() - t0
        print(f"  wrote in {save_secs:.1f}s", flush=True)

        fix_summary = {
            "output": str(fixed_path),
            "fix_secs": round(fix_secs, 2),
            "save_secs": round(save_secs, 2),
            "by_status": dict(by_fix_status),
        }

    if args.csv:
        IssueCSVData.from_(results).export_csv(args.csv)
        print(f"\nWrote CSV: {args.csv}")
    if args.json:
        export_json_file(args.json, [results])
        print(f"Wrote JSON: {args.json}")
    if args.summary:
        summary = {
            "asset": args.asset,
            "open_secs": round(open_secs, 2),
            "validate_secs": round(validate_secs, 2),
            "total": len(issues),
            "by_severity": dict(by_severity),
            "by_rule": dict(by_rule),
        }
        if fix_summary:
            summary["fix"] = fix_summary
        Path(args.summary).write_text(json.dumps(summary, indent=2, sort_keys=True))
        print(f"Wrote summary: {args.summary}")

    return 0


def _compare(args: argparse.Namespace) -> int:
    a = json.loads(Path(args.a).read_text())
    b = json.loads(Path(args.b).read_text())

    a_rules: dict = a.get("by_rule", {})
    b_rules: dict = b.get("by_rule", {})
    rules = sorted(set(a_rules) | set(b_rules))

    print(f"A: {a.get('asset', args.a)}")
    print(f"B: {b.get('asset', args.b)}")
    print()
    print(f"{'Rule':55s}  {'A':>8s}  {'B':>8s}  {'delta':>8s}")
    print("-" * 85)
    for rule in rules:
        ca = a_rules.get(rule, 0)
        cb = b_rules.get(rule, 0)
        delta = cb - ca
        print(f"{rule:55s}  {ca:>8d}  {cb:>8d}  {delta:>+8d}")

    ta = a.get("total", sum(a_rules.values()))
    tb = b.get("total", sum(b_rules.values()))
    print()
    print(f"Total: A={ta}, B={tb}, delta={tb - ta:+d}")

    va = a.get("validate_secs")
    vb = b.get("validate_secs")
    if va is not None and vb is not None:
        print(f"Validate time: A={va}s, B={vb}s, delta={vb - va:+.1f}s")

    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run Scene Optimizer performance validators on a USD asset.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_run = sub.add_parser("run", help="run validators on an asset")
    p_run.add_argument("asset", help="path to a .usd / .usda / .usdc / .usdz")
    p_run.add_argument("--csv", help="write per-issue CSV here")
    p_run.add_argument("--json", help="write full asset-validator JSON here")
    p_run.add_argument(
        "--summary",
        help="write small summary JSON here (input format for `compare`)",
    )
    p_run.add_argument(
        "--fix",
        metavar="PATH",
        help="run IssueFixer on Scene Optimizer issues and write the fixed stage here",
    )
    p_cmp = sub.add_parser("compare", help="diff two summary JSONs")
    p_cmp.add_argument("a", help="summary JSON from an earlier run")
    p_cmp.add_argument("b", help="summary JSON from a later run")

    args = parser.parse_args()
    if args.cmd == "run":
        return _run(args)
    if args.cmd == "compare":
        return _compare(args)
    return 2


if __name__ == "__main__":
    sys.exit(main())
