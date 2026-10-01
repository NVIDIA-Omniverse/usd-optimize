import argparse
import glob
import json
import os
import shutil
import sys
from typing import Callable, Dict

import omni.repo.man
import toml

# The USD version(s) the wheel is pinned to live in this file (single source of truth,
# shared with tools/ci/build.py). The wheel binds usd-exchange / usd-validation-nvidia
# from PyPI, which are built against these USD versions, so the wheel must be too.
_WHEEL_USD_VERSIONS_FILE = "$root/tools/pyproject/wheel_usd_versions.json"

# OpenUSD (and its TBB runtime) DLLs supplied at runtime by the ``usd-exchange``
# dependency (declared in pyproject.toml). On Windows these are excluded from the
# ``usd_optimize.libs`` bundle so the wheel binds usd-exchange's copies instead of
# shipping a duplicate set. usd-exchange does NOT name-mangle these DLLs on Windows,
# so the plain ``usd_*.dll``/``tbb.dll`` imports in our binaries resolve to
# usd-exchange's already-loaded modules (the Windows loader reuses a DLL of the same
# basename that is already in the process, which pxr loads before our core).
# ``usd_optimize.core.dll`` is deliberately absent — that one is ours to bundle.
_USD_EXCHANGE_DLLS = [
    "tbb.dll",
    "usd_arch.dll",
    "usd_ar.dll",
    "usd_gf.dll",
    "usd_js.dll",
    "usd_kind.dll",
    "usd_ndr.dll",
    "usd_pcp.dll",
    "usd_plug.dll",
    "usd_python.dll",
    "usd_sdf.dll",
    "usd_sdr.dll",
    "usd_tf.dll",
    "usd_trace.dll",
    "usd_ts.dll",
    "usd_usd.dll",
    "usd_usdGeom.dll",
    "usd_usdLux.dll",
    "usd_usdPhysics.dll",
    "usd_usdSemantics.dll",
    "usd_usdShade.dll",
    "usd_usdSkel.dll",
    "usd_usdUI.dll",
    "usd_usdUtils.dll",
    "usd_vt.dll",
    "usd_work.dll",
]


def _wheel_usd_versions():
    """USD versions the wheel may be built against (see _WHEEL_USD_VERSIONS_FILE)."""
    with open(omni.repo.man.resolve_tokens(_WHEEL_USD_VERSIONS_FILE)) as f:
        return json.load(f)["usd_vers"]


def _assert_wheel_usd_ver():
    """Fail fast unless the built tree targets a pinned wheel USD version.

    The tree's USD version is inferred from deps/usd-lib-deps.generated.packman.xml, which
    the build regenerates for the selected usd_ver on every fetch; its library version
    strings embed a ``usd<ver>`` marker (see deps/usd-lib-deps.json)."""
    allowed = _wheel_usd_versions()
    if not allowed:
        raise RuntimeError("tools/pyproject/wheel_usd_versions.json must list at least one 'usd_vers'.")
    try:
        with open(omni.repo.man.resolve_tokens("$root/deps/usd-lib-deps.generated.packman.xml")) as f:
            text = f.read()
    except OSError:
        text = ""
    if not any(f"usd{v}" in text or f"usd{v.replace('.', '-')}" in text for v in allowed):
        raise RuntimeError(
            f"The usd-optimize wheel must be built against USD {' or '.join(allowed)}, but the "
            f"build tree does not. Rebuild with './repo.sh --set-token usd_ver:{allowed[0]} build' "
            f"before running py_package."
        )


def _assert_pypi_compatible_tag(wheel: str):
    """Fail fast if the wheel's PEP 427 tags are not PyPI-publishable.

    PyPI rejects bare ``linux_*`` platform tags (only ``manylinux_*`` / ``musllinux_*`` are
    accepted), so a missing auditwheel repair must fail here rather than at upload. The
    interpreter tag must also match the pinned CPython (``requires-python`` in
    tools/pyproject/pyproject.toml)."""
    name = os.path.basename(wheel)
    if "cp312" not in name:
        raise RuntimeError(f"wheel is not tagged cp312 (wrong build interpreter?): {name}")
    if omni.repo.man.is_windows():
        if "win_amd64" not in name:
            raise RuntimeError(f"wheel is missing the expected win_amd64 platform tag: {name}")
    elif "manylinux" not in name:
        raise RuntimeError(f"wheel has a non-manylinux tag; PyPI rejects bare linux_* tags: {name}")


