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
* (Linux) the single shared runtime: no shared library may be mapped from
  two files, whether the copies carry their original SONAME or an auditwheel
  hash, e.g. a second OpenUSD or oneTBB next to usd-exchange's.

Exits non-zero with a readable message on any failure.
"""

import argparse
import collections
import ctypes
import importlib.metadata
import os
import re
import subprocess
import sys
from pathlib import Path


_AUDITWHEEL_HASH = re.compile(r"-[0-9a-f]{8}(?=\.)")


def library_identity(name):
    """The library a shared-object filename names, independent of auditwheel's renaming and of version suffixes.

    auditwheel inserts an 8-digit content hash before the first dot and keeps the version suffix, so
    ``libtbb.so.12`` and ``libtbb-821c567d.so.12.13`` are both ``libtbb``, and
    ``libusd_optimize-ee8d86b0.core.so`` is ``libusd_optimize.core``.
    """
    return _AUDITWHEEL_HASH.sub("", name, count=1).split(".so")[0]


def is_shared_runtime(identity):
    """Whether a library identity is part of the runtime shared with usd-exchange: OpenUSD, oneTBB or MaterialX.

    Other libraries are left alone: a future dependency bundling its own copy of a common library (``libz``, say)
    beside a system copy is not a duplicate of our runtime.
    """
    if identity.startswith("libusd_optimize"):
        return False
    return identity.startswith(("libusd_", "libtbb", "libMaterialX"))


def find_duplicate_libraries(maps_lines):
    """Map each shared-runtime library mapped from more than one file to those files, given ``/proc/<pid>/maps`` lines."""
    mapped = collections.defaultdict(set)
    for line in maps_lines:
        fields = line.split(maxsplit=5)
        if len(fields) < 6 or not fields[5].startswith("/"):
            continue
        path = fields[5].rstrip("\n").removesuffix(" (deleted)")
        name = os.path.basename(path)
        if ".so" in name and is_shared_runtime(library_identity(name)):
            mapped[library_identity(name)].add(path)
    return {lib: sorted(paths) for lib, paths in mapped.items() if len(paths) > 1}


def check_duplicate_detection():
    """Regression cases for find_duplicate_libraries, using synthetic maps entries."""

    def maps(*paths):
        return [f"7f0000000000-7f0000001000 r--p 00000000 08:01 1 {path}\n" for path in paths]

    cases = [
        # a library mapped as several segments of one file is not a duplicate
        (maps("/a/libusd_tf.so", "/a/libusd_tf.so", "/a/libusd_usdGeom.so"), set()),
        # two copies under the same original SONAME, as when two wheels both ship it unhashed
        (maps("/sdk/libtbb.so.12", "/opt/libtbb.so.12"), {"libtbb"}),
        # an original name next to an auditwheel-hashed copy of the same library
        (maps("/sdk/libusd_tf.so", "/opt/libusd_tf-12345678.so"), {"libusd_tf"}),
        (maps("/sdk/libtbb.so.12", "/opt/libtbb-821c567d.so.12.13"), {"libtbb"}),
        # two hashed copies, the case the check covered before
        (maps("/sdk/libusd_tf-87654321.so", "/opt/libusd_tf-12345678.so"), {"libusd_tf"}),
        # MaterialX is part of the shared runtime too
        (maps("/sdk/libMaterialXCore.so.1.39.5", "/opt/libMaterialXCore-0123abcd.so.1.39.5"), {"libMaterialXCore"}),
        # extension modules are not libraries, and distinct libraries stay distinct
        (maps("/a/pxr/Tf/_tf.so", "/b/other/Tf/_tf.so", "/a/libusd_usd.so", "/a/libusd_usdShade.so"), set()),
        # libraries outside the shared runtime are not compared, including our own
        (maps("/usr/lib/libz.so.1", "/opt/libz-89abcdef.so.1.3", "/a/libusd_optimize.core.so", "/b/libusd_optimize.core.so"), set()),
    ]
    for lines, expected in cases:
        found = set(find_duplicate_libraries(lines))
        if found != expected:
            raise AssertionError(f"duplicate detection found {sorted(found)}, expected {sorted(expected)}, for {lines}")
    print(f"[smoke] duplicate detection regression cases pass ({len(cases)} cases)")


def assert_no_duplicate_libraries():
    """Fail if an OpenUSD, oneTBB or MaterialX library is mapped from two different files.

    A second OpenUSD breaks stage-cache sharing; a second oneTBB ignores the host's
    Work.SetConcurrencyLimit. Check SDK libraries too, even when neither copy is ours.
    """
    if not sys.platform.startswith("linux"):
        return
    with open("/proc/self/maps") as f:
        lines = f.readlines()
    duplicates = find_duplicate_libraries(lines)
    if duplicates:
        raise AssertionError(f"a second copy of {sorted(duplicates)} is mapped: {duplicates}")
    names = {os.path.basename(line.split()[-1]) for line in lines if line.split()[-1].startswith("/")}
    checked = {library_identity(name) for name in names if ".so" in name and is_shared_runtime(library_identity(name))}
    print(f"[smoke] no duplicate OpenUSD, oneTBB or MaterialX libraries mapped ({len(checked)} libraries checked)")


def assert_shared_runtime():
    if not sys.platform.startswith("linux"):
        return
    package = importlib.metadata.distribution("usd-optimize")
    libs = Path(package.locate_file("usd_optimize.libs"))
    bundled = [path.name for path in libs.glob("*.so*") if is_shared_runtime(library_identity(path.name))]
    if bundled:
        raise AssertionError(f"OpenUSD, oneTBB and MaterialX libraries must come from usd-exchange: {bundled}")
    if hasattr(ctypes.CDLL(None), "TBB_runtime_interface_version"):
        raise AssertionError("oneTBB was promoted to global symbol scope")
    print("[smoke] no bundled OpenUSD, oneTBB or MaterialX libraries; oneTBB symbols remain local")


def main(import_order):
    # Importing the package triggers UsdOptimizeCore.getInstance(), which
    # dlopens every operation plugin. A packaging/RPATH regression fails here.
    if import_order == "optimizer":
        import usd_optimize.core  # noqa: F401
    else:
        import usdex.core  # noqa: F401
    from pxr import Plug, Usd, UsdGeom
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

    # Load by plugin path after optimizer import: duplicate USD copies can abort here
    # even when matching SONAMEs initially made stage-cache sharing appear to work.
    plugin = Plug.Registry().GetPluginWithName("usdSkel")
    if not plugin or not plugin.Load():
        raise AssertionError("usdSkel plugin failed to load")
    context.analysisMode = 0
    UsdGeom.Cube.Define(stage, "/Keep")
    result = core.executeOperation("deletePrims", context, {"primPaths": ["/World/Mesh"]})
    if not result[0] or stage.GetPrimAtPath("/World/Mesh") or not stage.GetPrimAtPath("/Keep"):
        raise AssertionError(f"deletePrims did not modify the caller's stage: {result}")
    print("[smoke] deletePrims modified the caller's stage")
    context.remove_stage()
    assert_shared_runtime()
    assert_no_duplicate_libraries()

    print("[smoke] PASS")


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("--import-order", choices=("optimizer", "usdex"))
    args = parser.parse_args()
    try:
        if args.import_order:
            main(args.import_order)
        else:
            check_duplicate_detection()
            for order in ("optimizer", "usdex"):
                subprocess.run([sys.executable, __file__, "--import-order", order], check=True)
    except Exception as exc:  # noqa: BLE001 - top-level smoke-test reporter
        print(f"[smoke] FAIL: {type(exc).__name__}: {exc}", file=sys.stderr)
        sys.exit(1)
