#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 ]]; then
    echo "Usage: $0 <Debug|RelWithDebInfo|Release> [--target <target>]"
    exit 1
fi

cfg="$1"; shift
target="all"
if [[ $# -eq 2 && "$1" == "--target" ]]; then
    target="$2"
fi

echo "Configuring ($cfg)…"
cmake -B build -DCMAKE_BUILD_TYPE="$cfg" "$@" -DCMAKE_POLICY_VERSION_MINIMUM=3.5

echo "Building ($cfg)…"
cpu=$(nproc 2>/dev/null || sysctl -n hw.ncpu)
cmake --build build --config "$cfg" --target "$target" -- -j${cpu}

echo "Done."