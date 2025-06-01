#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <Debug|RelWithDebInfo|Release> [-- <extra-cmake-args>]"
    exit 1
fi

cfg="$1"; shift

if [[ "$cfg" == "Debug" ]]; then
  CMAKE_COVERAGE="-DENABLE_COVERAGE=ON"
else
  CMAKE_COVERAGE=""
fi

echo "Configuring ($cfg)…"
cmake -B build \
      -DCMAKE_BUILD_TYPE="$cfg" \
      -DCMAKE_POLICY_VERSION_MINIMUM=3.5 \
      $CMAKE_COVERAGE \
      "$@"

echo "Building ($cfg)…"
cpu=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
cmake --build build --config "$cfg" -- -j${cpu}

echo "Done."