def _stage_stripped_libs(libDirs, stagingDir: str) -> str:
    """Copy the shared libraries auditwheel will graft into *stagingDir* and strip the copies.

    auditwheel renames each vendored library to ``<name>-<sha256[:8]>.so``, so the wheel only binds
    usd-exchange's already-loaded OpenUSD when our copies are byte-identical to the ones its wheel
    ships. usd-exchange strips before repairing (their tools/repoman/py_package.py), so an unstripped
    copy hashes differently and the wheel loads a second, parallel OpenUSD -- every stage passed
    across the boundary then misses UsdUtilsStageCache and operations silently do nothing.

    The graft is also named after the symlink-resolved file (``libtbb.so.12`` -> ``libtbb.so.12.13``
    -> ``libtbb-<hash>.so.12.13``), so symlinks are recreated, not dereferenced. A flattened
    ``libtbb.so.12`` copy grafts as ``libtbb-<hash>.so.12``, which usd-exchange's TBB does not match,
    and the process loads a second oneTBB that ignores the host's Work.SetConcurrencyLimit.

    Stripping copies rather than the build tree keeps symbols in the packman drop and symstore, which
    are packaged after this step. Strip must run before auditwheel's patchelf, not via
    ``auditwheel repair --strip``, which can leave LOAD segments that are no longer page-aligned.
    """
    strippedDir = os.path.join(stagingDir, "stripped-libs")
    os.makedirs(strippedDir, exist_ok=True)
    stripped = 0
    for libDir in libDirs:
        for path in glob.glob(f"{libDir}/*.so*"):
            real = os.path.realpath(path)
            if not os.path.isfile(real):
                continue
            with open(real, "rb") as f:
                if f.read(4) != b"\x7fELF":
                    continue
            # First directory wins for both names, matching the LD_LIBRARY_PATH order below.
            realName = os.path.basename(real)
            dest = os.path.join(strippedDir, realName)
            if not os.path.lexists(dest):
                shutil.copy2(real, dest)
                omni.repo.man.run_process(["strip", dest], exit_on_error=True)
                stripped += 1
            link = os.path.join(strippedDir, os.path.basename(path))
            if not os.path.lexists(link):
                os.symlink(realName, link)
    omni.repo.man.logger.info(f"Stripped {stripped} shared libraries into {strippedDir}")
    return strippedDir


def _smoke_test_wheel(wheel: str, stagingDir: str):
    """Install *wheel* into a throwaway virtual environment and run the smoke
    test, which imports the package (loading every operation plugin) and runs a
    simple operation end-to-end. Raises on failure."""
    # The wheel is built for CPython 3.12, so the venv must use a 3.12 interpreter.
    python = shutil.which("python3.12")
    if python is None:
        if sys.version_info[:2] == (3, 12):
            python = sys.executable
        else:
            raise RuntimeError("python3.12 not found on PATH; cannot test the cp312 wheel")

    venvDir = f"{stagingDir}/test_venv"
    if os.path.exists(venvDir):
        shutil.rmtree(venvDir)

    bin_dir = "Scripts" if omni.repo.man.is_windows() else "bin"
    exe_ext = ".exe" if omni.repo.man.is_windows() else ""
    venvPython = f"{venvDir}/{bin_dir}/python{exe_ext}"

    omni.repo.man.logger.info(f"Creating test venv at {venvDir}")
    omni.repo.man.run_process([python, "-m", "venv", venvDir], exit_on_error=True)
    omni.repo.man.run_process([venvPython, "-m", "pip", "install", "--upgrade", "pip"], exit_on_error=True)
    omni.repo.man.logger.info(f"Installing {os.path.basename(wheel)} (with runtime dependencies)")
    omni.repo.man.run_process([venvPython, "-m", "pip", "install", wheel], exit_on_error=True)

    smoke = omni.repo.man.resolve_tokens("$root/tools/pyproject/wheel_smoke_test.py")
    omni.repo.man.logger.info("Running wheel smoke test")
    omni.repo.man.run_process([venvPython, smoke], exit_on_error=True)
    omni.repo.man.logger.info("Wheel smoke test PASSED")


