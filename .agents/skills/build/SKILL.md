---
name: build
description: Build Scene Optimizer from source via repo.sh (Linux) or repo.bat (Windows). Use when compiling the repo, switching configs, or selecting a USD flavor.
version: "1.0.0"
allowed-tools: Shell
metadata:
  author: NVIDIA Corporation
  tags: [build, source, compile, repo]
---

<!-- SPDX-FileCopyrightText: Copyright (c) 2026 NVIDIA CORPORATION & AFFILIATES. All rights reserved. -->
<!-- SPDX-License-Identifier: Apache-2.0 -->

# Building Scene Optimizer

> The repo ships two equivalent entry scripts: `./repo.sh` for Linux/bash-compatible shells and `repo.bat` for Windows `cmd.exe`/PowerShell. They accept the same arguments — every example below shows both. Pick whichever matches the active shell.
>
> Consuming a **published binary drop** (no source, no `repo.sh`/`repo.bat`) is a different workflow — see the `prebuilt-package` skill and [`docs/install-prebuilt-windows.md`](../../../docs/install-prebuilt-windows.md).

## What this skill covers

Search this doc for keywords like `MSVC`, `gcc`, `Packman`, `repo.sh`, `repo.bat`, `--config`, `--rebuild`, `usd-flavor`, `gcov`, `format` to jump.

- **Prerequisites** — Linux (`gcc`, `make`) and Windows (Visual Studio, MSVC, Win SDK).
- **Default Build (release)** — `./repo.sh build` / `repo.bat build` invocation.
- **Build Options** — `--config`, `--rebuild`, `--enable-gcov`, USD-flavor selection.
- **Build Output** — `_build/<platform>/<config>/` layout.
- **USD Flavor Tokens** — selecting which USD bundle to build against.
- **Formatting Check** — `./repo.sh ci format`.

Companion skills: `prebuilt-package` (use a binary drop instead), `testing` (run tests against a built tree).

## Prerequisites

