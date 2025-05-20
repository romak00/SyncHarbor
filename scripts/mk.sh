#!/bin/bash
set -euo pipefail

rm -rf build
mkdir  build
cd     build

cmake \
  -DCMAKE_BUILD_TYPE=Debug \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  ..
cmake --build . --parallel

#ctest --output-on-failure

cd ..
#./cloudsync config-file-initial test-conf