def setup_repo_tool(parser: argparse.ArgumentParser, config: Dict) -> Callable:
    toolConfig = config.get("repo_py_package", {})
    if not toolConfig.get("enabled", True):
        return None

    parser.description = "Tool to build a wheel for the precompiled Usd Optimize modules and all of its runtime dependencies."
    parser.add_argument(
        "--test",
        action="store_true",
        help="After building, install the wheel into a fresh virtual environment and run a smoke test "
        "(imports the package, loads the operation plugins, and runs a simple operation).",
    )
    omni.repo.man.add_config_arg(parser)

    def run_repo_tool(options, config: Dict):
        _assert_wheel_usd_ver()
        toolConfig = config["repo_py_package"]
        stagingDir = toolConfig["staging_dir"]
        installDir = toolConfig["install_dir"]
        exclusions = toolConfig.get("exclude", [])
        ignore_callable = shutil.ignore_patterns(*exclusions)
        repoVersionFile = config["repo"]["folders"]["version_file"]
        fullVersion = omni.repo.man.build_number.generate_build_number_from_file(repoVersionFile)
        packageVersion, _ = fullVersion.split("+")

        # copy artifacts so they can be packaged by with a reasonable name
        source = omni.repo.man.resolve_tokens("_build/$platform/$config")
        if os.path.exists(stagingDir):
            shutil.rmtree(stagingDir)
        shutil.copytree(f"{source}/python/usd_optimize", f"{stagingDir}/usd_optimize", ignore=ignore_callable)
        # back-compat shim package for the pre-rename `omni.scene.optimizer.*` namespace
        shutil.copytree(f"{source}/python/omni", f"{stagingDir}/omni", ignore=ignore_callable)
        # copy the libs on windows since they are needed for the wheel to work, but for Linux auditwheel will handle this
        if omni.repo.man.is_windows():
            # Exclude the USD/TBB DLLs supplied by usd-exchange so the wheel never
            # ships a second copy that could shadow theirs (see _USD_EXCHANGE_DLLS).
            windows_ignore = shutil.ignore_patterns(*exclusions, *_USD_EXCHANGE_DLLS)
            shutil.copytree(f"{source}/lib", f"{stagingDir}/usd_optimize.libs", ignore=windows_ignore)
        else:
            shutil.copytree(f"{source}/lib/operations", f"{stagingDir}/usd_optimize.libs/operations", ignore=ignore_callable)
            shutil.copyfile(f"{source}/lib/operation_mapping.json", f"{stagingDir}/usd_optimize.libs/operation_mapping.json")

        # generate pyproject file
        pyproject_source = omni.repo.man.resolve_tokens("$root/tools/pyproject/pyproject.toml")
        pyproject_target = f"{stagingDir}/pyproject.toml"
        with open(pyproject_source, "r") as f:
            data = toml.load(f)
        data["project"]["version"] = packageVersion
        with open(pyproject_target, "w") as f:
            toml.dump(data, f)

        # Stage the wheel's README and LICENSE next to the pyproject.toml (referenced by its
        # readme / license-files fields). The packaging README is its own PyPI-appropriate doc:
        # the repo README.md is a developer guide whose relative links do not render on the index.
        shutil.copyfile(
            omni.repo.man.resolve_tokens("$root/tools/pyproject/README.md"), f"{stagingDir}/README.md"
        )
        shutil.copyfile(omni.repo.man.resolve_tokens("$root/LICENSE"), f"{stagingDir}/LICENSE")

        # copy the pyproject setup script
        shutil.copyfile(omni.repo.man.resolve_tokens("$root/tools/pyproject/pybuild.py"), f"{stagingDir}/pybuild.py")

        # build the wheel
        build_cmd = omni.repo.man.resolve_tokens("$root/tools/pyproject/pybuild${shell_ext}")
        build_args = [build_cmd, "build", "--format=wheel", f"--directory={stagingDir}", f"--output={stagingDir}/dist"]
        omni.repo.man.logger.info(" ".join(build_args))
        omni.repo.man.run_process(build_args, exit_on_error=True)

        wheel = glob.glob(f"{stagingDir}/dist/*.whl")[0]
        os.makedirs(installDir, exist_ok=True)
        if omni.repo.man.is_windows():
            # No repair step on Windows. Unlike Linux there is no manylinux policy to
            # satisfy, and the wheel is already self-contained: the first-party DLLs
            # (including the dlopen'd operation plugins) were staged into
            # usd_optimize.libs above with their original names, and the package init
            # (source/core/python/usd_optimize/impl/core/__init__.py) puts that
            # directory on the DLL search path and preloads its DLLs at import time.
            # A delvewheel repair would actively break this: it name-mangles the
            # vendored DLLs but only patches the import tables of the extension
            # modules, so the operation plugins' unmangled imports
            # (usd_optimize.core.dll etc.) no longer resolve when they are dlopen'd.
            shutil.copy2(wheel, installDir)
        else:
            # Repair the wheel by baking in the dependent shared libraries found in the
            # first-party lib/ tree and the runtime libraries in extraLibs/. This patches
            # the import tables of every binary in the wheel (extension module + the
            # dlopen'd operation plugins) so it is self-contained.
            lib_dir = os.path.abspath(os.path.realpath(f"{source}/lib"))
            extra_libs_dir = os.path.abspath(os.path.realpath(f"{source}/extraLibs"))
            tokens = omni.repo.man.get_tokens()
            platform_target_abi = omni.repo.man.get_abi_platform_translation(tokens["platform"], tokens.get("abi", "2.35"))
            # Stripped copies come first so auditwheel grafts those; see _stage_stripped_libs.
            stripped_dir = _stage_stripped_libs([lib_dir, extra_libs_dir], stagingDir)
            env = os.environ.copy()
            env["LD_LIBRARY_PATH"] = f"{stripped_dir}:{lib_dir}:{extra_libs_dir}"
            auditwheel_cmd = omni.repo.man.resolve_tokens("$root/tools/pyproject/auditwheel${shell_ext}")
            auditwheel_args = [auditwheel_cmd, "repair", wheel, "--plat", platform_target_abi, "-w", installDir]
            omni.repo.man.logger.info(" ".join(auditwheel_args))
            omni.repo.man.run_process(auditwheel_args, exit_on_error=True, env=env)
        # the repair tool may retag the platform, so pick the repaired wheel by recency
        final_wheel = max(glob.glob(f"{installDir}/*.whl"), key=os.path.getmtime)
        _assert_pypi_compatible_tag(final_wheel)

        if getattr(options, "test", False):
            _smoke_test_wheel(final_wheel, stagingDir)

    return run_repo_tool
