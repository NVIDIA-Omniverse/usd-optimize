#!/bin/bash

set -e

# Setup the build environment
VENV=./_build/host-deps/py_package_venv
source "$VENV/bin/activate"

# Bake the libs into the wheel
auditwheel --version
auditwheel "$@"
