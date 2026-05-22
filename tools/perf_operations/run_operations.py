"""Run a chain of Scene Optimizer operations against a USD asset.

Two subcommands:

  run <asset> [--config <inline-json>] [--config-file <path>] [--pipeline <name>]
              [--output <path>] [--summary <path>]
      Open the stage, run the requested operation chain, save the optimized
      stage to <output> (defaults to <tmp>/scene-optimizer-operations/<sha1>/
      <asset_stem>.optimized.usdc), and print a per-operation summary. Pass
      --no-save to skip the save entirely.

  list-pipelines
      Print available named pipelines (keys from pipelines.json).

This script must run under the build's bundled Python with the right
LD_LIBRARY_PATH / PYTHONPATH set. Use the `run.sh` / `run.bat` wrapper next
to it.
"""

import argparse
import json
import sys
import time
from pathlib import Path

# Sibling pure-stdlib helper. Imported (not duplicated) so the default
# output path and the supported-extension list have a single source of truth.
from resolve_output import SUPPORTED_USD_EXTENSIONS, default_output_path

_PIPELINES_FILE = Path(__file__).resolve().parent / "pipelines.json"


def _load_pipelines() -> dict:
    """Load named pipeline definitions from pipelines.json (drops _comment keys)."""
    if not _PIPELINES_FILE.is_file():
        return {}
    data = json.loads(_PIPELINES_FILE.read_text())
    return {k: v for k, v in data.items() if not k.startswith("_")}


def _resolve_config(args: argparse.Namespace) -> list:
    """Pick a config source from --config / --config-file / --pipeline.

    Returns a list of operation dicts. Errors out if nothing or multiple
    sources are specified.
    """
    sources = [
        ("--config", args.config),
        ("--config-file", args.config_file),
        ("--pipeline", args.pipeline),
    ]
    given = [(name, value) for name, value in sources if value]

    if len(given) == 0:
        raise SystemExit(
            "error: one of --config / --config-file / --pipeline is required. "
            "Run `list-pipelines` to see available named pipelines."
        )
    if len(given) > 1:
        names = ", ".join(name for name, _ in given)
        raise SystemExit(f"error: pass only one of: {names}")

    name, value = given[0]

    if name == "--config":
        # Inline JSON string.
        try:
            config = json.loads(value)
        except json.JSONDecodeError as e:
            raise SystemExit(f"error: --config is not valid JSON: {e}")
    elif name == "--config-file":
        path = Path(value)
        if not path.is_file():
            raise SystemExit(f"error: --config-file not found: {value}")
        try:
            config = json.loads(path.read_text())
        except json.JSONDecodeError as e:
            raise SystemExit(f"error: --config-file is not valid JSON: {e}")
    else:
        # --pipeline name
        pipelines = _load_pipelines()
        if value not in pipelines:
            available = ", ".join(sorted(pipelines)) or "(none)"
            raise SystemExit(
                f"error: unknown pipeline '{value}'. Available: {available}"
            )
        config = pipelines[value]

    if not isinstance(config, list):
        raise SystemExit("error: config must be a JSON list of operation dicts.")

    return config


