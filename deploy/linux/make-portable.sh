#!/usr/bin/env bash
# Build a self-contained DeskConnect distribution (bundled Qt + runtime libs).
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
QTDIR="${QTDIR:-/home/hans/Qt/6.8.3/gcc_64}"
BUILD_BIN="${BUILD_BIN:-$ROOT/build/bin}"
OUT_DIR="${OUT_DIR:-$ROOT/dist/DeskConnect}"
ARCH="$(uname -m)"
VERSION="$(grep -E 'set\(CMAKE_PROJECT_VERSION|project\(deskflow' "$ROOT/CMakeLists.txt" 2>/dev/null | head -1 || true)"
VERSION="${VERSION:-1.26.0.9999}"
# Prefer git describe-ish from binary if available
if [[ -x "$BUILD_BIN/deskflow-core" ]]; then
  VERSION="$("$BUILD_BIN/deskflow-core" --version 2>/dev/null | head -1 | awk '{print $2}' || echo 1.26.0.9999)"
  VERSION="${VERSION%,}"
  VERSION="${VERSION%%[^0-9.vV]*}"
fi

if [[ ! -x "$BUILD_BIN/deskflow" || ! -x "$BUILD_BIN/deskflow-core" ]]; then
  echo "Missing binaries in $BUILD_BIN — build Release first." >&2
  exit 1
fi
if [[ ! -d "$QTDIR/lib" ]]; then
  echo "QTDIR not found: $QTDIR" >&2
  exit 1
fi

rm -rf "$OUT_DIR"
mkdir -p "$OUT_DIR"/{bin,lib,plugins,share/icons,share/applications,share/DeskConnect}

cp -a "$BUILD_BIN/deskflow" "$OUT_DIR/bin/"
cp -a "$BUILD_BIN/deskflow-core" "$OUT_DIR/bin/"

# ---- collect shared libs (exclude glibc / basic system) ----
is_system_lib() {
  case "$1" in
    *ld-linux*|*libc.so*|*libm.so*|*libdl.so*|*libpthread.so*|*librt.so*|*libresolv.so*| \
    *libgcc_s.so*|*libstdc++.so*|*libglib-2.0.so*|*libgobject-2.0.so*|*libgio-2.0.so*| \
    *libgmodule-2.0.so*|*libz.so*|*libzstd.so*|*libbz2.so*|*liblzma.so*| \
    *libX11.so*|*libXext.so*|*libXau.so*|*libXdmcp.so*|*libxcb.so*|*libX*|*libXi.so*| \
    *libGL.so*|*libGLX.so*|*libGLdispatch.so*|*libOpenGL.so*|*libEGL.so*| \
    *libdrm.so*|*libgbm.so*|*libfontconfig.so*|*libfreetype.so*|*libexpat.so*| \
    *libuuid.so*|*libpcre*|*libffi.so*|*libselinux.so*|*libmount.so*|*libblkid.so*| \
    *libnss*|*libnspr*|*libsystemd.so*|*libdbus-1.so*|*libudev.so*| \
    *libwayland-*|*libxkbcommon.so*|*libxkbcommon-x11.so*| \
    *libssl.so*|*libcrypto.so*|*libbrotli*|*libpng*|*libjpeg*|*libharfbuzz*| \
    *libgraphite*|*libmd4c*|*libdouble-conversion*)
      return 0
      ;;
  esac
  return 1
}

declare -A SEEN=()
queue=()

enqueue() {
  local f="$1"
  [[ -z "$f" || "$f" == "not" ]] && return
  [[ -e "$f" ]] || return
  local real
  real="$(readlink -f "$f")"
  [[ -n "${SEEN[$real]:-}" ]] && return
  SEEN[$real]=1
  queue+=("$real")
}

# Seed from binaries + Qt platform plugins we will ship
for b in "$OUT_DIR/bin/deskflow" "$OUT_DIR/bin/deskflow-core"; do
  while read -r _ arrow lib _; do
    [[ "$arrow" == "=>" ]] && enqueue "$lib"
  done < <(ldd "$b" 2>/dev/null || true)
done

