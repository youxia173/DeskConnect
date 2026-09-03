#!/usr/bin/env bash
# Build a self-contained .deb from the portable DeskConnect tree (bundled Qt).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PORTABLE="${PORTABLE:-$ROOT/dist/DeskConnect}"
DIST_ROOT="$(dirname "$PORTABLE")"
ARCH_DEB="${ARCH_DEB:-amd64}"

if [[ ! -x "$PORTABLE/DeskConnect" || ! -x "$PORTABLE/bin/deskflow-core" ]]; then
  echo "Portable tree missing — running make-portable.sh first..." >&2
  QTDIR="${QTDIR:-/home/hans/Qt/6.8.3/gcc_64}" "$ROOT/deploy/linux/make-portable.sh"
fi

VERSION="$("$PORTABLE/bin/deskflow-core" --version 2>/dev/null | head -1 | awk '{print $2}' || echo 1.26.0.9999)"
# Debian version: drop leading 'v'
VERSION="${VERSION#v}"
PKG_NAME="deskconnect"
PKG_ROOT="$DIST_ROOT/${PKG_NAME}_${VERSION}_${ARCH_DEB}"
DEB_PATH="$DIST_ROOT/${PKG_NAME}_${VERSION}_${ARCH_DEB}.deb"

rm -rf "$PKG_ROOT"
mkdir -p "$PKG_ROOT/DEBIAN" \
  "$PKG_ROOT/opt" \
  "$PKG_ROOT/usr/bin" \
  "$PKG_ROOT/usr/share/applications" \
  "$PKG_ROOT/usr/share/icons/hicolor"

# Payload
rsync -a --delete \
  --exclude install.sh --exclude uninstall.sh \
  "$PORTABLE"/ "$PKG_ROOT/opt/DeskConnect/"

# PATH helpers
ln -sfn /opt/DeskConnect/DeskConnect "$PKG_ROOT/usr/bin/DeskConnect"
ln -sfn /opt/DeskConnect/DeskConnect "$PKG_ROOT/usr/bin/deskflow"
ln -sfn /opt/DeskConnect/deskflow-core "$PKG_ROOT/usr/bin/deskflow-core"

# Desktop entry with absolute Exec
sed 's|^Exec=DeskConnect|Exec=/opt/DeskConnect/DeskConnect|' \
  "$PORTABLE/share/applications/org.deskconnect.deskconnect.desktop" \
  > "$PKG_ROOT/usr/share/applications/org.deskconnect.deskconnect.desktop"

# Icons
if [[ -d "$PORTABLE/share/icons/hicolor" ]]; then
  rsync -a "$PORTABLE/share/icons/hicolor/" "$PKG_ROOT/usr/share/icons/hicolor/"
fi
if [[ -f "$PORTABLE/share/icons/org.deskconnect.deskconnect.png" ]]; then
  mkdir -p "$PKG_ROOT/usr/share/icons/hicolor/512x512/apps"
  cp -a "$PORTABLE/share/icons/org.deskconnect.deskconnect.png" \
    "$PKG_ROOT/usr/share/icons/hicolor/512x512/apps/"
fi

INSTALLED_SIZE="$(du -sk "$PKG_ROOT/opt/DeskConnect" | awk '{print $1}')"

cat > "$PKG_ROOT/DEBIAN/control" <<EOF
Package: ${PKG_NAME}
Version: ${VERSION}
Section: utils
Priority: optional
Architecture: ${ARCH_DEB}
Installed-Size: ${INSTALLED_SIZE}
Maintainer: DeskConnect <youxia173@users.noreply.github.com>
Homepage: https://github.com/youxia173/DeskConnect
Depends: libc6, libgl1, libegl1, libx11-6, libxcb1, libxkbcommon0, libwayland-client0, libssl3, libdbus-1-3, libfontconfig1, libfreetype6, libglib2.0-0
Recommends: libxcb-cursor0
Description: DeskConnect keyboard and mouse sharing utility
 DeskConnect is a Deskflow-based tool for sharing a keyboard and mouse
 across computers. This package bundles Qt 6.8 so it runs on Ubuntu 24.04
 without requiring a newer system Qt.
EOF

cat > "$PKG_ROOT/DEBIAN/postinst" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 755 "$PKG_ROOT/DEBIAN/postinst"

cat > "$PKG_ROOT/DEBIAN/postrm" <<'EOF'
#!/bin/sh
set -e
if command -v update-desktop-database >/dev/null 2>&1; then
  update-desktop-database -q /usr/share/applications || true
fi
if command -v gtk-update-icon-cache >/dev/null 2>&1; then
  gtk-update-icon-cache -f /usr/share/icons/hicolor >/dev/null 2>&1 || true
fi
exit 0
EOF
chmod 755 "$PKG_ROOT/DEBIAN/postrm"

# Root-owned package contents
if command -v fakeroot >/dev/null 2>&1; then
  fakeroot dpkg-deb --build --root-owner-group "$PKG_ROOT" "$DEB_PATH"
else
  dpkg-deb --build --root-owner-group "$PKG_ROOT" "$DEB_PATH"
fi

echo "Deb: $DEB_PATH"
dpkg-deb -I "$DEB_PATH"
du -sh "$DEB_PATH"
