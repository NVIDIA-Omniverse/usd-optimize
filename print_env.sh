#!/usr/bin/env bash
# Copyright (c) 2023-2026, NVIDIA CORPORATION.
# Reports environment information useful for diagnosing Scene Optimizer issues.
# Usage:
#   ./print_env.sh           — prints GitHub-friendly HTML block to stdout
#   ./print_env.sh > env.txt — save to a file and attach when filing an issue

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
export OMNI_REPO_ROOT="${OMNI_REPO_ROOT:-$SCRIPT_DIR}"

print_env() {
    echo "***git***"
    if [[ "$(git -C "${SCRIPT_DIR}" rev-parse --is-inside-work-tree 2>/dev/null)" == "true" ]]; then
        git -C "${SCRIPT_DIR}" log --decorate -n 1
        echo "***git submodules***"
        git -C "${SCRIPT_DIR}" submodule status --recursive
    else
        echo "Not inside a git repository"
    fi
    echo

    echo "***Scene Optimizer***"
    if [[ -f "${SCRIPT_DIR}/VERSION.md" ]]; then
        echo "VERSION.md: $(tr -d '\n' <"${SCRIPT_DIR}/VERSION.md")"
    fi
    if [[ -f "${SCRIPT_DIR}/repo.toml" ]]; then
        echo "repo.toml ([repo] name and [repo.tokens]):"
        sed -n '/^\[repo\]$/,/^\[repo\./p' "${SCRIPT_DIR}/repo.toml" | grep '^name = ' | head -1
        grep -E '^usd_flavor|^usd_ver|^python_ver' "${SCRIPT_DIR}/repo.toml" 2>/dev/null || true
    fi
    echo

    echo "***OS information***"
    if [[ -f /etc/os-release ]]; then
        cat /etc/os-release
    else
        cat /etc/*-release 2>/dev/null || true
    fi
    uname -a
    echo

    echo "***GPU information***"
    if command -v nvidia-smi >/dev/null 2>&1; then
        nvidia-smi
    else
        echo "nvidia-smi not found (no NVIDIA driver in PATH or not installed)"
    fi
    echo

    echo "***CPU***"
    if command -v lscpu >/dev/null 2>&1; then
        lscpu
    else
        echo "lscpu not available"
    fi
    echo

    echo "***CMake***"
    command -v cmake >/dev/null 2>&1 && cmake --version || echo "cmake not found"
    echo

    echo "***C++ compilers***"
    command -v g++ >/dev/null 2>&1 && g++ --version || echo "g++ not found"
    command -v clang++ >/dev/null 2>&1 && clang++ --version || echo "clang++ not found"
    echo

    echo "***nvcc***"
    command -v nvcc >/dev/null 2>&1 && nvcc --version || echo "nvcc not found"
    echo

    echo "***Python***"
    for py in python3 python; do
        if command -v "${py}" >/dev/null 2>&1; then
            echo "Using: $(command -v "${py}")"
            "${py}" -c "import sys; print('Python {}.{}.{}'.format(sys.version_info[0], sys.version_info[1], sys.version_info[2]))"
            break
        fi
    done
    if ! command -v python3 >/dev/null 2>&1 && ! command -v python >/dev/null 2>&1; then
        echo "python3/python not found"
    fi
    echo

    echo "***OpenUSD (pxr) in Python***"
    if command -v python3 >/dev/null 2>&1; then
        python3 -c "import pxr; print('pxr:', pxr.__file__)" 2>/dev/null || echo "pxr import failed (expected before deps are set up)"
    elif command -v python >/dev/null 2>&1; then
        python -c "import pxr; print('pxr:', pxr.__file__)" 2>/dev/null || echo "pxr import failed (expected before deps are set up)"
    else
        echo "No Python interpreter to probe pxr"
    fi
    echo

    echo "***Environment variables***"
    printf '%-32s: %s\n' PATH "${PATH}"
    printf '%-32s: %s\n' LD_LIBRARY_PATH "${LD_LIBRARY_PATH}"
    printf '%-32s: %s\n' OMNI_REPO_ROOT "${OMNI_REPO_ROOT}"
    printf '%-32s: %s\n' PM_PACKAGES_ROOT "${PM_PACKAGES_ROOT}"
    printf '%-32s: %s\n' CMAKE_PREFIX_PATH "${CMAKE_PREFIX_PATH}"
    printf '%-32s: %s\n' CONDA_PREFIX "${CONDA_PREFIX}"
    printf '%-32s: %s\n' PYTHONPATH "${PYTHONPATH}"
    echo

    if command -v conda >/dev/null 2>&1; then
        echo "***conda packages***"
        command -v conda && conda list
        echo
    elif command -v pip >/dev/null 2>&1; then
        echo "***pip packages***"
        command -v pip && pip list
        echo
    else
        echo "conda and pip not found"
    fi
    return 0
}

echo "<details><summary>Click here to see environment details</summary><pre>"
echo "     "
print_env | while read -r line; do
    echo "     $line"
done
echo "</pre></details>"
