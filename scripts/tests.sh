#!/usr/bin/env bash
set -euo pipefail

cfg="${1:-Debug}"
pushd build

if command -v nproc &>/dev/null; then
  PARALLEL=$(nproc)
else
  PARALLEL=$(sysctl -n hw.ncpu)
fi

echo "Running DatabaseTests in parallel…"
ctest -C "$cfg" \
  --output-on-failure \
  --parallel "$PARALLEL" \
  -R "^DatabaseTest\."

echo "Running LocalStorageTests sequentially…"
ctest -C "$cfg" \
  --output-on-failure \
  -R "^LocalStorageTest\."

popd