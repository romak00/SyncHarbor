#!/usr/bin/env bash
set -euo pipefail

unameOut="$(uname -s)"
case "${unameOut}" in
    Linux*)
        sudo apt-get update
        sudo apt-get install -y \
            build-essential cmake ninja-build curl libcurl4-openssl-dev \
            libsqlite3-dev pkg-config git
        ;;
    Darwin*)
        brew update
        brew install cmake ninja curl sqlite3
        ;;
    MINGW*|MSYS*|CYGWIN*)
        echo "On Windows we recommend using vcpkg:"
        if [ ! -d "third_party/vcpkg" ]; then
          git clone https://github.com/Microsoft/vcpkg.git third_party/vcpkg
          pushd third_party/vcpkg
          ./bootstrap-vcpkg.sh
          popd
        fi
        third_party/vcpkg/vcpkg install curl sqlite3
        ;;
    *)
        echo "Unknown OS: ${unameOut}"
        exit 1
        ;;
esac

echo "Bootstrap done."