# Always include Qt plugins we'll copy
for plug in \
  "$QTDIR/plugins/platforms/libqxcb.so" \
  "$QTDIR/plugins/platforms/libqwayland-generic.so" \
  "$QTDIR/plugins/platforms/libqwayland-egl.so" \
  "$QTDIR/plugins/platforms/libqoffscreen.so" \
  "$QTDIR/plugins/platforms/libqminimal.so" \
  "$QTDIR/plugins/imageformats/libqjpeg.so" \
  "$QTDIR/plugins/imageformats/libqico.so" \
  "$QTDIR/plugins/imageformats/libqsvg.so" \
  "$QTDIR/plugins/iconengines/libqsvgicon.so" \
  "$QTDIR/plugins/platformthemes/libqgtk3.so" \
  "$QTDIR/plugins/platformthemes/libqxdgdesktopportal.so" \
  "$QTDIR/plugins/xcbglintegrations/libqxcb-glx-integration.so" \
  "$QTDIR/plugins/xcbglintegrations/libqxcb-egl-integration.so"
do
  [[ -e "$plug" ]] || continue
  while read -r _ arrow lib _; do
    [[ "$arrow" == "=>" ]] && enqueue "$lib"
  done < <(ldd "$plug" 2>/dev/null || true)
done

# Also pull known non-system deps we built ourselves / Qt dlopens at runtime
for extra in \
  /usr/local/lib/x86_64-linux-gnu/libei.so.1 \
  /usr/local/lib/x86_64-linux-gnu/libportal.so.1 \
  /usr/lib/x86_64-linux-gnu/libxcb-cursor.so.0 \
  /lib/x86_64-linux-gnu/libxcb-cursor.so.0 \
  "$QTDIR/lib/libicuuc.so.73" \
  "$QTDIR/lib/libicui18n.so.73" \
  "$QTDIR/lib/libicudata.so.73" \
  "$QTDIR/lib/libQt6XcbQpa.so.6" \
  "$QTDIR/lib/libQt6WaylandClient.so.6" \
  "$QTDIR/lib/libQt6Svg.so.6"
do
  [[ -e "$extra" ]] || continue
  enqueue "$extra"
done

