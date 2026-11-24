#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build-release-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DSKIP_BUILD_TESTS=ON
cmake --build build-release-linux --parallel 4
mkdir -p dist
export QTDIR=/opt/qt
export BUILD_BIN="$PWD/build-release-linux/bin"
bash deploy/linux/make-portable.sh
SKIP_PORTABLE_REFRESH=1 bash deploy/linux/make-deb.sh
