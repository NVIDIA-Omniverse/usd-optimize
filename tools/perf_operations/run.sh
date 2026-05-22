#!/bin/bash
# Wrapper that sets up LD_LIBRARY_PATH / PYTHONPATH and invokes run_operations.py
# under the build's bundled Python. Pass through all args to the python script.
#
# Examples:
#   tools/perf_operations/run.sh run path/to/asset.usd \
#       --config '[{"operation":"meshCleanup","mergeVertices":true}]' \
#       --output out.usda
#   tools/perf_operations/run.sh run path/to/asset.usd --pipeline memory-reduction
#   tools/perf_operations/run.sh list-pipelines
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

exec "$PYTHON" "$SCRIPT_DIR/run_operations.py" "$@"
