#!/usr/bin/env python3
"""Exercise the debug-only InputTestActivity through a local Barrier server.

Requires an installed debug APK, an activated ADB input helper, overlay permission,
an unlocked phone, and no normal DeskConnect session. Only the dedicated test page
is edited. No clipboard, user files, or other applications are inspected.
"""

import argparse
import json
import re
import socket
import struct
import subprocess
import sys
import threading
import time


PORT = 24999
COMPONENT = "com.deskconnect.app/.input.InputTestActivity"
FRAME_LIMIT = 1024 * 1024


class Adb:
    def __init__(self, executable, serial):
        self.command = [executable] + (["-s", serial] if serial else [])

    def run(self, *arguments, check=True):
        result = subprocess.run(self.command + list(arguments), capture_output=True,
                                text=True, encoding="utf-8", errors="replace", timeout=25)
        if check and result.returncode:
            raise RuntimeError(f"adb {' '.join(arguments)} failed: {result.stdout} {result.stderr}")
        return result.stdout


class TestLogs:
    def __init__(self, adb):
        self.lines = []
        self.started = False
        self.condition = threading.Condition()
        # Do not clear the device's log buffers. Discard existing entries until
        # this invocation's new Activity emits its explicit creation marker.
        self.process = subprocess.Popen(
            adb.command + ["logcat", "-v", "raw", "-T", "1",
                           "DeskConnectInputTest:I", "DeskConnectInput:I", "*:S"],
            stdout=subprocess.PIPE, stderr=subprocess.DEVNULL,
            text=True, encoding="utf-8", errors="replace")
        self.thread = threading.Thread(target=self._read, daemon=True)
        self.thread.start()

    def _read(self):
        for raw in self.process.stdout:
            line = raw.rstrip("\r\n")
            with self.condition:
                if line == "created scope=this_test_page_only":
                    self.started = True
                    self.lines.clear()
                if self.started and line:
                    self.lines.append(line)
                    self.condition.notify_all()

    def snapshot(self):
        with self.condition:
            return list(self.lines)

    def wait(self, predicate, description, timeout=8, allow_disconnect=False, expected_failure=None):
        deadline = time.monotonic() + timeout
        with self.condition:
            while True:
                failures = [line for line in self.lines if
                            line.startswith(("input failure ", "connection refused ")) or
                            (line.startswith("disconnected ") and not allow_disconnect)]
                if expected_failure:
                    failures = [line for line in failures if expected_failure not in line]
                if failures:
                    raise AssertionError("Test connection failed: " + failures[-1])
                result = predicate(self.lines)
                if result:
                    return result
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    raise AssertionError("Timed out waiting for " + description)
                self.condition.wait(min(remaining, 0.25))

    def layout(self):
        def collect(lines):
            found = {}
            for line in lines:
                if line.startswith("layout name="):
                    values = dict(re.findall(r"(\w+)=([^ ]+)", line))
                    name = values.pop("name")
                    found[name] = {key: int(value) for key, value in values.items()}
            return found if {"editor", "button", "scroll"}.issubset(found) else None
        return self.wait(collect, "test page layout")

    def text(self):
        for line in reversed(self.snapshot()):
            if line.startswith("text value="):
                return json.loads(line[len("text value="):])
        return ""

    def expect_text(self, expected):
        self.wait(lambda lines: self.text() == expected, f"editor text {expected!r}")

    def close(self):
        self.process.terminate()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.kill()
            self.process.wait(timeout=3)


