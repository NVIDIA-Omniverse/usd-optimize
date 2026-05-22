"""Resolve the default output path for a USD asset under Scene Optimizer.

Pure-stdlib helper used by the run-operations skill so it doesn't have to
depend on POSIX-only utilities. Runs under any Python 3 on any OS.

Validates two things up front: that the asset exists, and that its
extension is one of `.usd / .usda / .usdc / .usdz`. Either failure prints
a single JSON object with an `error` key on stdout and exits non-zero.

Output (success) is a single JSON object on stdout:

    {
      "output_dir":   "/tmp/scene-optimizer-operations/<sha1>",
      "output":       "<output_dir>/<asset_stem>.optimized.usdc",
      "log":          "<output_dir>/run.log",
      "asset_abs":    "<absolute path to asset>"
    }

Usage:
    python tools/perf_operations/resolve_output.py <asset_path> [--output <path>]
"""

import argparse
import hashlib
import json
import sys
import tempfile
from pathlib import Path

SUPPORTED_USD_EXTENSIONS = {".usd", ".usda", ".usdc", ".usdz"}


def temp_root() -> Path:
    """Cross-platform writable temp directory."""
    return Path(tempfile.gettempdir())


def default_output_path(asset_abs: str, asset_stem: str) -> Path:
    """Default optimized-output path for a USD asset.

    Public so ``run_operations.py`` can produce the same default the helper
    advertises when ``--output`` is omitted. Both call sites must use this
    function so the two stay in sync.
    """
    h = hashlib.sha1(asset_abs.encode("utf-8")).hexdigest()
    return temp_root() / "scene-optimizer-operations" / h / f"{asset_stem}.optimized.usdc"


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Resolve output path for run-operations.")
    p.add_argument("asset")
    p.add_argument(
        "--output",
        default=None,
        help="Override the output USD path. If omitted, defaults to "
        "<tmp>/scene-optimizer-operations/<sha1>/<asset_stem>.optimized.usdc.",
    )
    args = p.parse_args(argv)

    asset = Path(args.asset)
    if not asset.is_file():
        print(json.dumps({"error": f"asset not found: {args.asset}"}))
        return 1

    if asset.suffix.lower() not in SUPPORTED_USD_EXTENSIONS:
        print(
            json.dumps(
                {
                    "error": (
                        f"not a USD file (must be one of {sorted(SUPPORTED_USD_EXTENSIONS)}): "
                        f"{args.asset}"
                    )
                }
            )
        )
        return 1

    asset_abs = str(asset.resolve())

    if args.output:
        output_path = Path(args.output).resolve()
        output_dir = output_path.parent
    else:
        # Default to .usdc (binary, mmap-friendly) regardless of input format.
        output_path = default_output_path(asset_abs, asset.stem)
        output_dir = output_path.parent

    log_path = output_dir / "run.log"

    # Pure resolve: do not create the directory. Callers create it before writing.

    print(
        json.dumps(
            {
                "output_dir": str(output_dir),
                "output": str(output_path),
                "log": str(log_path),
                "asset_abs": asset_abs,
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
