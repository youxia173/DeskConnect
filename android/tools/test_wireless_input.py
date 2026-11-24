import importlib.util
from pathlib import Path
import types
import struct
import unittest
import subprocess
import sys
from unittest.mock import Mock, patch

spec = importlib.util.spec_from_file_location("wireless_input", Path(__file__).with_name("wireless-input.py"))
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class WirelessRecoveryTests(unittest.TestCase):
    def test_server_registry_starts_only_registered_devices_and_cancels_individually(self):
        registry = module.ServerRecovery("adb")
        first, second = "a" * 32, "b" * 32
        with patch.object(module.threading, "Thread") as thread:
            registry.update([])
            thread.assert_not_called()
            registry.update([first, second])
            self.assertEqual(thread.call_count, 2)
            first_stop, second_stop = registry.workers[first], registry.workers[second]
            registry.update([first, second])
            self.assertEqual(thread.call_count, 2)
            registry.update([second])
            self.assertTrue(first_stop.is_set())
            self.assertFalse(second_stop.is_set())
            registry.close()
            self.assertTrue(second_stop.is_set())
            self.assertEqual(registry.workers, {})

    def test_invalid_or_oversized_registry_cannot_start_workers(self):
        registry = module.ServerRecovery("adb")
        with patch.object(module.threading, "Thread") as thread:
            for bad in [None, {}, ["phone model"], ["a" * 32] * 33, [123]]:
                with self.assertRaises(ValueError):
                    registry.update(bad)
            thread.assert_not_called()

    def test_server_worker_exits_on_pipe_eof_without_any_phone_or_adb(self):
        result = subprocess.run([sys.executable, str(Path(module.__file__)), "--adb", "missing-adb", "server"],
                                input="[]\n", text=True, capture_output=True, timeout=5)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "")

    def test_matching_uses_private_registration_not_device_order_or_name(self):
        adb = Mock(executable="adb", allowed=lambda: True)
        adb.run.side_effect = lambda *args, **kw: "List of devices attached\nphone1\tdevice\nphone2\tdevice" if args[0] == "devices" else ""
        first, second = Mock(), Mock()
        second.run.return_value = "physical2"
        with patch.object(module, "Adb", side_effect=[first, second]), patch.object(module, "recovery_identity", side_effect=[("a" * 32, True), ("b" * 32, True)]):
            device, serial = module.find_registered_device(adb, "b" * 32)
        self.assertIs(device, second)
        self.assertEqual(serial, "physical2")
        first.run.assert_not_called()

    def test_old_server_or_disabled_phone_cannot_activate(self):
        device = Mock()
        for state in [("b" * 32, True), ("a" * 32, False), (None, False)]:
            with patch.object(module, "recovery_identity", return_value=state), patch.object(module, "activate") as activate, patch.object(module, "installed_apk") as apk:
                self.assertIn("Paused", module.recover_registered(device, "a" * 32))
                activate.assert_not_called()
                apk.assert_not_called()

    def test_manual_disconnect_and_server_change_revoke_activation_consent(self):
        device = Mock(registration="a" * 32)
        for identity, resume, expected in [("a" * 32, "true", True), ("a" * 32, "false", False), ("b" * 32, "true", False)]:
            device.run.return_value = f'<map><string name="recoveryId">{identity}</string><boolean name="enabled" value="true"/><boolean name="wirelessRecovery" value="true"/><boolean name="resumeConnection" value="{resume}"/></map>'
            self.assertEqual(module.consent(device), expected)

    def test_cancellation_prevents_further_adb_commands(self):
        device = module.Adb("adb")
        device.allowed = lambda: False
        with patch.object(module.subprocess, "run") as run:
            with self.assertRaises(RuntimeError):
                device.run("shell", "am", "start")
            run.assert_not_called()

    def test_discovery_accepts_only_exact_serial_and_connect_service(self):
        text = """adb-560cf235-new _adb-tls-connect._tcp. 192.168.1.8:37123
adb-560cf235-new _adb-tls-pairing._tcp. 192.168.1.8:44444
adb-560cf2350-new _adb-tls-connect._tcp. 192.168.1.9:37123
adb-other-new _adb-tls-connect._tcp. 192.168.1.10:37123
adb-560cf235-bad _adb-tls-connect._tcp. 999.0.0.1:12345"""
        self.assertEqual(module.discover_targets(text, "560cf235"), ["192.168.1.8:37123"])

    def test_pairing_endpoint_rejects_shell_text_and_wrong_port(self):
        for value in ["127.0.0.1:0", "127.0.0.1:70000", "host;command:5555", "999.1.1.1:5555"]:
            with self.assertRaises(ValueError):
                module.endpoint(value)

    def test_consent_requires_both_persistent_switches(self):
        device = Mock()
        for enabled, recovery, expected in [(True, True, True), (False, True, False), (True, False, False)]:
            device.run.return_value = f'<map><boolean name="enabled" value="{str(enabled).lower()}"/><boolean name="wirelessRecovery" value="{str(recovery).lower()}"/></map>'
            self.assertEqual(module.consent(device), expected)
        device.run.return_value = "invalid xml"
        self.assertFalse(module.consent(device))

    def test_discovery_spoof_cannot_activate_another_phone(self):
        adb = Mock(executable="adb")
        adb.run.side_effect = lambda *args, **kw: ("adb-560cf235-new _adb-tls-connect._tcp 192.168.1.8:37123" if args[0] == "mdns" else "")
        wrong_phone = Mock()
        wrong_phone.run.return_value = "different-phone"
        with patch.object(module, "Adb", return_value=wrong_phone), patch.object(module, "discover_mdns", return_value=[]):
            self.assertIsNone(module.connect_target(adb, "560cf235"))

    def test_native_mdns_finds_new_port_and_rejects_other_serial(self):
        def name(value):
            return b''.join(bytes([len(label)]) + label.encode() for label in value.split('.')) + b'\0'
        service = name('adb-unit123-random._adb-tls-connect._tcp.local')
        target = name('phone.local')
        rdata = struct.pack('!HHH', 0, 0, 39999) + target
        packet = struct.pack('!6H', 0, 0x8400, 0, 2, 0, 0)
        packet += service + struct.pack('!HHIH', 33, 1, 120, len(rdata)) + rdata
        packet += target + struct.pack('!HHIH', 1, 1, 120, 4) + bytes([192, 0, 2, 9])
        self.assertEqual(module.mdns_targets(packet, '192.0.2.1', 'unit123'), ['192.0.2.9:39999'])
        self.assertEqual(module.mdns_targets(packet, '192.0.2.1', 'other'), [])
        self.assertEqual(module.mdns_targets(packet[:-1], '192.0.2.1', 'unit123'), [])

    def test_dns_compression_cannot_loop_forever(self):
        with self.assertRaises(ValueError):
            module.dns_name(b'\xc0\x00', 0)
        with self.assertRaises(ValueError):
            module.dns_name(b'\xc0', 0)

    def test_watcher_never_restarts_live_helper_or_overrides_disabled_switch(self):
        args = types.SimpleNamespace(adb="adb", serial="unit_test_phone", address=None, once=True, interval=5)
        device = Mock()
        device.run.return_value = "1234"
        with patch.object(module, "connect_target", return_value=device), patch.object(module, "consent", return_value=False), patch.object(module, "activate") as activate:
            self.assertEqual(module.watch(args), 1)
            activate.assert_not_called()
        with patch.object(module, "connect_target", return_value=device), patch.object(module, "consent", return_value=True), patch.object(module, "installed_apk", return_value="apk"), patch.object(module, "helper_alive", return_value=True), patch.object(module, "activate") as activate:
            self.assertEqual(module.watch(args), 0)
            activate.assert_not_called()

    def test_missing_helper_is_reactivated(self):
        args = types.SimpleNamespace(adb="adb", serial="unit_test_phone", address=None, once=True, interval=5)
        device = Mock()
        with patch.object(module, "connect_target", return_value=device), patch.object(module, "consent", return_value=True), patch.object(module, "installed_apk", return_value="apk"), patch.object(module, "helper_alive", return_value=False), patch.object(module, "activate", return_value=True) as activate:
            self.assertEqual(module.watch(args), 0)
            activate.assert_called_once_with(device, "apk")


if __name__ == "__main__":
    unittest.main()