class BarrierServer:
    def __init__(self):
        self.listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        self.listener.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.listener.bind(("127.0.0.1", PORT))
        self.listener.listen(1)
        self.listener.settimeout(20)
        self.connection = None
        self.send_lock = threading.Lock()
        self.stop = threading.Event()
        self.disconnected = threading.Event()
        self.error = None
        self.heartbeats = 0

    def send(self, payload):
        with self.send_lock:
            self.connection.sendall(struct.pack("!I", len(payload)) + payload)

    def _exact(self, length):
        data = bytearray()
        while len(data) < length:
            part = self.connection.recv(length - len(data))
            if not part:
                raise EOFError("test client closed the socket")
            data.extend(part)
        return bytes(data)

    def receive(self):
        length, = struct.unpack("!I", self._exact(4))
        if not 0 < length <= FRAME_LIMIT:
            raise AssertionError(f"Invalid Barrier frame length {length}")
        return self._exact(length)

    def handshake(self):
        self.connection, _ = self.listener.accept()
        self.connection.settimeout(10)
        self.connection.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
        self.send(b"Barrier" + struct.pack("!HH", 1, 8))
        hello = self.receive()
        if not hello.startswith(b"Barrier" + struct.pack("!HH", 1, 8)) or len(hello) < 15:
            raise AssertionError(f"Invalid client hello: {hello!r}")
        name_length, = struct.unpack("!I", hello[11:15])
        name = hello[15:15 + name_length].decode("utf-8")
        if name != "DeskConnectInputTest" or len(hello) != 15 + name_length:
            raise AssertionError(f"Unexpected client screen: {name!r}")
        self.send(b"QINF")
        while True:
            info = self.receive()
            if info.startswith(b"DINF"):
                if len(info) != 18:
                    raise AssertionError("Invalid DINF length")
                _, _, width, height, _, _, _ = struct.unpack("!hhhhhhh", info[4:])
                if width <= 0 or height <= 0:
                    raise AssertionError("Invalid reported display dimensions")
                print(f"Connected test screen: {width} x {height}", flush=True)
                break
        self.send(b"CIAK")
        self.send(b"CROP")
        self.send(b"DSOP" + struct.pack("!I", 2) + b"HART" + struct.pack("!I", 3000))
        self.connection.settimeout(None)
        threading.Thread(target=self._receive_loop, daemon=True).start()
        threading.Thread(target=self._heartbeat_loop, daemon=True).start()

    def _receive_loop(self):
        try:
            while not self.stop.is_set():
                packet = self.receive()
                if packet == b"CALV":
                    self.heartbeats += 1
        except (OSError, EOFError, AssertionError) as error:
            if not self.stop.is_set():
                self.error = error
        finally:
            self.disconnected.set()

    def _heartbeat_loop(self):
        while not self.stop.wait(3):
            try:
                self.send(b"CALV")
            except OSError as error:
                if not self.stop.is_set():
                    self.error = error
                return

    def check(self):
        if self.error or self.disconnected.is_set():
            raise AssertionError(f"Barrier connection interrupted: {self.error}")

    def enter(self, x, y, sequence=1):
        self.send(b"CINN" + struct.pack("!hhIH", x, y, sequence, 0))

    def move(self, x, y):
        self.send(b"DMMV" + struct.pack("!hh", x, y))

    def click(self, x, y):
        self.move(x, y)
        self.send(b"DMDN\x01")
        time.sleep(0.08)
        self.send(b"DMUP\x01")

    def key(self, key, modifiers=0, button=None):
        button = key if button is None else button
        body = struct.pack("!HHH", key, modifiers, button)
        self.send(b"DKDN" + body)
        time.sleep(0.025)
        self.send(b"DKUP" + body)
        time.sleep(0.025)

    def close(self):
        self.stop.set()
        if self.connection is not None:
            try:
                self.connection.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.connection.close()
        self.listener.close()


