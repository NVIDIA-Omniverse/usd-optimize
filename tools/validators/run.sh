#!/bin/bash
# Wrapper that sets up LD_LIBRARY_PATH / PYTHONPATH and invokes validators.py
# under the build's bundled Python. Pass through all args to the python script.
set -e

SCRIPT_DIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")")
REPO_ROOT=$(realpath "$SCRIPT_DIR/../..")

CONFIG="${USD_OPTIMIZE_CONFIG:-release}"
PLATFORM="${USD_OPTIMIZE_PLATFORM:-linux-x86_64}"
BUILD_DIR="$REPO_ROOT/_build/$PLATFORM/$CONFIG"
USD_DIR="$REPO_ROOT/_build/target-deps/usd/$CONFIG"
PYTHON="$REPO_ROOT/_build/target-deps/python/bin/python3.12"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Build not found at $BUILD_DIR -- run ./repo.sh build first." >&2
    exit 1
fi

export LD_LIBRARY_PATH=$BUILD_DIR/lib:$BUILD_DIR/extraLibs:$USD_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PYTHONPATH=$BUILD_DIR/python:$USD_DIR/lib/python${PYTHONPATH:+:$PYTHONPATH}

# pip enforces the floor; a presence check would skip an older install.
# Exit 2 = nothing was validated, matching validators.py's guards; 1 means "issues found".
if ! "$PYTHON" -m pip install --quiet --disable-pip-version-check "usd-validation-nvidia>=1.22.0,<2"; then
    echo "usd-validation-nvidia install failed -- nothing was validated. Set HTTPS_PROXY if behind a proxy." >&2
    exit 2
fi

exec "$PYTHON" "$SCRIPT_DIR/validators.py" "$@"