- **Most dependencies** (USD, Python, **premake**, third-party libraries, and so on) are fetched automatically with **Packman** when you run the build script. You do **not** install those by hand.
- **Windows (x86_64):** You must install a **host** C++ stack: **Visual Studio** or **Build Tools** with the **MSVC** workload and a **Windows 10/11 SDK** on the machine. The repo is configured to use the host toolchain (see `repo.toml` → `[repo_build]` → `msbuild.link_host_toolchain`). If auto-discovery fails, set `msbuild.vs_path` / `msbuild.winsdk_path` in `repo.toml` or see the [repo_build toolchains](https://docs.omniverse.nvidia.com/kit/docs/repo_build/latest/docs/toolchains.html) doc.
- **Windows (non-English locale): set `PYTHONUTF8=1`.** Repo tooling reads UTF-8 config files such as `tools/repoman/repo_tools.toml`, but Python on Windows decodes text files using the system ANSI code page (e.g. **CP949** on Korean, **CP932** on Japanese, **GBK** on Simplified Chinese installs). Any non-ASCII byte in a config triggers `UnicodeDecodeError` and aborts the build. Enabling Python's UTF-8 mode forces UTF-8 for all I/O and resolves the mismatch:

    ```cmd
    :: cmd.exe — current session
    set PYTHONUTF8=1
    repo.bat build
    ```

    ```powershell
    # PowerShell — current session
    $env:PYTHONUTF8 = "1"
    repo.bat build
    ```

    To make it permanent, set it as a user environment variable: **System Properties → Environment Variables → New → `PYTHONUTF8` = `1`**, or via `setx PYTHONUTF8 1` (takes effect in new shells). English / US-locale machines default to CP1252, which happens to overlap with UTF-8 for ASCII bytes and so usually hides this bug — but setting `PYTHONUTF8=1` is harmless there and a good default. See [PEP 540](https://peps.python.org/pep-0540/) for background.
- **Linux:** A suitable **gcc** and **make** in **PATH** (or your container image). The build does not ship the compiler through Packman.

## Default Build (release)

```bash
./repo.sh build      # Linux / bash
repo.bat build       # Windows cmd.exe / PowerShell
```

This fetches dependencies, runs premake to generate build files, and compiles in **release** mode.

## Build Options

```bash
# Rebuild from scratch (clean + build)
./repo.sh build --rebuild
repo.bat build --rebuild

# Build a specific config
./repo.sh build --config release
repo.bat build --config release

./repo.sh build --config debug
repo.bat build --config debug

# Build both configs
./repo.sh build --config release debug
repo.bat build --config release debug

# Full rebuild of both configs (used by CI)
./repo.sh build --rebuild --config release debug
repo.bat build --rebuild --config release debug

# Build with code coverage instrumentation (Linux only, requires clean build)
./repo.sh build --rebuild --config release --enable-gcov
```

## Build Output

Build artifacts are placed in:

```
_build/<platform>/<config>/
```

For example on Linux: `_build/linux-x86_64/release/`. On Windows: `_build/windows-x86_64/release/`.

Key outputs:
- `lib/` — shared libraries (core + operation plugins)
- `python/` — Python bindings
- `test.cpp` — C++ test executable (Doctest)
- `test.python.sh` / `test.python.bat` — Python test runner script

## USD Flavor Tokens

The default USD flavor and version are set in `repo.toml`. Override them at build time:

```bash
./repo.sh --set-token usd_flavor:usd --set-token usd_ver:25.11 --set-token python_ver:3.12 build
repo.bat --set-token usd_flavor:usd --set-token usd_ver:25.11 --set-token python_ver:3.12 build
```

Supported flavors and versions are listed in `deps/usd_flavors.json`.

## Formatting Check

```bash
./repo.sh ci format
repo.bat ci format
```

## Purpose

Compile Scene Optimizer from a source checkout into a runnable build tree
under `_build/<platform>/<config>/`, including the C++ libraries, Python
bindings, and operation plugins that every other skill in this repo relies
on. Use this skill when starting work on a fresh clone, after pulling
changes that affect the C++ side, when toggling between `release` and
`debug`, or when changing USD flavor / version / Python version tokens.

## Limitations

- This skill assumes a **source checkout** with `repo.sh` / `repo.bat`
  present. For a binary drop, use the `prebuilt-package` skill instead.
- It does not install dependencies beyond what Packman can fetch — host
  toolchain (MSVC + Win SDK on Windows, gcc + make on Linux) must already
  be on the machine.
- Code coverage (`--enable-gcov`) is **Linux only**.
- This skill does not handle running tests — chain into the `testing`
  skill once the build succeeds.

## Troubleshooting

| Symptom | Likely cause | Fix |
|---|---|---|
| `Could not find Visual Studio` / `MSVC` (Windows) | Auto-discovery failed for the VS install path. | Install/repair Visual Studio with the MSVC workload + Windows 10/11 SDK, or set `msbuild.vs_path` / `msbuild.winsdk_path` in `repo.toml`. See the [repo_build toolchains](https://docs.omniverse.nvidia.com/kit/docs/repo_build/latest/docs/toolchains.html) doc. |
| `UnicodeDecodeError` reading `repo_tools.toml` / a `.toml` / `.json` config (Windows) | Python is decoding UTF-8 config files with the system ANSI code page (CP949 Korean, CP932 Japanese, GBK Chinese, …). | Enable Python UTF-8 mode: `set PYTHONUTF8=1` (cmd) or `$env:PYTHONUTF8 = "1"` (PowerShell) before running `repo.bat`, or `setx PYTHONUTF8 1` to persist for new shells. See the Prerequisites note above. |
| Packman download / checksum errors | Network proxy or stale Packman cache. | Set `HTTPS_PROXY` if behind a proxy; clear `~/.cache/packman/` and re-run. |
| Linker error referencing a USD symbol | USD flavor / version mismatch with what plugins were built against. | Pass matching `--set-token usd_flavor:... usd_ver:... python_ver:...` consistently across `build` invocations. |
| `--enable-gcov` errors on Windows | Coverage build is Linux-only. | Drop `--enable-gcov` on Windows. |
| Build succeeds but tests fail to find `_build/<platform>/<config>/lib` | Wrong config selected. | Re-run with the same `--config` you'll test under (`release` by default). |

