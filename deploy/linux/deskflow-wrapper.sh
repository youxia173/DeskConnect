#!/bin/sh
# Launcher for DeskConnect when Qt is not the system default (e.g. Ubuntu 24.04).
QTDIR="${DESKCONNECT_QTDIR:-/home/hans/Qt/6.8.3/gcc_64}"
APP_NAME="$(basename "$0")"
REAL_BIN="/usr/lib/deskconnect/${APP_NAME}"

export LD_LIBRARY_PATH="${QTDIR}/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="${QTDIR}/plugins${QT_PLUGIN_PATH:+:$QT_PLUGIN_PATH}"

exec "$REAL_BIN" "$@"
