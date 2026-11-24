#!/usr/bin/env bash
set -euo pipefail
cmake -S . -B build-release-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DSKIP_BUILD_TESTS=ON
cmake --build build-release-linux --parallel 4
mkdir -p dist
cpack --config build-release-linux/CPackConfig.cmake -G DEB -B dist
