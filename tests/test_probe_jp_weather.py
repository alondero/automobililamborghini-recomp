import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools/emu_instrumentation"
SPEC = importlib.util.spec_from_file_location("probe_jp_weather", TOOLS / "probe_jp_weather.py")
probe = importlib.util.module_from_spec(SPEC)
with patch.object(sys, "path", [str(TOOLS), *sys.path]):
    SPEC.loader.exec_module(probe)


class WeatherProbeTests(unittest.TestCase):
    def wire_client(self, wire):
        client = probe.WeatherClient()
        stream = io.BytesIO(wire)
        client.sock = Mock()
        client.sock.recv.side_effect = stream.read
        return client

    def test_packet_eof_before_or_inside_payload_or_checksum_is_fatal(self):
        for wire in [b"", b"+", b"$", b"$0002", b"$0002#", b"$0002#c"]:
            with self.subTest(wire=wire):
                client = self.wire_client(wire)
                with self.assertRaises(EOFError):
                    client._recv_packet()
                client.sock.sendall.assert_not_called()

    def test_packet_checksum_is_verified_before_acknowledgement(self):
        for wire in [b"$0002#00", b"$0002#zz", b"$0002# c"]:
            with self.subTest(wire=wire):
                client = self.wire_client(wire)
                with self.assertRaisesRegex(RuntimeError, "checksum"):
                    client._recv_packet()
                client.sock.sendall.assert_not_called()
        client = self.wire_client(b"+$0002#C2")
        self.assertEqual(client._recv_packet(), "0002")
        client.sock.sendall.assert_called_once_with(b"+")

    def test_oversized_packet_is_rejected(self):
        client = self.wire_client(b"$" + b"0" * 4097 + b"#30")
        with self.assertRaisesRegex(RuntimeError, "limit"):
            client._recv_packet()
        client.sock.sendall.assert_not_called()

    def test_pause_requires_a_stop_acknowledgement(self):
        for replies in [[""], ["E01"], ["0000"], ["OK", "OK"], ["S0b"],
                        ["OK", TimeoutError("missing stop")], [EOFError("closed")]]:
            with self.subTest(replies=replies):
                client = probe.WeatherClient()
                client._send_packet = Mock()
                client._recv_packet = Mock(side_effect=replies)
                with self.assertRaises((RuntimeError, TimeoutError, EOFError)):
                    probe.pause(client)
                client._send_packet.assert_called_once_with("vCtrlC")

    def test_pause_accepts_stop_alone_or_after_ok(self):
        for replies in [["S02"], ["T05"], ["OK", "S05"], ["OK", "T02"]]:
            with self.subTest(replies=replies):
                client = probe.WeatherClient()
                client._send_packet = Mock()
                client._recv_packet = Mock(side_effect=replies)
                self.assertEqual(probe.pause(client), replies[-1])
                client._send_packet.assert_called_once_with("vCtrlC")

    def test_memory_reads_preserve_zero_and_big_endian_values(self):
        client = Mock()
        for reply, size, expected in [("0000", 2, 0), ("1234", 2, 0x1234), ("fe", 1, 254)]:
            with self.subTest(reply=reply):
                client._recv_packet.return_value = reply
                self.assertEqual(probe.read(client, probe.ACTIVE_WEATHER, size), expected)
                client._send_packet.assert_called_with(f"m{probe.ACTIVE_WEATHER:x},{size:x}")

    def test_invalid_memory_observations_cannot_become_zero(self):
        client = Mock()
        for reply in ["", "S05", "T05;watch:800c503c;", "E01", "00", "000000", "zzzz", "00  "]:
            with self.subTest(reply=reply):
                client._recv_packet.return_value = reply
                with self.assertRaises(RuntimeError):
                    probe.read(client, probe.ACTIVE_WEATHER)

    def test_write_requires_acknowledgement_and_matching_readback(self):
        client = Mock()
        client._recv_packet.side_effect = ["OK", "0002"]
        probe.write_halfword(client, probe.ACTIVE_WEATHER, 2)
        self.assertEqual(client._send_packet.call_args_list[0].args, ("M800c503c,2:0002",))
        for replies in [["E01"], ["S05"], ["OK", "0001"], ["OK", "T05;"]]:
            with self.subTest(replies=replies):
                client._recv_packet.side_effect = replies
                with self.assertRaises(RuntimeError):
                    probe.write_halfword(client, probe.ACTIVE_WEATHER, 2)

    def test_rejected_watchpoint_does_not_resume_target(self):
        client = Mock()
        client.set_watchpoint.return_value = False
        with self.assertRaisesRegex(RuntimeError, "Watchpoint rejected"):
            probe.wait_write(client, probe.ACTIVE_WEATHER, 10)
        client.continue_until_halt.assert_not_called()

    def test_watchpoint_requires_expected_stop_and_address(self):
        client = Mock()
        client.set_watchpoint.return_value = True
        client.continue_until_halt.return_value = "T05;watch:800c503c;"
        for event in [{}, {"kind": "signal", "addr": probe.ACTIVE_WEATHER},
                      {"kind": "watch", "addr": probe.STATE}, {"kind": "watch"}]:
            with self.subTest(event=event):
                client.parse_stop_reason.return_value = event
                with self.assertRaisesRegex(RuntimeError, "Unexpected stop"):
                    probe.wait_write(client, probe.ACTIVE_WEATHER, 10)
        client.parse_stop_reason.return_value = {"kind": "watch", "addr": probe.ACTIVE_WEATHER}
        self.assertEqual(probe.wait_write(client, probe.ACTIVE_WEATHER, 10), "T05;watch:800c503c;")

    def test_timeout_does_not_send_more_debugger_requests(self):
        client = Mock()
        client.set_watchpoint.return_value = True
        client.continue_until_halt.side_effect = TimeoutError("no stop")
        with self.assertRaises(TimeoutError):
            probe.wait_write(client, probe.ACTIVE_WEATHER, 10)
        client.parse_stop_reason.assert_not_called()
        client.clear_watchpoint.assert_not_called()
        client._send_packet.assert_not_called()

    def test_invalid_cli_selection_fails_before_reading_cartridge(self):
        for options in [["--circuits", "6"], ["--weather", "3"], ["--players", "0"],
                        ["--players", "3", "--circuits", "3"], ["--port", "0"],
                        ["--port", "65536"], ["--boot-wait", "0"]]:
            with self.subTest(options=options), patch.object(sys, "argv", [
                    "probe_jp_weather", "--ares", "unused", "--output", "unused", *options]), \
                    patch.object(Path, "read_bytes") as read_bytes, \
                    contextlib.redirect_stderr(io.StringIO()):
                with self.assertRaises(SystemExit) as raised:
                    probe.main()
                self.assertEqual(raised.exception.code, 2)
                read_bytes.assert_not_called()

    def test_failed_case_is_retained_and_returns_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rom, exe, output = root / "test.z64", root / "ares.exe", root / "result.json"
            rom.write_bytes(b"synthetic cartridge identity")
            exe.write_bytes(b"synthetic emulator identity")
            argv = ["probe_jp_weather", "--ares", str(exe), "--rom", str(rom),
                    "--output", str(output), "--circuits", "0", "--weather", "1"]
            with patch.object(sys, "argv", argv), \
                    patch.object(probe, "JP_SHA256", hashlib.sha256(rom.read_bytes()).hexdigest()), \
                    patch.object(probe.socket, "socket") as socket_factory, \
                    patch.object(probe, "measure", side_effect=RuntimeError("Invalid memory reply")), \
                    patch.object(probe.subprocess, "Popen") as launch, \
                    contextlib.redirect_stdout(io.StringIO()):
                socket_factory.return_value.__enter__.return_value.connect_ex.return_value = 1
                self.assertEqual(probe.main(), 1)
                launch.assert_not_called()
            result = json.loads(output.read_text())
            self.assertTrue(result["complete"])
            self.assertEqual(result["requested_cases"], [
                {"circuit_index": 0, "selected_weather": 1, "players": 1}])
            self.assertEqual(result["runs"], [])
            self.assertEqual(result["failures"], [{"circuit_index": 0, "selected_weather": 1,
                                                  "players": 1, "error": "Invalid memory reply"}])

    def test_interrupted_campaign_keeps_requested_cases_and_incomplete_status(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rom, exe, output = root / "test.z64", root / "ares.exe", root / "result.json"
            rom.write_bytes(b"synthetic cartridge identity")
            exe.write_bytes(b"synthetic emulator identity")
            first_case = {"circuit_index": 0, "selected_weather": 0, "players": 1}
            argv = ["probe_jp_weather", "--ares", str(exe), "--rom", str(rom),
                    "--output", str(output), "--circuits", "0", "1", "--weather", "0", "1"]
            with patch.object(sys, "argv", argv), \
                    patch.object(probe, "JP_SHA256", hashlib.sha256(rom.read_bytes()).hexdigest()), \
                    patch.object(probe.socket, "socket") as socket_factory, \
                    patch.object(probe, "measure", side_effect=[first_case, KeyboardInterrupt()]), \
                    patch.object(probe.subprocess, "Popen") as launch, \
                    contextlib.redirect_stdout(io.StringIO()):
                socket_factory.return_value.__enter__.return_value.connect_ex.return_value = 1
                with self.assertRaises(KeyboardInterrupt):
                    probe.main()
                launch.assert_not_called()
            result = json.loads(output.read_text())
            self.assertFalse(result["complete"])
            self.assertEqual(result["requested_cases"], [
                {"circuit_index": circuit, "selected_weather": weather, "players": 1}
                for circuit in (0, 1) for weather in (0, 1)])
            self.assertEqual(result["runs"], [first_case])
            self.assertEqual(result["failures"], [])


if __name__ == "__main__":
    unittest.main()