def run_tests(server, logs):
    server.handshake()
    logs.wait(lambda lines: any(line.startswith("connected protocol=") for line in lines), "connection log")
    # The app bridge may still be reconnecting to the already activated helper.
    time.sleep(1.5)
    # Connection status can wrap onto another line, moving the controls. Read
    # their latest on-screen coordinates after that layout has settled.
    layout = logs.layout()
    center = lambda name: (layout[name]["centerX"], layout[name]["centerY"])

    server.enter(*center("button"))
    server.click(*center("button"))
    logs.wait(lambda lines: "click count=1" in lines, "first button click")
    print("PASS click", flush=True)

    server.click(*center("editor"))
    logs.wait(lambda lines: "editor focus=true" in lines, "editor focus")
    baseline = "deskconnect123"
    for character in baseline:
        server.key(ord(character))
    logs.expect_text(baseline)
    print("PASS text entry", flush=True)

    server.send(b"DKDN" + struct.pack("!HHH", 0xEFE1, 1, 50))
    server.key(ord("A"), modifiers=1, button=30)
    server.send(b"DKUP" + struct.pack("!HHH", 0xEFE1, 0, 50))
    logs.expect_text(baseline + "A")
    server.key(0xEF08, button=14)
    logs.expect_text(baseline)
    print("PASS Shift uppercase and Backspace", flush=True)

    server.move(*center("scroll"))
    server.send(b"DMWM" + struct.pack("!hh", 0, -480))
    logs.wait(lambda lines: any(re.search(r"^scroll x=\d+ y=[1-9]\d*", line) for line in lines), "scroll movement")
    print("PASS scrolling", flush=True)

    # Exercise the touchscreen path as well as the mouse wheel: hold the left
    # button inside this page's ScrollView and move upward in eight steps.
    time.sleep(0.3)
    scroll = layout["scroll"]
    scroll_height = scroll["bottom"] - scroll["top"]
    if scroll_height < 40:
        raise AssertionError("Scroll test area is too small for a drag")
    drag_x = scroll["centerX"]
    drag_start = scroll["top"] + scroll_height * 3 // 4
    drag_end = scroll["top"] + scroll_height // 4
    previous_lines = logs.snapshot()
    previous_scroll = [int(match.group(1)) for line in previous_lines
                       if (match := re.search(r"^scroll x=-?\d+ y=(-?\d+)", line))]
    before_drag_y = previous_scroll[-1]
    drag_log_start = len(previous_lines)
    server.move(drag_x, drag_start)
    server.send(b"DMDN\x01")
    try:
        for step in range(1, 9):
            time.sleep(0.03)
            server.move(drag_x, drag_start + (drag_end - drag_start) * step // 8)
        time.sleep(0.03)
    finally:
        server.send(b"DMUP\x01")

    def dragged_scroll_y(lines):
        for line in lines[drag_log_start:]:
            match = re.search(r"^scroll x=-?\d+ y=(-?\d+)", line)
            if match and int(match.group(1)) > before_drag_y:
                return int(match.group(1))
        return None

    after_drag_y = logs.wait(dragged_scroll_y, "new scroll position after left-button drag")
    print(f"PASS left-button drag/swipe (scrollY {before_drag_y} -> {after_drag_y})", flush=True)

    x, y = center("button")
    start = time.monotonic()
    moves = 0
    while time.monotonic() - start < 6:
        server.check()
        server.move(x + moves % 21 - 10, y)
        moves += 1
        time.sleep(0.008)
    server.check()
    logs.wait(lambda lines: server.heartbeats >= 2, "two heartbeat replies", timeout=4)
    print(f"PASS sustained motion ({moves} events over 6 seconds; {server.heartbeats} heartbeats)", flush=True)

    server.send(b"DKDN" + struct.pack("!HHH", 0xEFE1, 1, 50))
    time.sleep(0.15)
    server.send(b"COUT")
    server.key(ord("z"))
    time.sleep(0.4)
    if logs.text() != baseline:
        raise AssertionError("Input after COUT changed test text")
    server.enter(*center("editor"), sequence=2)
    server.click(*center("editor"))
    server.key(ord("a"))
    logs.expect_text(baseline + "a")
    server.click(*center("button"))
    logs.wait(lambda lines: "click count=2" in lines, "button after re-enter")
    print("PASS COUT ignores input and releases Shift; re-enter works", flush=True)

    server.send(b"DKDN" + struct.pack("!HHH", 0xEFE3, 2, 29))
    server.send(b"DKDN" + struct.pack("!HHH", 0xEFE9, 6, 56))
    server.send(b"DKDN" + struct.pack("!HHH", 0xEF1B, 6, 1))
    logs.wait(lambda lines: any(line.startswith("input failure ") and "Ctrl+Alt+Esc" in line for line in lines),
              "emergency return", allow_disconnect=True, expected_failure="Ctrl+Alt+Esc")
    logs.wait(lambda lines: any(line.startswith("disconnected ") for line in lines),
              "disconnect log", allow_disconnect=True, expected_failure="Ctrl+Alt+Esc")
    server.close()
    print("PASS Ctrl+Alt+Esc emergency return and disconnect", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--adb", default="adb", help="ADB executable path")
    parser.add_argument("--serial", help="ADB device serial")
    arguments = parser.parse_args()
    adb = Adb(arguments.adb, arguments.serial)
    server = None
    logs = None
    reversed_port = False
    try:
        server = BarrierServer()
        existing = adb.run("reverse", "--list")
        if any(f"tcp:{PORT}" in line.split() for line in existing.splitlines()):
            raise RuntimeError(f"ADB reverse tcp:{PORT} already exists; remove it before this test")
        adb.run("reverse", f"tcp:{PORT}", f"tcp:{PORT}")
        reversed_port = True
        logs = TestLogs(adb)
        time.sleep(0.25)
        launch = adb.run("shell", "am", "start", "-W", "-f", "0x14000000", "-n", COMPONENT,
                         "--es", "test_host", "127.0.0.1", "--ei", "test_port", str(PORT))
        if "Error:" in launch or "Exception" in launch:
            raise RuntimeError("Could not launch debug input test: " + launch)
        run_tests(server, logs)
        print("All device input checks passed. Only the dedicated test page was changed.", flush=True)
        return 0
    except (AssertionError, OSError, RuntimeError, subprocess.SubprocessError) as error:
        print(f"FAIL: {error}", file=sys.stderr, flush=True)
        if logs:
            print("--- Test-page logs ---", file=sys.stderr)
            for line in logs.snapshot()[-40:]:
                print(line, file=sys.stderr)
        return 1
    finally:
        if server:
            server.close()
        if logs:
            logs.close()
        if reversed_port:
            try:
                adb.run("reverse", "--remove", f"tcp:{PORT}", check=False)
            except (OSError, subprocess.SubprocessError) as error:
                print(f"Could not remove test ADB reverse: {error}", file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
