#!/usr/bin/env bash
set -euo pipefail
export QT_QPA_PLATFORM=offscreen
export LANG=C.UTF-8
# The bind-mounted checkout belongs to the runner user, not container root.
git config --global --add safe.directory "$PWD"
cmake -P cmake/tests/GitVersionTests.cmake
cmake -S . -B build-release-linux -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_INSTALL_PREFIX=/usr -DSKIP_BUILD_TESTS=ON -DENABLE_BLUETOOTH=ON
cmake --build build-release-linux --parallel 4
ctest --test-dir build-release-linux/src/unittests --output-on-failure -R '^BluetoothTests$'
mkdir -p dist
export QTDIR=/opt/qt
export BUILD_BIN="$PWD/build-release-linux/bin"
bash deploy/linux/make-portable.sh
# The Bluetooth bridge lives in the GUI, not deskconnect-core. Do not publish
# a release that silently omits its Qt runtime dependency.
test -f "$PWD/dist/DeskConnect/lib/libQt6Bluetooth.so.6"
SKIP_PORTABLE_REFRESH=1 bash deploy/linux/make-deb.sh
bash deploy/linux/make-appimage.sh
