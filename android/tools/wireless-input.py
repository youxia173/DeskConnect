#!/usr/bin/env python3
"""Restore DeskConnect input using a previously paired Android wireless ADB device.

Python 3.10+, Android 11+, a debug APK and current platform-tools are required.
Pairing is explicit; the watcher never pairs, scans subnets, or enables debugging.
"""
import argparse
import getpass
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import time
import threading
import xml.etree.ElementTree as ET

PACKAGE = "com.deskconnect.app"
MAIN = PACKAGE + "/.ui.MainActivity"
HELPER = PACKAGE + ".input.ShellInputMain"
REMOTE = "/data/local/tmp/deskconnect-input"


def emit(message):
    print(time.strftime("%Y-%m-%d %H:%M:%S"), message, flush=True)


class Adb:
    def __init__(self, executable, target=None):
        self.executable, self.target = executable, target
        self.allowed = lambda: True

    def run(self, *args, timeout=15, input_text=None, check=True):
        if not self.allowed():
            raise RuntimeError("Recovery registration ended")
        command = [self.executable] + (["-s", self.target] if self.target else []) + list(args)
        result = subprocess.run(command, input=input_text, capture_output=True, text=True,
                                encoding="utf-8", errors="replace", timeout=timeout,
                                creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
        if check and result.returncode:
            # Never echo command bodies, private bootstrap data, or pairing codes.
            raise RuntimeError("ADB operation failed: " + args[0])
        return result.stdout.strip()


def validate_serial(value):
    if not re.fullmatch(r"[A-Za-z0-9_]+", value):
        raise ValueError("Use the physical device serial, not an IP address, for --serial")
    return value


def endpoint(value):
    # Deliberately require the displayed IPv4 address and port, not shell text.
    match = re.fullmatch(r"(\d{1,3}(?:\.\d{1,3}){3}):(\d{1,5})", value)
    if not match or any(int(part) > 255 for part in match[1].split(".")) or not 1 <= int(match[2]) <= 65535:
        raise ValueError("Expected an IPv4 address and port, for example 192.168.1.20:37123")
    return value


def discover_targets(services, serial):
    found = []
    for line in services.splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[0].startswith("adb-" + serial + "-") and fields[1].rstrip(".") == "_adb-tls-connect._tcp":
            try:
                found.append(endpoint(fields[2]))
            except ValueError:
                pass
    return sorted(set(found))


def dns_name(data, offset):
    labels, end, visited = [], None, set()
    while True:
        if offset >= len(data) or offset in visited or len(visited) > 128:
            raise ValueError("Invalid DNS name")
        visited.add(offset)
        size = data[offset]
        if size & 0xc0 == 0xc0:
            if offset + 1 >= len(data):
                raise ValueError("Truncated DNS pointer")
            if end is None:
                end = offset + 2
            offset = ((size & 0x3f) << 8) | data[offset + 1]
        elif size == 0:
            return ".".join(labels), end if end is not None else offset + 1
        elif size <= 63 and offset + 1 + size <= len(data):
            labels.append(data[offset + 1:offset + 1 + size].decode("ascii"))
            offset += 1 + size
        else:
            raise ValueError("Invalid DNS label")


def mdns_targets(data, peer_ip, serial):
    if len(data) < 12:
        return []
    _, flags, questions, answers, authorities, additional = struct.unpack_from("!6H", data)
    if not flags & 0x8000 or questions + answers + authorities + additional > 256:
        return []
    try:
        offset, addresses, services = 12, {}, []
        for _ in range(questions):
            _, offset = dns_name(data, offset)
            offset += 4
        for _ in range(answers + authorities + additional):
            name, offset = dns_name(data, offset)
            kind, _, ttl, length = struct.unpack_from("!HHIH", data, offset)
            offset += 10
            stop = offset + length
            if stop > len(data):
                return []
            if ttl and kind == 1 and length == 4:
                addresses[name] = socket.inet_ntoa(data[offset:stop])
            elif ttl and kind == 33 and length >= 7 and name.startswith("adb-" + serial + "-" if serial else "adb-") and name.endswith("._adb-tls-connect._tcp.local"):
                port = struct.unpack_from("!H", data, offset + 4)[0]
                target, _ = dns_name(data, offset + 6)
                if port:
                    services.append((target, port))
            offset = stop
        return [endpoint(f"{addresses.get(target, peer_ip)}:{port}") for target, port in services]
    except (ValueError, UnicodeError, struct.error, OSError):
        return []


def discover_mdns(serial, cached_address=None):
    name = b"".join(bytes([len(part)]) + part.encode("ascii") for part in "_adb-tls-connect._tcp.local".split(".")) + b"\0"
    query = struct.pack("!6H", 0, 0, 1, 0, 0, 0) + name + struct.pack("!HH", 12, 0x8001)
    targets = ["224.0.0.251"]
    if cached_address:
        targets.insert(0, endpoint(cached_address).rsplit(":", 1)[0])
    found = set()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as channel:
        channel.bind(("0.0.0.0", 0))
        channel.settimeout(0.25)
        for address in targets:
            channel.sendto(query, (address, 5353))
        deadline = time.monotonic() + 1.5
        while time.monotonic() < deadline:
            try:
                data, peer = channel.recvfrom(9000)
                found.update(mdns_targets(data, peer[0], serial))
                if found:
                    break
            except socket.timeout:
                pass
    return sorted(found)


def consent(device):
    xml = device.run("exec-out", "run-as", PACKAGE, "cat", "shared_prefs/input-control.xml", check=False)
    try:
        values = {node.get("name"): node.get("value") == "true" for node in ET.fromstring(xml).findall("boolean")}
        registration = getattr(device, "registration", None)
        if isinstance(registration, str):
            strings = {node.get("name"): node.text for node in ET.fromstring(xml).findall("string")}
            if strings.get("recoveryId") != registration or not values.get("resumeConnection", True):
                return False
        return values.get("enabled", False) and values.get("wirelessRecovery", False)
    except ET.ParseError:
        return False


def installed_apk(device):
    for line in device.run("shell", "pm", "path", PACKAGE).splitlines():
        path = line.removeprefix("package:")
        if re.fullmatch(r"/data/app/[A-Za-z0-9_./=+~\-]+/base\.apk", path):
            return path
    raise RuntimeError("DeskConnect debug APK is not installed")


def helper_alive(device, apk):
    script = """cd /data/local/tmp/deskconnect-input 2>/dev/null || exit 1
test -f helper.pid && test -f apk.path || exit 1
old_pid=$(cat helper.pid)
case "$old_pid" in ''|*[!0-9]*) exit 1;; esac
tr '\\000' '\\n' < /proc/$old_pid/cmdline 2>/dev/null | grep -Fxq com.deskconnect.app.input.ShellInputMain || exit 1
cat apk.path
"""
    return device.run("shell", "sh", input_text=script, check=False) == apk


def activate(device, apk):
    if not consent(device):
        return False
    device.run("shell", "am", "start", "-n", MAIN, "--ez", "restart_input_bridge", "true")
    time.sleep(0.6)
    raw = device.run("exec-out", "run-as", PACKAGE, "cat", "files/input-bridge.json")
    config = json.loads(raw)
    if not isinstance(config, dict) or type(config.get("port")) is not int or not 1024 <= config["port"] <= 65535 or not isinstance(config.get("token"), str) or not re.fullmatch(r"[0-9a-f]{64}", config["token"]):
        raise RuntimeError("Invalid private input bridge configuration")
    if not consent(device):
        return False
    script = """#!/system/bin/sh
set -e
cd /data/local/tmp/deskconnect-input
if [ -f helper.pid ]; then
    old_pid=$(cat helper.pid)
    case "$old_pid" in
        ''|*[!0-9]*) ;;
        *) if tr '\\000' '\\n' < /proc/$old_pid/cmdline 2>/dev/null | grep -Fxq com.deskconnect.app.input.ShellInputMain; then kill "$old_pid"; sleep 1; fi ;;
    esac
fi
export CLASSPATH='__APK__'
nohup /system/bin/app_process / com.deskconnect.app.input.ShellInputMain /data/local/tmp/deskconnect-input/bootstrap.json </dev/null >helper.log 2>&1 &
echo $! >helper.pid
printf '%s' "$CLASSPATH" >apk.path
""".replace("__APK__", apk)
    with tempfile.TemporaryDirectory(prefix="deskconnect-wireless-") as temporary:
        folder = Path(temporary)
        for name, content in (("bootstrap.json", raw), ("start.sh", script)):
            path = folder / name
            path.write_text(content, encoding="utf-8", newline="\n")
            os.chmod(path, 0o600)
        device.run("shell", "mkdir", "-p", REMOTE)
        device.run("shell", "chmod", "700", REMOTE)
        for name in ("bootstrap.json", "start.sh"):
            device.run("push", str(folder / name), REMOTE + "/" + name)
        device.run("shell", "chmod", "600", REMOTE + "/bootstrap.json")
        if not consent(device):
            return False
        device.run("shell", "sh", REMOTE + "/start.sh")
    time.sleep(1.5)
    if not helper_alive(device, apk):
        raise RuntimeError("Input helper did not start; inspect the helper log on the phone")
    if consent(device):
        device.run("shell", "am", "start", "-n", MAIN, "--ez", "restore_connection", "true")
    emit("Input helper restored; phone will reconnect to its saved server unless manually disconnected.")
    return True


def connect_target(adb, serial, configured=None, cached=None, allow_usb=False):
    try:
        services = adb.run("mdns", "services", check=False)
    except (RuntimeError, subprocess.TimeoutExpired):
        services = ""
    candidates = discover_targets(services, serial)
    if configured:
        candidates.append(configured)
    # Also recognize already connected TLS mDNS targets without depending on LAN multicast.
    for line in adb.run("devices").splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            if fields[0].startswith("adb-" + serial + "-"):
                candidates.append(fields[0])
            else:
                try:
                    candidates.append(endpoint(fields[0]))
                except ValueError:
                    pass
    def verified(candidate):
        adb.run("connect", candidate, check=False)
        device = Adb(adb.executable, candidate)
        device.allowed = getattr(adb, "allowed", lambda: True)
        try:
            actual = device.run("shell", "getprop", "ro.serialno")
            if actual == serial:
                return device
        except (RuntimeError, subprocess.TimeoutExpired):
            pass
        return None
    for candidate in dict.fromkeys(candidates):
        device = verified(candidate)
        if device:
            return device
    # Query the standard DNS-SD service directly when ADB's mDNS backend cannot
    # discover it. Only a previously verified phone address is used for unicast.
    for candidate in discover_mdns(serial, cached or configured):
        device = verified(candidate)
        if device:
            return device
    if allow_usb:
        device = Adb(adb.executable, serial)
        device.allowed = getattr(adb, "allowed", lambda: True)
        try:
            if device.run("shell", "getprop", "ro.serialno") == serial:
                return device
        except (RuntimeError, subprocess.TimeoutExpired):
            pass
    return None


def watch(args):
    adb = Adb(args.adb)
    state_path = getattr(args, "state_file", None)
    cached = None
    if state_path and state_path.exists():
        try:
            state = json.loads(state_path.read_text(encoding="utf-8"))
            if state.get("serial") == args.serial:
                cached = endpoint(state["address"])
        except (ValueError, KeyError, OSError, AttributeError):
            pass
    # An OS-held local socket prevents two watchers from repeatedly restarting the helper.
    import hashlib
    lock_port = 35000 + int(hashlib.sha256(args.serial.encode()).hexdigest()[:4], 16) % 20000
    with socket.socket() as lock:
        try:
            lock.bind(("127.0.0.1", lock_port))
        except OSError:
            raise RuntimeError("A watcher for this phone is already running (or its lock port is occupied)")
        last_status = None
        while True:
            try:
                device = connect_target(adb, args.serial, args.address, cached)
                if device and state_path:
                    try:
                        address = endpoint(device.target)
                        if address != cached:
                            state_path.parent.mkdir(parents=True, exist_ok=True)
                            temporary_state = state_path.with_suffix(".tmp")
                            temporary_state.write_text(json.dumps({"serial": args.serial, "address": address}), encoding="utf-8")
                            temporary_state.replace(state_path)
                            cached = address
                    except ValueError:
                        pass
                if device is None:
                    status = "Waiting for the paired phone; enable Wireless debugging and connect to the same LAN."
                elif not consent(device):
                    status = "Paused: enable keyboard control and wireless recovery in the phone app."
                else:
                    apk = installed_apk(device)
                    if helper_alive(device, apk):
                        if not device.run("shell", "pidof", PACKAGE, check=False) and consent(device):
                            device.run("shell", "am", "start", "-n", MAIN, "--ez", "restore_connection", "true")
                        status = "Input helper is running."
                    else:
                        status = "Input helper restored." if activate(device, apk) else "Recovery cancelled by phone settings."
                if status != last_status:
                    emit(status)
                    last_status = status
                if args.once:
                    return 0 if device and status in ("Input helper is running.", "Input helper restored.") else 1
                time.sleep(args.interval)
            except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                status = "Recovery pending: " + (str(error) if not isinstance(error, (ValueError, subprocess.TimeoutExpired)) else type(error).__name__)
                if status != last_status:
                    emit(status)
                    last_status = status
                if args.once:
                    return 1
                time.sleep(max(args.interval, 10))


def recovery_identity(device):
    """Read the app-generated lease, not a model name, IP or user-entered name."""
    xml = device.run("exec-out", "run-as", PACKAGE, "cat", "shared_prefs/input-control.xml", check=False)
    try:
        root = ET.fromstring(xml)
        strings = {node.get("name"): node.text for node in root.findall("string")}
        flags = {node.get("name"): node.get("value") == "true" for node in root.findall("boolean")}
        return strings.get("recoveryId"), (flags.get("enabled", False) and
            flags.get("wirelessRecovery", False) and flags.get("resumeConnection", True))
    except ET.ParseError:
        return None, False


def find_registered_device(adb, registration):
    # Only ADB-authorized transports and standard advertised TLS endpoints.
    # Never auto-pair, scan a subnet or enable debugging on an unregistered phone.
    candidates = []
    for line in adb.run("devices").splitlines()[1:]:
        fields = line.split()
        if len(fields) >= 2 and fields[1] == "device":
            candidates.append(fields[0])
    for line in adb.run("mdns", "services", check=False).splitlines():
        fields = line.split()
        if len(fields) >= 3 and fields[1].rstrip(".") == "_adb-tls-connect._tcp":
            try:
                address = endpoint(fields[2])
                if address not in candidates:
                    adb.run("connect", address, check=False)
                    candidates.append(address)
            except ValueError:
                pass
    if not candidates:
        for address in discover_mdns(None):
            adb.run("connect", address, check=False)
            candidates.append(address)
    for target in dict.fromkeys(candidates):
        device = Adb(adb.executable, target)
        device.allowed = adb.allowed
        try:
            identity, enabled = recovery_identity(device)
            if identity == registration and enabled:
                serial = validate_serial(device.run("shell", "getprop", "ro.serialno"))
                return device, serial
        except (RuntimeError, ValueError, subprocess.TimeoutExpired):
            continue
    return None, None


def recover_registered(device, registration):
    device.registration = registration
    identity, enabled = recovery_identity(device)
    if identity != registration or not enabled:
        return "Paused: phone disabled recovery, disconnected manually or changed server."
    apk = installed_apk(device)
    if helper_alive(device, apk):
        if not device.run("shell", "pidof", PACKAGE, check=False):
            identity, enabled = recovery_identity(device)
            if identity == registration and enabled:
                device.run("shell", "am", "start", "-n", MAIN, "--ez", "restore_connection", "true")
        return "Input helper is running."
    # Recheck the current lease immediately before any activation side effects.
    identity, enabled = recovery_identity(device)
    if identity != registration or not enabled:
        return "Recovery cancelled by phone settings."
    return "Input helper restored." if activate(device, apk) else "Recovery cancelled by phone settings."


class ServerRecovery:
    """In-memory, per-server registry. EOF revokes all leases; no OS startup task."""
    def __init__(self, adb):
        self.adb = adb
        self.workers = {}
        self.slots = threading.BoundedSemaphore(4)

    def update(self, registrations):
        if (not isinstance(registrations, list) or len(registrations) > 32 or
            any(not isinstance(item, str) or not re.fullmatch(r"[0-9a-f]{32}", item) for item in registrations)):
            raise ValueError("Invalid server recovery registry")
        desired = set(registrations)
        for identity in set(self.workers) - desired:
            self.workers.pop(identity).set()
        for identity in desired - set(self.workers):
            cancelled = threading.Event()
            self.workers[identity] = cancelled
            threading.Thread(target=self.watch_device, args=(identity, cancelled), daemon=True).start()

    def close(self):
        for cancelled in self.workers.values():
            cancelled.set()
        self.workers.clear()

    def watch_device(self, identity, cancelled):
        serial = cached = last_status = None
        adb = Adb(self.adb)
        adb.allowed = lambda: not cancelled.is_set()
        while not cancelled.is_set():
            if not self.slots.acquire(timeout=0.25):
                continue
            try:
                if cancelled.is_set():
                    return
                if serial is None:
                    device, serial = find_registered_device(adb, identity)
                else:
                    device = connect_target(adb, serial, cached=cached, allow_usb=True)
                if device:
                    device.allowed = adb.allowed
                    try:
                        cached = endpoint(device.target)
                    except ValueError:
                        # A USB bootstrap can teach us the phone's WLAN address.
                        # This is only a discovery hint; connect_target still
                        # checks ro.serialno before touching an app or helper.
                        port = device.run("shell", "getprop", "service.adb.tls.port", check=False)
                        addresses = device.run("shell", "ip", "-o", "-4", "addr", "show", check=False)
                        match = re.search(r"\b(?:wlan|wifi)\d+\s+inet\s+(\d+(?:\.\d+){3})/", addresses)
                        if match and port.isdigit() and 0 < int(port) <= 65535:
                            cached = endpoint(match[1] + ":" + port)
                    status = recover_registered(device, identity)
                else:
                    status = "Waiting for registered phone: pair wireless ADB on this server and enable Wireless debugging."
                if status != last_status:
                    emit("[" + identity[:8] + "] " + status)
                    last_status = status
            except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
                status = "Recovery pending: " + type(error).__name__
                if not cancelled.is_set() and status != last_status:
                    emit("[" + identity[:8] + "] " + status)
                    last_status = status
            finally:
                self.slots.release()
            cancelled.wait(5)


def serve(args):
    import sys
    recovery = ServerRecovery(args.adb)
    try:
        # A private pipe owned by the server. No persistent registration file,
        # socket listener, login task, global daemon or user-specific device ID.
        for line in sys.stdin:
            if len(line) > 4096:
                raise ValueError("Recovery registry is too large")
            recovery.update(json.loads(line))
    finally:
        recovery.close()
    return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb")
    parser.add_argument("--log-file", type=Path, help="Append status logs here, suitable for background startup")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("server", help="Internal server-owned mode; registry arrives over stdin")
    pair = sub.add_parser("pair", help="Pair once using the port shown in the pairing-code dialog")
    pair.add_argument("address", type=endpoint)
    watcher = sub.add_parser("watch", help="Run while this computer is on; Ctrl+C stops it")
    watcher.add_argument("--serial", required=True, type=validate_serial)
    watcher.add_argument("--address", type=endpoint, help="Optional connection address, NOT the pairing port; changes after reboot")
    watcher.add_argument("--interval", type=float, default=5)
    watcher.add_argument("--once", action="store_true")
    watcher.add_argument("--state-file", type=Path, help="Store only the last verified phone address for mDNS fallback")
    args = parser.parse_args()
    if args.log_file:
        import sys
        args.log_file.parent.mkdir(parents=True, exist_ok=True)
        sys.stdout = sys.stderr = args.log_file.open("a", encoding="utf-8", buffering=1)
    try:
        if args.command == "server":
            return serve(args)
        if args.command == "pair":
            code = getpass.getpass("Six-digit pairing code from the phone: ")
            if not re.fullmatch(r"\d{6}", code):
                raise ValueError("Pairing code must contain six digits")
            output = Adb(args.adb).run("pair", args.address, input_text=code + "\n", timeout=45)
            if "Successfully paired" not in output:
                raise RuntimeError("Pairing failed; reopen the pairing dialog and retry")
            emit("Paired successfully. The pairing code was not stored.")
            return 0
        if args.interval < 2:
            raise ValueError("Polling interval must be at least 2 seconds")
        if args.state_file is None:
            state_root = Path(os.environ.get("LOCALAPPDATA", str(Path.home() / ".cache"))) / "DeskConnect"
            args.state_file = state_root / ("wireless-" + args.serial + ".state.json")
        return watch(args)
    except KeyboardInterrupt:
        emit("Wireless recovery watcher stopped.")
        return 0
    except (OSError, RuntimeError, ValueError, subprocess.TimeoutExpired) as error:
        emit(str(error) if not isinstance(error, subprocess.TimeoutExpired) else "ADB timed out")
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
