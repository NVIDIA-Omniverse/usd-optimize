"""Resolve the artifact directory for a USD asset.

Pure-stdlib helper used by the run-validators / interpret-validators skills so
they don't have to depend on POSIX-only utilities (realpath, sha1sum, stat -c,
date -d). Runs under any Python 3 on any OS.

Output is a single JSON object on stdout:

    {
      "artifact_dir": "/tmp/scene-optimizer-validation/<sha1>",
      "csv":          "<artifact_dir>/issues.csv",
      "summary":      "<artifact_dir>/summary.json",
      "log":          "<artifact_dir>/run.log",
      "asset_abs":    "<absolute path to asset>",
      "state":        "fresh" | "stale" | "missing",
      "csv_mtime":    "<ISO-8601 UTC timestamp>" | null,
      "asset_mtime":  "<ISO-8601 UTC timestamp>"
    }

`state` semantics:
  - "missing" — no `issues.csv` exists for this asset yet
  - "fresh"   — CSV exists and is newer than the asset (replay is safe)
  - "stale"   — CSV exists but the asset has been edited since the run

Usage:
    python tools/perf_validators/resolve_artifacts.py <asset_path> [--logs-dir <dir>]
"""

import argparse
import datetime as dt
import hashlib
import json
import sys
import tempfile
from pathlib import Path


def _temp_root() -> Path:
    """Cross-platform writable temp directory."""
    return Path(tempfile.gettempdir())


def _iso(ts: float) -> str:
    return dt.datetime.fromtimestamp(ts, tz=dt.timezone.utc).isoformat()


def main(argv: list[str]) -> int:
    p = argparse.ArgumentParser(description="Resolve artifact dir for a USD asset.")
    p.add_argument("asset")
    p.add_argument(
        "--logs-dir",
        default=None,
        help="Override the artifact dir (otherwise: <tmp>/scene-optimizer-validation/<sha1>).",
    )
    args = p.parse_args(argv)

    asset = Path(args.asset)
    if not asset.is_file():
        print(json.dumps({"error": f"asset not found: {args.asset}"}))
        return 1

    asset_abs = str(asset.resolve())
    if args.logs_dir:
        artifact_dir = Path(args.logs_dir).resolve()
    else:
        h = hashlib.sha1(asset_abs.encode("utf-8")).hexdigest()
        artifact_dir = _temp_root() / "scene-optimizer-validation" / h

    # Pure resolve: do not create the directory here. Callers that intend to
    # write into it (run-validators) create it themselves; read-only callers
    # (interpret-validators) get an honest "missing" state without leaving an
    # empty directory behind.

    csv_path = artifact_dir / "issues.csv"
    summary_path = artifact_dir / "summary.json"
    log_path = artifact_dir / "run.log"

    asset_mtime = asset.stat().st_mtime
    state = "missing"
    csv_mtime = None
    if csv_path.is_file():
        csv_mtime = csv_path.stat().st_mtime
        state = "stale" if asset_mtime > csv_mtime else "fresh"

    print(
        json.dumps(
            {
                "artifact_dir": str(artifact_dir),
                "csv": str(csv_path),
                "summary": str(summary_path),
                "log": str(log_path),
                "asset_abs": asset_abs,
                "state": state,
                "csv_mtime": _iso(csv_mtime) if csv_mtime else None,
                "asset_mtime": _iso(asset_mtime),
            },
            indent=2,
        )
    )
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
