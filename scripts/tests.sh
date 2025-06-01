#!/usr/bin/env bash
set -euo pipefail

cfg="${1:-Debug}"
pushd build

if command -v nproc &>/dev/null; then
  PARALLEL=$(nproc)
else
  PARALLEL=$(sysctl -n hw.ncpu)
fi

echo "Running DatabaseUnitTests in parallel..."
ctest -C "$cfg" \
  --output-on-failure \
  --parallel "$PARALLEL" \
  -R "^DatabaseUnitTest\."

echo "Running LocalStorageUnitTests sequentially..."
ctest -C "$cfg" \
  --output-on-failure \
  -R "^LocalStorageUnitTest\."

echo "Running HttpServerUnitTests sequentially..."
ctest -C "$cfg" \
  --output-on-failure \
  -R "^HttpServerUnitTest\."

echo "Running CallbackDispatcherUnitTests in parallel..."
ctest -C "$cfg" \
  --output-on-failure \
  --parallel "$PARALLEL" \
  -R "^CallbackDispatcherUnitTest\."

echo "Running RequestHandleUnitTests in parallel..."
ctest -C "$cfg" \
  --output-on-failure \
  --parallel "$PARALLEL" \
  -R "^RequestHandleUnitTest\."

echo "Running ActiveCountUnitTests sequentially..."
ctest -C "$cfg" \
  --output-on-failure \
  -R "^ActiveCountUnitTest\."

echo "Running EventRegistryUnitTests sequentially..."
ctest -C "$cfg" \
  --output-on-failure \
  -R "^EventRegistryUnitTest\."

echo "Running UtilsUnitTests in parallel..."
ctest -C "$cfg" \
  --output-on-failure \
  --parallel "$PARALLEL" \
  -R "^UtilsUnitTest\."

echo "Running SyncManagerUnitTests sequentially..."
ctest -C "$cfg" \
  --output-on-failure \
  -R "^SyncManagerUnitTest\."

echo "Running HttpClientIntegrationTests sequentially..."
ctest -C "$cfg" \
  --output-on-failure \
  -R "^HttpClientIntegrationTest\."

echo "Running LocalStorageIntegrationTests sequentially..."
ctest -C "$cfg" \
  --output-on-failure \
  -R "^LocalStorageIntegrationTest\."

popd