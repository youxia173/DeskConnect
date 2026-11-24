#!/usr/bin/env bash
# Activate the installed debug APK's input bridge over an authorized USB ADB connection.
set -euo pipefail
umask 077

if (( $# > 1 )); then
    printf 'Usage: %s [device-serial]\n' "$0" >&2
    exit 2
fi

adb_bin=${ADB:-adb}
device_serial=${1:-${SERIAL:-}}
if ! command -v "$adb_bin" >/dev/null 2>&1; then
    printf 'ADB was not found. Set ADB to the adb executable path.\n' >&2
    exit 1
fi
if ! command -v python3 >/dev/null 2>&1; then
    printf 'python3 is required to validate the private bridge configuration.\n' >&2
    exit 1
fi

adb_args=()
if [[ -n "$device_serial" ]]; then adb_args=(-s "$device_serial"); fi
adb_cmd() { "$adb_bin" "${adb_args[@]}" "$@"; }

package=com.deskconnect.app
remote_dir=/data/local/tmp/deskconnect-input
task_tmp=$(mktemp -d "${TMPDIR:-/tmp}/deskconnect-input.XXXXXXXX")
cleanup() {
    # Remove only files created inside this invocation's private directory.
    rm -f -- "$task_tmp/bootstrap.json" "$task_tmp/start.sh"
    rmdir -- "$task_tmp" 2>/dev/null || true
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

adb_cmd get-state >/dev/null
adb_cmd shell appops set "$package" SYSTEM_ALERT_WINDOW allow >/dev/null
# Close the previous control socket first, so its helper can release any held
# keys/touch before it is replaced. A process signal alone cannot guarantee that.
adb_cmd shell am start -n "$package/.ui.MainActivity" --ez enable_input false >/dev/null
sleep 0.5

validate_config() {
    python3 - "$task_tmp/bootstrap.json" <<'PY'
import json
import re
import sys

try:
    with open(sys.argv[1], encoding="utf-8") as source:
        value = json.load(source)
    valid = (
        isinstance(value, dict)
        and type(value.get("port")) is int
        and 1024 <= value["port"] <= 65535
        and isinstance(value.get("token"), str)
        and re.fullmatch(r"[0-9a-f]{64}", value["token"]) is not None
    )
except (OSError, ValueError, TypeError):
    valid = False
sys.exit(0 if valid else 1)
PY
}

# run-as deliberately requires a debug APK. Never print the token or pass it in
# a process argument; exec-out preserves the file bytes without a terminal.
config_ready=false
for ((attempt = 0; attempt < 20; attempt++)); do
    if adb_cmd exec-out run-as "$package" cat files/input-bridge.json \
        >"$task_tmp/bootstrap.json" 2>/dev/null && validate_config; then
        config_ready=true
        break
    fi
    sleep 0.25
done
if [[ "$config_ready" != true ]]; then
    printf 'Cannot read a valid bridge configuration. Install the debug APK and open DeskConnect first; run-as must be available.\n' >&2
    exit 1
fi

apk_path=
while IFS= read -r package_line; do
    package_line=${package_line%$'\r'}
    if [[ "$package_line" == package:*/base.apk ]]; then
        apk_path=${package_line#package:}
        break
    fi
done < <(adb_cmd shell pm path "$package")
if [[ ! "$apk_path" =~ ^/data/app/[a-zA-Z0-9_./=+~-]+/base\.apk$ ]]; then
    printf 'Cannot locate the installed DeskConnect base APK.\n' >&2
    exit 1
fi

cat >"$task_tmp/start.sh" <<'REMOTE'
#!/system/bin/sh
set -e
umask 077
cd /data/local/tmp/deskconnect-input
helper_class=com.deskconnect.app.input.ShellInputMain
if [ -f helper.pid ]; then
    old_pid=$(cat helper.pid)
    case "$old_pid" in
        ''|*[!0-9]*) ;;
        *)
            if [ -r "/proc/$old_pid/cmdline" ] &&
                tr '\000' '\n' <"/proc/$old_pid/cmdline" | grep -Fxq "$helper_class"; then
                kill "$old_pid"
                sleep 1
            fi
            ;;
    esac
fi
export CLASSPATH="$1"
nohup /system/bin/app_process / "$helper_class" \
    /data/local/tmp/deskconnect-input/bootstrap.json \
    </dev/null >helper.log 2>&1 &
echo $! >helper.pid
REMOTE

adb_cmd shell mkdir -p "$remote_dir"
adb_cmd shell chmod 700 "$remote_dir"
adb_cmd push "$task_tmp/bootstrap.json" "$remote_dir/bootstrap.json" >/dev/null
adb_cmd push "$task_tmp/start.sh" "$remote_dir/start.sh" >/dev/null
adb_cmd shell chmod 600 "$remote_dir/bootstrap.json" "$remote_dir/start.sh"
adb_cmd shell sh "$remote_dir/start.sh" "$apk_path"

helper_log=
for ((attempt = 0; attempt < 12; attempt++)); do
    sleep 0.5
    helper_log=$(adb_cmd shell cat "$remote_dir/helper.log")
    if [[ "$helper_log" == *"input helper ready"* ]]; then
        adb_cmd shell am start -n "$package/.ui.MainActivity" --ez enable_input true >/dev/null
        printf 'ADB input helper activated. Confirm the app shows ADB enhanced mode ready.\n'
        exit 0
    fi
done
printf 'Helper did not report readiness. Diagnostic log:\n%s\n' "$helper_log" >&2
exit 1