idx=0
while (( idx < ${#queue[@]} )); do
  lib="${queue[$idx]}"
  idx=$((idx + 1))
  if is_system_lib "$lib"; then
    # Still recurse into Qt libs even if path looks system-ish — skip only true system
    case "$lib" in
      /home/hans/Qt/*|/usr/local/*) ;;
      *) continue ;;
    esac
  fi
  # Copy Qt /usr/local and any other non-skipped
  case "$lib" in
    /lib/*|/usr/lib/*)
      # Only keep selected runtime libs resolved from system paths
      base="$(basename "$lib")"
      case "$base" in
        libei.so*|libportal.so*|libxcb-cursor.so*) ;;
        *) continue ;;
      esac
      ;;
  esac
  cp -a "$lib" "$OUT_DIR/lib/"
  # Also copy soname links
  dir="$(dirname "$lib")"
  base="$(basename "$lib")"
  # Follow one level of symlinks naming
  for link in "$dir/$base"* ; do
    [[ -e "$link" ]] || continue
    bn="$(basename "$link")"
    [[ -e "$OUT_DIR/lib/$bn" ]] || cp -a "$link" "$OUT_DIR/lib/" 2>/dev/null || true
  done
  while read -r _ arrow dep _; do
    [[ "$arrow" == "=>" ]] && enqueue "$dep"
  done < <(ldd "$lib" 2>/dev/null || true)
done

# Ensure key Qt libs exist (copy full needed set from QTDIR)
for need in \
  libQt6Core.so.6 libQt6Gui.so.6 libQt6Widgets.so.6 libQt6Network.so.6 libQt6DBus.so.6 \
  libQt6XcbQpa.so.6 libQt6WaylandClient.so.6 libQt6WaylandEglClientHwIntegration.so.6 \
  libQt6Svg.so.6 libicuuc.so.73 libicui18n.so.73 libicudata.so.73
do
  if [[ ! -e "$OUT_DIR/lib/$need" && -e "$QTDIR/lib/$need" ]]; then
    cp -a "$QTDIR/lib/$need"* "$OUT_DIR/lib/" 2>/dev/null || cp -a "$QTDIR/lib/$need" "$OUT_DIR/lib/"
  fi
done

# Prefer /usr/local libei/portal over older system; always ship xcb-cursor for X11/Qt6.5+
for need in libei.so.1 libportal.so.1 libxcb-cursor.so.0; do
  for dir in /usr/local/lib/x86_64-linux-gnu /usr/lib/x86_64-linux-gnu /lib/x86_64-linux-gnu; do
    if [[ -e "$dir/$need" ]]; then
      cp -a "$dir/$need"* "$OUT_DIR/lib/" 2>/dev/null || cp -a "$dir/$need" "$OUT_DIR/lib/"
      break
    fi
  done
done

# ---- plugins ----
mkdir -p "$OUT_DIR/plugins"/{platforms,imageformats,iconengines,platformthemes,xcbglintegrations,wayland-shell-integration,wayland-graphics-integration-client,wayland-decoration-client}
copy_plug_dir() {
  local src="$1" dst="$2"
  [[ -d "$src" ]] || return 0
  mkdir -p "$dst"
  find "$src" -maxdepth 1 -type f -name '*.so' -exec cp -a {} "$dst/" \;
}
copy_plug_dir "$QTDIR/plugins/platforms" "$OUT_DIR/plugins/platforms"
copy_plug_dir "$QTDIR/plugins/imageformats" "$OUT_DIR/plugins/imageformats"
copy_plug_dir "$QTDIR/plugins/iconengines" "$OUT_DIR/plugins/iconengines"
copy_plug_dir "$QTDIR/plugins/platformthemes" "$OUT_DIR/plugins/platformthemes"
copy_plug_dir "$QTDIR/plugins/xcbglintegrations" "$OUT_DIR/plugins/xcbglintegrations"
copy_plug_dir "$QTDIR/plugins/wayland-shell-integration" "$OUT_DIR/plugins/wayland-shell-integration"
copy_plug_dir "$QTDIR/plugins/wayland-graphics-integration-client" "$OUT_DIR/plugins/wayland-graphics-integration-client"
# GNOME/Wayland has no server-side decorations; without these plugins the window
# has no title bar, close button, or drag handle.
copy_plug_dir "$QTDIR/plugins/wayland-decoration-client" "$OUT_DIR/plugins/wayland-decoration-client"

# ---- rpath / qt.conf ----
cat > "$OUT_DIR/bin/qt.conf" <<'EOF'
[Paths]
Prefix = ..
Libraries = lib
Plugins = plugins
EOF

for bin in deskflow deskflow-core; do
  patchelf --set-rpath '$ORIGIN/../lib' "$OUT_DIR/bin/$bin"
done
# Fix plugin rpaths too
find "$OUT_DIR/plugins" -name '*.so' -print0 | while IFS= read -r -d '' p; do
  patchelf --set-rpath '$ORIGIN/../../lib' "$p" 2>/dev/null || true
done
find "$OUT_DIR/lib" -name '*.so*' -type f -print0 | while IFS= read -r -d '' p; do
  # Only patch ELF files
  if file "$p" | grep -q 'ELF'; then
    patchelf --set-rpath '$ORIGIN' "$p" 2>/dev/null || true
  fi
done

# ---- launcher ----
cat > "$OUT_DIR/DeskConnect" <<'EOF'
#!/bin/sh
set -e
HERE="$(CDPATH= cd -- "$(dirname "$(readlink -f "$0")")" && pwd)"
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
export QT_QPA_PLATFORM_PLUGIN_PATH="$HERE/plugins/platforms"
# Prefer Adwaita CSD on GNOME so the title bar matches the desktop.
export QT_WAYLAND_DECORATION="${QT_WAYLAND_DECORATION:-adwaita}"
exec "$HERE/bin/deskflow" "$@"
EOF
chmod 755 "$OUT_DIR/DeskConnect" "$OUT_DIR/bin/deskflow" "$OUT_DIR/bin/deskflow-core"

cat > "$OUT_DIR/deskflow-core" <<'EOF'
#!/bin/sh
set -e
HERE="$(CDPATH= cd -- "$(dirname "$(readlink -f "$0")")" && pwd)"
export LD_LIBRARY_PATH="$HERE/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$HERE/plugins"
exec "$HERE/bin/deskflow-core" "$@"
EOF
chmod 755 "$OUT_DIR/deskflow-core"

# ---- translations (I18N looks for ../share/deskflow/translations from bin/) ----
mkdir -p "$OUT_DIR/share/deskflow/translations"
TR_SRC="${BUILD_BIN%/bin}/translations"
if [[ -d "$TR_SRC" ]]; then
  cp -a "$TR_SRC"/deskflow_*.qm "$OUT_DIR/share/deskflow/translations/" 2>/dev/null || true
fi

# ---- desktop + icons ----
ICON_SRC="$ROOT/deploy/linux/org.deskconnect.deskconnect.png"
[[ -f "$ICON_SRC" ]] || ICON_SRC="$ROOT/deploy/linux/icons/hicolor/512x512/apps/org.deskconnect.deskconnect.png"
if [[ -f "$ICON_SRC" ]]; then
  cp -a "$ICON_SRC" "$OUT_DIR/share/icons/org.deskconnect.deskconnect.png"
  for sz in 48 64 128 256 512; do
    src="$ROOT/deploy/linux/icons/hicolor/${sz}x${sz}/apps/org.deskconnect.deskconnect.png"
    if [[ -f "$src" ]]; then
      mkdir -p "$OUT_DIR/share/icons/hicolor/${sz}x${sz}/apps"
      cp -a "$src" "$OUT_DIR/share/icons/hicolor/${sz}x${sz}/apps/"
    fi
  done
fi

cat > "$OUT_DIR/share/applications/org.deskconnect.deskconnect.desktop" <<EOF
[Desktop Entry]
Type=Application
Version=1.0
Name=DeskConnect
Comment=Mouse and keyboard sharing utility
Exec=DeskConnect
Icon=org.deskconnect.deskconnect
Terminal=false
Categories=Utility;
Keywords=keyboard;mouse;sharing;network;share;
Name[zh_CN]=DeskConnect
Comment[zh_CN]=键鼠共享工具
EOF

cat > "$OUT_DIR/README.txt" <<EOF
DeskConnect portable package
Version: $VERSION
Arch: $ARCH

Run directly:
  ./DeskConnect

Or install system-wide:
  sudo ./install.sh

Uninstall:
  sudo ./uninstall.sh
EOF

cat > "$OUT_DIR/install.sh" <<'EOF'
#!/bin/sh
set -e
HERE="$(CDPATH= cd -- "$(dirname "$0")" && pwd)"
DEST="${DESTDIR:-}/opt/DeskConnect"
echo "Installing to $DEST ..."
mkdir -p "$DEST"
# Copy payload but skip install scripts recursion issues
rsync -a --delete \
  --exclude install.sh --exclude uninstall.sh \
  "$HERE"/ "$DEST"/
ln -sfn "$DEST/DeskConnect" /usr/local/bin/DeskConnect
ln -sfn "$DEST/DeskConnect" /usr/local/bin/deskflow
ln -sfn "$DEST/deskflow-core" /usr/local/bin/deskflow-core

# Desktop entry with absolute Exec
mkdir -p /usr/local/share/applications
sed "s|^Exec=DeskConnect|Exec=$DEST/DeskConnect|" \
  "$DEST/share/applications/org.deskconnect.deskconnect.desktop" \
  > /usr/local/share/applications/org.deskconnect.deskconnect.desktop

# Icons
if [ -d "$DEST/share/icons/hicolor" ]; then
  mkdir -p /usr/local/share/icons/hicolor
  rsync -a "$DEST/share/icons/hicolor/" /usr/local/share/icons/hicolor/
  gtk-update-icon-cache -f /usr/local/share/icons/hicolor 2>/dev/null || true
fi
update-desktop-database /usr/local/share/applications 2>/dev/null || true
echo "Done. Launch from app menu (DeskConnect) or: DeskConnect"
EOF
chmod 755 "$OUT_DIR/install.sh"

cat > "$OUT_DIR/uninstall.sh" <<'EOF'
#!/bin/sh
set -e
DEST="${DESTDIR:-}/opt/DeskConnect"
rm -f /usr/local/bin/DeskConnect /usr/local/bin/deskflow /usr/local/bin/deskflow-core
rm -f /usr/local/share/applications/org.deskconnect.deskconnect.desktop
rm -f /usr/local/share/icons/hicolor/*/apps/org.deskconnect.deskconnect.png
rm -rf "$DEST"
gtk-update-icon-cache -f /usr/local/share/icons/hicolor 2>/dev/null || true
update-desktop-database /usr/local/share/applications 2>/dev/null || true
echo "DeskConnect removed."
EOF
chmod 755 "$OUT_DIR/uninstall.sh"

# ---- pack ----
DIST_ROOT="$(dirname "$OUT_DIR")"
ARCHIVE="$DIST_ROOT/DeskConnect-${VERSION}-linux-${ARCH}.tar.gz"
tar -C "$DIST_ROOT" -czf "$ARCHIVE" "$(basename "$OUT_DIR")"

echo "Payload: $OUT_DIR"
echo "Archive: $ARCHIVE"
du -sh "$OUT_DIR" "$ARCHIVE"
# Smoke test
export LD_LIBRARY_PATH="$OUT_DIR/lib"
"$OUT_DIR/bin/deskflow-core" --version
