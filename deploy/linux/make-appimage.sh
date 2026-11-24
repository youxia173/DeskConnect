#!/usr/bin/env bash
# Package the already built portable tree; no FUSE or privileged Docker needed.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PORTABLE="${PORTABLE:-$ROOT/dist/DeskConnect}"
TOOL="${APPIMAGETOOL:-/opt/appimage/appimagetool.AppImage}"
RUNTIME="${APPIMAGE_RUNTIME:-/opt/appimage/runtime-x86_64}"
[[ "$(uname -m)" == x86_64 ]] || { echo 'This AppImage toolchain targets x86_64' >&2; exit 1; }
test -x "$PORTABLE/DeskConnect"
test -x "$PORTABLE/bin/deskconnect-core"
VERSION="${VERSION_OVERRIDE:-$(QT_QPA_PLATFORM=offscreen "$PORTABLE/deskconnect-core" --version | awk 'NR == 1 {print $2}')}"
VERSION="${VERSION#v}"
VERSION="${VERSION%,}"
[[ "$VERSION" =~ ^[0-9]+(\.[0-9]+){2,3}$ ]] || { echo "Invalid package version: $VERSION" >&2; exit 1; }

WORK="$(mktemp -d "$ROOT/dist/appimage-build.XXXXXX")"
trap 'rm -rf -- "$WORK"' EXIT
APPDIR="$WORK/DeskConnect.AppDir"
mkdir -p "$APPDIR"
rsync -a --exclude install.sh --exclude uninstall.sh "$PORTABLE/" "$APPDIR/"
cat > "$APPDIR/AppRun" <<'EOF'
#!/bin/sh
set -eu
HERE="$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)"
exec "$HERE/DeskConnect" "$@"
EOF
chmod 755 "$APPDIR/AppRun"
sed 's/^Exec=.*/Exec=DeskConnect/' "$ROOT/deploy/linux/org.deskconnect.deskconnect.desktop" \
  > "$APPDIR/org.deskconnect.deskconnect.desktop"
cp "$ROOT/deploy/linux/org.deskconnect.deskconnect.png" "$APPDIR/org.deskconnect.deskconnect.png"
ln -s org.deskconnect.deskconnect.png "$APPDIR/.DirIcon"
desktop-file-validate "$APPDIR/org.deskconnect.deskconnect.desktop"

OUTPUT="$ROOT/dist/DeskConnect-${VERSION}-linux-x86_64.AppImage"
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$TOOL" --runtime-file "$RUNTIME" "$APPDIR" "$OUTPUT"
chmod 755 "$OUTPUT"
# Verify the actual image without requiring /dev/fuse in GitHub's container.
mkdir "$WORK/verify"
(
  cd "$WORK/verify"
  "$OUTPUT" --appimage-extract >/dev/null
  test -x squashfs-root/AppRun
  test -x squashfs-root/bin/deskflow
  test -f squashfs-root/plugins/platforms/libqxcb.so
  sh -n squashfs-root/AppRun
  QT_QPA_PLATFORM=offscreen timeout 30 squashfs-root/deskconnect-core --version
)
echo "AppImage: $OUTPUT"