def _run(args: argparse.Namespace) -> int:
    # Validate inputs (config + asset path) before importing the runtime so
    # quoting / typo errors surface without needing the bundled Python.
    config = _resolve_config(args)

    asset = Path(args.asset)
    if not asset.is_file():
        print(f"error: asset not found: {args.asset}", file=sys.stderr)
        return 1
    if asset.suffix.lower() not in SUPPORTED_USD_EXTENSIONS:
        print(
            f"error: not a USD file (must be one of {sorted(SUPPORTED_USD_EXTENSIONS)}): {args.asset}",
            file=sys.stderr,
        )
        return 1

    if args.no_save and args.output:
        print(
            "error: --no-save and --output are mutually exclusive",
            file=sys.stderr,
        )
        return 1

    # Resolve output path now (before importing the runtime) so we can fail
    # fast if the user passed something invalid.
    if args.no_save:
        output_path = None
    elif args.output:
        output_path = Path(args.output).resolve()
    else:
        output_path = default_output_path(str(asset.resolve()), asset.stem)

    # Lazy imports — only load the runtime once we know we're actually going
    # to execute. Keeps list-pipelines, --pipeline-typo, and --config-bad-json
    # paths usable outside the wrapper.
    from omni.scene.optimizer.core import ExecutionContext, SceneOptimizerCore
    from pxr import Usd

    print(f"Opening: {asset}", flush=True)
    t0 = time.time()
    stage = Usd.Stage.Open(str(asset))
    open_secs = time.time() - t0
    if stage is None:
        # Usd.Stage.Open returns None on failure rather than raising
        # (invalid path, corrupt layer, missing references, etc.). Surface
        # a clear error here instead of letting context.set_stage fail with
        # a cryptic C++ binding message.
        print(f"error: failed to open stage: {asset}", file=sys.stderr)
        return 1
    print(f"  opened in {open_secs:.1f}s", flush=True)

    context = ExecutionContext()
    context.set_stage(stage)
    if args.verbose:
        context.verbose = 1
    if args.capture_stats:
        context.captureStats = 1

    so_core = SceneOptimizerCore.getInstance()
    results = []
    total_t0 = time.time()
    for i, op_dict in enumerate(config):
        op_dict = dict(op_dict)  # don't mutate caller's data
        if "operation" not in op_dict:
            print(f"  [{i}] skip: entry has no 'operation' key", flush=True)
            results.append({"index": i, "operation": None, "success": False, "error": "no 'operation' key"})
            continue
        op_name = op_dict.pop("operation")

        if op_name == "executionContext":
            # In-line context overrides — mutate context, skip execution.
            for key, value in op_dict.items():
                if key in {"debug", "singleThreaded", "verbose", "generateReport", "captureStats"}:
                    setattr(context, key, 1 if value else 0)
            print(f"  [{i}] executionContext override: {op_dict}", flush=True)
            results.append({"index": i, "operation": op_name, "success": True, "error": None})
            continue

        print(f"  [{i}] running {op_name} ...", flush=True)
        op_t0 = time.time()
        success, error, output = so_core.executeOperation(op_name, context, op_dict)
        op_secs = time.time() - op_t0
        status = "OK" if success else "FAIL"
        print(f"  [{i}] {op_name}: {status} in {op_secs:.1f}s", flush=True)
        if not success:
            print(f"      error: {error}", flush=True)
        results.append(
            {
                "index": i,
                "operation": op_name,
                "success": bool(success),
                "error": error,
                "secs": round(op_secs, 2),
            }
        )

    total_secs = time.time() - total_t0

    # Save output (path was resolved up front).
    save_secs = None
    if output_path:
        output_path.parent.mkdir(parents=True, exist_ok=True, mode=0o700)
        t0 = time.time()
        stage.GetRootLayer().Export(str(output_path))
        save_secs = time.time() - t0
        print(f"\nSaved: {output_path} (in {save_secs:.1f}s)", flush=True)
    else:
        print("\n(--no-save: optimized stage discarded)", flush=True)

    # Headline summary.
    n_ok = sum(1 for r in results if r["success"])
    n_fail = sum(1 for r in results if not r["success"])
    print(f"\n=== {n_ok} succeeded, {n_fail} failed across {len(results)} operations in {total_secs:.1f}s ===")

    if args.summary:
        summary = {
            "asset": str(asset),
            "output": str(output_path) if output_path else None,
            "open_secs": round(open_secs, 2),
            "total_secs": round(total_secs, 2),
            "save_secs": round(save_secs, 2) if save_secs is not None else None,
            "config": config,
            "results": results,
        }
        Path(args.summary).parent.mkdir(parents=True, exist_ok=True)
        Path(args.summary).write_text(json.dumps(summary, indent=2, sort_keys=True))
        print(f"Wrote summary: {args.summary}")

    return 0 if n_fail == 0 else 1


def _list_pipelines(_args: argparse.Namespace) -> int:
    pipelines = _load_pipelines()
    if not pipelines:
        print("(no pipelines defined)")
        return 0
    print("Available pipelines (use with --pipeline <name>):\n")
    for name in sorted(pipelines):
        ops = [step.get("operation", "?") for step in pipelines[name]]
        print(f"  {name}")
        print(f"      {' -> '.join(ops)}")
    print("\nFor rationale and parameter guidance, see .agents/operations/PIPELINES.md.")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Run a chain of Scene Optimizer operations against a USD asset.",
        formatter_class=argparse.RawDescriptionHelpFormatter,
        epilog=__doc__,
    )
    sub = parser.add_subparsers(dest="cmd", required=True)

    p_run = sub.add_parser("run", help="run an operation chain on an asset")
    p_run.add_argument("asset", help="path to a .usd / .usda / .usdc / .usdz")
    src = p_run.add_mutually_exclusive_group()  # for --help readability; _resolve_config also enforces
    src.add_argument(
        "--config",
        help="inline JSON list of operation dicts, e.g. '[{\"operation\":\"meshCleanup\"}]'",
    )
    src.add_argument(
        "--config-file",
        help="path to a JSON file containing a list of operation dicts",
    )
    src.add_argument(
        "--pipeline",
        help="named pipeline from pipelines.json (run `list-pipelines` to see options)",
    )
    p_run.add_argument(
        "--output",
        help=(
            "output USD path. If omitted, defaults to "
            "<tmp>/scene-optimizer-operations/<sha1>/<asset_stem>.optimized.usdc. "
            "Pass --no-save to skip the save instead."
        ),
    )
    p_run.add_argument(
        "--no-save",
        action="store_true",
        help="run operations without writing the optimized stage to disk",
    )
    p_run.add_argument(
        "--summary",
        help="write a per-op summary JSON here",
    )
    p_run.add_argument("--verbose", action="store_true", help="set ExecutionContext.verbose=1")
    p_run.add_argument(
        "--capture-stats",
        action="store_true",
        help="set ExecutionContext.captureStats=1",
    )

    sub.add_parser("list-pipelines", help="print available named pipelines")

    args = parser.parse_args()
    if args.cmd == "run":
        return _run(args)
    if args.cmd == "list-pipelines":
        return _list_pipelines(args)
    return 2


if __name__ == "__main__":
    sys.exit(main())
