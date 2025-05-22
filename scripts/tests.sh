#!/usr/bin/env bash
set -euo pipefail

cfg="${1:-Debug}"
pushd build

echo "Running DatabaseTests in parallel…"
ctest -C "$cfg" \
  --output-on-failure \
  --parallel "$(nproc || sysctl -n hw.ncpu)" \
  -R "^DatabaseTest\."

echo "Running LocalStorageTests sequentially…"
ctest -C "$cfg" \
  --output-on-failure \
  -R "^LocalStorageTest\."

popd