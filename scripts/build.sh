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
cmake -B build -DCMAKE_BUILD_TYPE="$cfg" "$@"

echo "Building ($cfg)…"
cmake --build build --config "$cfg" --target "$target" -- -j$(nproc || sysctl -n hw.ncpu)

echo "Done."