#!/bin/bash
# Wrapper that sets up LD_LIBRARY_PATH / PYTHONPATH and invokes perf_validators.py
# under the build's bundled Python. Pass through all args to the python script.
#
# Examples:
#   tools/perf_validators/run.sh run path/to/asset.usd --summary out.json
#   tools/perf_validators/run.sh compare a.json b.json
set -e

SCRIPT_DIR=$(realpath "$(dirname "${BASH_SOURCE[0]}")")
REPO_ROOT=$(realpath "$SCRIPT_DIR/../..")

CONFIG="${SO_CONFIG:-release}"
PLATFORM="${SO_PLATFORM:-linux-x86_64}"
BUILD_DIR="$REPO_ROOT/_build/$PLATFORM/$CONFIG"
USD_DIR="$REPO_ROOT/_build/target-deps/usd/$CONFIG"
PYTHON="$REPO_ROOT/_build/target-deps/python/bin/python3.12"

if [[ ! -d "$BUILD_DIR" ]]; then
    echo "Build not found at $BUILD_DIR -- run ./repo.sh build first." >&2
    exit 1
fi

export LD_LIBRARY_PATH=$BUILD_DIR/lib:$BUILD_DIR/extraLibs:$USD_DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}
export PYTHONPATH=$BUILD_DIR/python:$USD_DIR/lib/python${PYTHONPATH:+:$PYTHONPATH}

"$PYTHON" -c "import omni.asset_validator" >/dev/null 2>&1 || \
    "$PYTHON" -m pip install --quiet --disable-pip-version-check "omniverse-asset-validator>=1.15.1"

exec "$PYTHON" "$SCRIPT_DIR/perf_validators.py" "$@"
