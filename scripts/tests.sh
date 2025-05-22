#!/usr/bin/env bash
set -euo pipefail

cfg="${1:-Debug}"
pushd build
ctest --output-on-failure --parallel $(nproc || sysctl -n hw.ncpu) -C "$cfg"
popd