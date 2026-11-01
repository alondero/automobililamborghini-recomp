"""Measure JP race-loader weather resolution in ares, using controlled menu writes.

This is an intrusive debugger experiment, not a menu playthrough or AI oracle.
Each case cold-boots the verified cartridge and sets menu selections before
requesting the stock race loader. No port code or ROM instructions are patched.
See docs/jp-weather.md for addresses, ownership and interpretation.
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import socket
import subprocess
import time

from ares_debug_client import AresDebugClient


JP_SHA256 = "06c9c992014da5982e747117c690c1590a011c0c6756b0522df126366bf8b9a1"
ROOT = Path(__file__).resolve().parents[2]
STATE = 0x800C4EA0
ACTIVE_WEATHER = 0x800C503C


class WeatherClient(AresDebugClient):
    """Fail on truncated or corrupt packets instead of retaining a false sample."""

    def _recv_packet(self):
        def receive():
            byte = self.sock.recv(1)
            if not byte:
                raise EOFError("Debugger closed during a packet")
            return byte

        while receive() != b"$":
            pass
        payload = bytearray()
        while True:
            byte = receive()
            if byte == b"#":
                break
            payload.extend(byte)
            if len(payload) > 4096:
                raise RuntimeError("Debugger packet exceeds advertised limit")
        checksum = receive() + receive()
        if checksum.lower() != f"{sum(payload) & 255:02x}".encode("ascii"):
            raise RuntimeError("Debugger packet checksum mismatch")
        self.sock.sendall(b"+")
        return payload.decode("ascii")


def pause(client):
    client._send_packet("vCtrlC")
    # ares can send both OK and an asynchronous stop packet, in either order.
    for _ in range(2):
        reply = client._recv_packet()
        if reply.startswith(("S", "T")) and client.parse_stop_reason(reply).get("signal") in (2, 5):
            return reply
        if reply != "OK":
            break
    raise RuntimeError("Debugger did not acknowledge a paused guest")


def read(client, address, size=2):
    # The legacy helper turns an unsolicited stop packet into four zero bytes.
    # Read the packet directly so a failed observation cannot become a value.
    client._send_packet(f"m{address:x},{size:x}")
    reply = client._recv_packet()
    if len(reply) != size * 2 or any(c not in "0123456789abcdefABCDEF" for c in reply):
        raise RuntimeError(f"Invalid memory reply at {address:#x}: {reply!r}")
    try:
        return int.from_bytes(bytes.fromhex(reply), "big")
    except ValueError as exc:
        raise RuntimeError(f"Non-hex memory reply at {address:#x}") from exc


def write_halfword(client, address, value):
    client._send_packet(f"M{address:x},2:{value:04x}")
    reply = client._recv_packet()
    if reply != "OK":
        raise RuntimeError(f"Write rejected at {address:#x}: {reply!r}")
    if read(client, address) != value:
        raise RuntimeError(f"Write verification failed at {address:#x}")


def wait_write(client, address, timeout):
    if not client.set_watchpoint(address, size=2):
        raise RuntimeError(f"Watchpoint rejected at {address:#x}")
    # On timeout, the caller terminates its process rather than sending another
    # request into an outstanding continue response.
    stop = client.continue_until_halt(timeout=timeout)
    event = client.parse_stop_reason(stop)
    if event.get("kind") != "watch" or event.get("addr") != address:
        raise RuntimeError(f"Unexpected stop: {stop!r}")
    return stop


def measure(exe, rom, circuit, weather, players, port, boot_wait):
    args = [str(exe), "--kiosk", "--no-file-prompt",
            "--setting", "DebugServer/Enabled=true",
            "--setting", f"DebugServer/Port={port}",
            "--setting", "DebugServer/UseIPv4=true",
            "--setting", "Boot/AwaitGDBClient=false", str(rom)]
    process = subprocess.Popen(
        args, cwd=exe.parent, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
        creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    client = None
    try:
        # Boot without a debugger: this build's await-client path did not
        # advance to a usable menu in the initial investigation.
        time.sleep(boot_wait)
        if process.poll() is not None:
            raise RuntimeError(f"ares exited during boot: {process.returncode}")
        client = WeatherClient(port=port, timeout=5)
        client.connect()
        client._drain(0.3)
        if not client.enable_no_ack_mode():
            raise RuntimeError("ares rejected no-ack mode")
        client._drain(0.2)
        pause_stop = pause(client)
        client._drain(0.5)
        initial_state = read(client, STATE)
        if initial_state not in (3, 4, 5, 6):
            raise RuntimeError(f"Not in a menu phase: state={initial_state}")
        selections = {
            0x800C4E98: players, 0x800C4E9A: 1, 0x800C4EA8: 2,
            0x800C4EAE: 0, 0x800C4EB0: circuit, 0x800C4ED0: 3,
            0x800C4F30: weather,
        }
        for address, value in selections.items():
            write_halfword(client, address, value)
        write_halfword(client, STATE, 7)
        weather_stop = wait_write(client, ACTIVE_WEATHER, 30)
        resolved = read(client, ACTIVE_WEATHER)
        if not client.clear_watchpoint(ACTIVE_WEATHER, size=2):
            raise RuntimeError("Could not remove weather watchpoint")
        state_stop = wait_write(client, STATE, 30)
        state = read(client, STATE)
        actual_circuit = read(client, 0x800C4FFA)
        if state != 8 or actual_circuit != circuit:
            raise RuntimeError(f"Unexpected loaded race: state={state}, circuit={actual_circuit}")
        return {
            "circuit_index": circuit, "players": players,
            "selected_weather": weather, "resolved_weather": resolved,
            "initial_state": initial_state, "loaded_state": state,
            "pause_stop": pause_stop,
            "active_circuit_index": actual_circuit,
            "weather_stop": weather_stop, "state_stop": state_stop,
            "fog_near": read(client, 0x80099C18),
            "fog_far": read(client, 0x80099C1A),
            "fog_rgb": [read(client, a, 1) for a in
                        (0x80099C1C, 0x800A9C20, 0x800A9C24)],
        }
    finally:
        if client:
            client.disconnect()
        if process.poll() is None:
            process.terminate()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait(timeout=5)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ares", type=Path, required=True)
    parser.add_argument("--rom", type=Path, default=ROOT / "Super Speed Race 64 (Japan).z64")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--circuits", type=int, nargs="+", choices=range(6), default=list(range(6)))
    parser.add_argument("--weather", type=int, nargs="+", choices=range(3), default=[0, 1, 2])
    parser.add_argument("--players", type=int, choices=range(1, 5), default=1)
    parser.add_argument("--port", type=int, default=9340)
    parser.add_argument("--boot-wait", type=float, default=20)
    args = parser.parse_args()
    if args.players >= 3 and max(args.circuits) > 2:
        parser.error("Three/four-player races only offer circuit indices 0..2")
    if not 1 <= args.port <= 65535 or args.boot_wait <= 0:
        parser.error("Port must be 1..65535 and boot wait must be positive")
    exe, rom = args.ares.resolve(), args.rom.resolve()
    digest = hashlib.sha256(rom.read_bytes()).hexdigest()
    if digest != JP_SHA256:
        parser.error("JP cartridge SHA-256 mismatch")
    # Refuse to attach to another session accidentally.
    with socket.socket() as sock:
        sock.settimeout(0.5)
        if sock.connect_ex(("127.0.0.1", args.port)) == 0:
            parser.error("Debugger port is already in use")
    result = {
        "schema": 1, "evidence_type": "controlled-live-jp-race-loader",
        "captured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
        "probe_sha256": hashlib.sha256(Path(__file__).read_bytes().replace(b"\r\n", b"\n")).hexdigest(),
        "rom_sha256": digest, "emulator_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
        "boot_wait_seconds": args.boot_wait,
        "method": "Cold boot; debugger writes single-race menu selections and state 7; observe weather and state writes. Audio quiesce callbacks are not invoked.",
        "limitations": ["Not an ordinary menu playthrough", "No AI trajectory or port comparison", "No elapsed-frame or perspective-distance measurement"],
        "requested_cases": [{"circuit_index": circuit, "selected_weather": weather,
                             "players": args.players}
                            for circuit in args.circuits for weather in args.weather],
        "complete": False, "runs": [], "failures": [],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    for circuit in args.circuits:
        for weather in args.weather:
            try:
                case = measure(exe, rom, circuit, weather, args.players, args.port, args.boot_wait)
                result["runs"].append(case)
                print(json.dumps(case), flush=True)
            except Exception as exc:
                failure = {"circuit_index": circuit, "selected_weather": weather,
                           "players": args.players, "error": str(exc)}
                result["failures"].append(failure)
                print(json.dumps(failure), flush=True)
            args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    result["complete"] = True
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    return 1 if result["failures"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
