# SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
# SPDX-License-Identifier: Apache-2.0
#

"""Smoke test for an installed ``usd-optimize`` wheel.

Run with the Python interpreter of the virtual environment the wheel was
installed into (see ``test_wheel.sh`` / ``test_wheel.bat``). It exercises the
parts of the package most likely to break in a packaged build:

* importing ``usd_optimize.core`` (loads the pybind extension + its bundled
  shared libraries),
* the lazy C++ plugin load triggered on import (``dlopen`` of every operation
  plugin under ``usd_optimize.libs/operations`` — the step that surfaces
  missing/renamed ``libusd_optimize.core.so`` style errors),
* running a real operation end-to-end against an in-memory USD stage,
* (Linux) the dedupe of bundled libraries: no library grafted into
  ``usd_optimize.libs`` may load next to another copy of itself, e.g.
  usd-exchange's OpenUSD or oneTBB.

Exits non-zero with a readable message on any failure.
"""

import collections
import os
import re
import sys


def assert_no_duplicate_vendored_libs():
    """Fail if a library grafted into ``usd_optimize.libs`` is mapped next to another copy of itself.

    auditwheel names each graft ``<name>-<sha256[:8]>.<ext>``. The wheel shares usd-exchange's
    OpenUSD and oneTBB only while our grafts carry the same names as theirs; any mismatch loads a
    second copy. A second oneTBB ignores the host's Work.SetConcurrencyLimit.
    """
    if not sys.platform.startswith("linux"):
        return
    mapped = collections.defaultdict(set)
    with open("/proc/self/maps") as f:
        for line in f:
            fields = line.split(maxsplit=5)
            if len(fields) < 6 or not fields[5].startswith("/"):
                continue
            path = fields[5].rstrip("\n")
            match = re.match(r"(.+)-[0-9a-f]{8}\.", os.path.basename(path))
            if match:
                mapped[match.group(1)].add(path)
    duplicates = {
        lib: sorted(paths)
        for lib, paths in mapped.items()
        if len(paths) > 1 and any(os.path.basename(os.path.dirname(p)) == "usd_optimize.libs" for p in paths)
    }
    if duplicates:
        raise AssertionError(f"usd_optimize.libs loaded a second copy of {sorted(duplicates)}: {duplicates}")
    print(f"[smoke] no duplicate vendored libraries mapped ({len(mapped)} grafted libraries checked)")


def main():
    # Importing the package triggers UsdOptimizeCore.getInstance(), which
    # dlopens every operation plugin. A packaging/RPATH regression fails here.
    from pxr import Usd, UsdGeom
    from usd_optimize.core import ExecutionContext, UsdOptimizeCore

    core = UsdOptimizeCore.getInstance()

    ops = core.getOperations()
    if "countVertices" not in ops:
        raise AssertionError(
            f"expected the 'countVertices' operation to be registered; "
            f"got {len(ops)} operations: {sorted(ops)[:10]}..."
        )
    print(f"[smoke] import OK — {len(ops)} operations registered")

    # Build a trivial stage: one quad mesh (4 vertices).
    stage = Usd.Stage.CreateInMemory()
    mesh = UsdGeom.Mesh.Define(stage, "/World/Mesh")
    mesh.CreatePointsAttr([(0, 0, 0), (1, 0, 0), (1, 1, 0), (0, 1, 0)])
    mesh.CreateFaceVertexCountsAttr([4])
    mesh.CreateFaceVertexIndicesAttr([0, 1, 2, 3])

    context = ExecutionContext()
    context.set_stage(stage)
    context.analysisMode = 1

    # Run countVertices in analysis mode; thresholds chosen so the 4-vertex
    # quad lands in the "high" bucket, proving the plugin actually executed.
    result = core.executeOperation(
        "countVertices", context, {"high": 4, "veryHigh": 100, "extreme": 1000}
    )
    success, error, extra = result[0], result[1], result[2]
    if not success:
        raise AssertionError(f"countVertices failed: {error}")
    if not isinstance(extra, dict) or "analysis" not in extra:
        raise AssertionError(f"countVertices returned no analysis payload: {extra!r}")
    print(f"[smoke] countVertices executed — analysis buckets: {list(extra['analysis'])}")

    assert_no_duplicate_vendored_libs()

    print("[smoke] PASS")


if __name__ == "__main__":
    try:
        main()
    except Exception as exc:  # noqa: BLE001 - top-level smoke-test reporter
        print(f"[smoke] FAIL: {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(1)
