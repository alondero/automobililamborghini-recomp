"""Observe JP weather-dependent AI table reads and countdown writes in ares.

Intrusive reference experiment: debugger-written single-race selections, not
a normal menu playthrough or a port-parity oracle. No AI fields are overwritten.
"""

import argparse
import datetime
import hashlib
import json
from pathlib import Path
import socket
import struct
import subprocess
import time

from probe_jp_weather import (ACTIVE_WEATHER, JP_SHA256, ROOT, STATE,
                              WeatherClient, pause, read, wait_write,
                              write_halfword)


def watch(client, address, kind, enable=True):
    method = client.set_watchpoint if enable else client.clear_watchpoint
    if not method(address, kind=kind, size=4 if kind == "read" else 2):
        raise RuntimeError(f"Watchpoint change rejected: {address:#x}, {kind}, enable={enable}")


def measure(exe, rom, weather, difficulty, port, boot_wait, timeout, max_events):
    result = {"selected_weather": weather, "selected_difficulty": difficulty,
              "circuit_index": 0, "players": 1, "complete": False,
              "countdown_events": []}
    command = [str(exe), "--kiosk", "--no-file-prompt",
               "--setting", "DebugServer/Enabled=true",
               "--setting", f"DebugServer/Port={port}",
               "--setting", "DebugServer/UseIPv4=true",
               "--setting", "Boot/AwaitGDBClient=false", str(rom)]
    process = subprocess.Popen(command, cwd=exe.parent, stdout=subprocess.DEVNULL,
                               stderr=subprocess.DEVNULL,
                               creationflags=getattr(subprocess, "CREATE_NO_WINDOW", 0))
    client = None
    try:
        time.sleep(boot_wait)
        if process.poll() is not None:
            raise RuntimeError(f"ares exited during boot: {process.returncode}")
        client = WeatherClient(port=port, timeout=5)
        client.connect()
        client._drain(0.3)
        if not client.enable_no_ack_mode():
            raise RuntimeError("No-ack mode rejected")
        client._drain(0.2)
        result["pause_stop"] = pause(client)
        client._drain(0.5)
        result["initial_state"] = read(client, STATE)
        if result["initial_state"] not in (3, 4, 5, 6):
            raise RuntimeError("Expected menu phase before selection writes")
        for address, value in {
                0x800C4E98: 1, 0x800C4E9A: 1, 0x800C4EA8: 2,
                0x800C4EAE: 0, 0x800C4EB0: 0, 0x800C4ED0: 3,
                0x800C4F30: weather, 0x800C5002: difficulty}.items():
            write_halfword(client, address, value)
        write_halfword(client, STATE, 7)
        result["weather_stop"] = wait_write(client, ACTIVE_WEATHER, timeout)
        result["active_weather"] = read(client, ACTIVE_WEATHER)
        watch(client, ACTIVE_WEATHER, "write", False)
        result["state_stop"] = wait_write(client, STATE, timeout)
        watch(client, STATE, "write", False)
        result["loaded_state"] = read(client, STATE)
        result["active_difficulty"] = read(client, 0x800C4FFE)
        result["active_circuit"] = read(client, 0x800C4FFA)
        if (result["loaded_state"], result["active_weather"],
                result["active_difficulty"], result["active_circuit"]) != (8, weather, difficulty, 0):
            raise RuntimeError("Loader did not apply requested selections")

        parameter = 0x8007DAF0 + weather * 16 + difficulty * 4
        bits = read(client, parameter, 4)
        result["speed_parameter"] = struct.unpack(">f", bits.to_bytes(4, "big"))[0]
        result["speed_parameter_address"] = f"0x{parameter:08X}"
        baseline = read(client, 0x8007DA78 + difficulty * 2)
        result["countdown_table_value"] = baseline
        expected = baseline >> 1 if weather == 1 else baseline
        result["expected_countdown_initialization"] = expected
        watch(client, parameter, "read")
        addresses = {0x800989D0 + racer * 0x84 + 0x30: racer for racer in range(1, 7)}
        for address in addresses:
            watch(client, address, "write")
        initialized = set()
        countdown_observed = False
        for _ in range(max_events):
            stop = client.continue_until_halt(timeout=timeout)
            reason = client.parse_stop_reason(stop)
            address = reason.get("addr")
            if reason.get("kind") == "rwatch" and address == parameter:
                result["speed_parameter_stop"] = stop
                result["speed_reader_racer"] = read(client, 0x800C4E9E)
                vehicle = read(client, 0x8008C7D8)
                if vehicle >= 64:
                    raise RuntimeError(f"Invalid vehicle index {vehicle}")
                result["vehicle_index_at_speed_read"] = vehicle
                vehicle_base = 0x800BA1E0 + vehicle * 0x10C
                result["vehicle_speed_at_parameter_read"] = read(client, vehicle_base + 0x90, 4)
                result["throttle_demand_at_parameter_read"] = read(client, vehicle_base + 0xAA)
                watch(client, parameter, "read", False)
            elif reason.get("kind") == "watch" and address in addresses:
                value = read(client, address)
                event = {"racer": addresses[address], "address": f"0x{address:08X}",
                         "value": value, "stop": stop}
                result["countdown_events"].append(event)
                if value == expected:
                    initialized.add(address)
                elif address in initialized and value == expected - 1:
                    countdown_observed = True
            else:
                raise RuntimeError(f"Unexpected AI stop: {stop!r}")
            if countdown_observed and "speed_parameter_stop" in result:
                result["complete"] = True
                break
        if not result["complete"]:
            raise RuntimeError("No speed read and countdown initialization/decrement in event budget")
    except Exception as exc:
        result["error"] = f"{type(exc).__name__}: {exc}"
    finally:
        try:
            if client:
                client.disconnect()
        finally:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=5)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ares", type=Path, required=True)
    parser.add_argument("--rom", type=Path, default=ROOT / "Super Speed Race 64 (Japan).z64")
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--weather", type=int, nargs="+", choices=range(3), default=[0, 1, 2])
    parser.add_argument("--difficulty", type=int, nargs="+", choices=range(4), default=[0, 2])
    parser.add_argument("--port", type=int, default=9341)
    parser.add_argument("--boot-wait", type=float, default=20)
    parser.add_argument("--timeout", type=float, default=30)
    parser.add_argument("--max-events", type=int, default=24)
    args = parser.parse_args()
    if not 1 <= args.port <= 65535 or min(args.boot_wait, args.timeout, args.max_events) <= 0:
        parser.error("Port, timing and event limits must be positive and valid")
    exe, rom = args.ares.resolve(), args.rom.resolve()
    if hashlib.sha256(rom.read_bytes()).hexdigest() != JP_SHA256:
        parser.error("JP cartridge SHA-256 mismatch")
    with socket.socket() as sock:
        sock.settimeout(0.5)
        if sock.connect_ex(("127.0.0.1", args.port)) == 0:
            parser.error("Debugger port is already in use")
    result = {"schema": 1, "evidence_type": "controlled-live-jp-ai-read-and-write",
              "captured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
              "rom_sha256": JP_SHA256, "emulator_sha256": hashlib.sha256(exe.read_bytes()).hexdigest(),
              "probe_sha256": hashlib.sha256(Path(__file__).read_bytes().replace(b"\r\n", b"\n")).hexdigest(),
              "requested_cases": [{"weather": weather, "difficulty": difficulty}
                                  for difficulty in args.difficulty for weather in args.weather],
              "boot_wait_seconds": args.boot_wait, "timeout_seconds": args.timeout,
              "max_countdown_events": args.max_events, "complete": False, "runs": [],
              "limitations": ["Debugger writes race selections; not a menu playthrough",
                              "No port trajectory comparison or AI sight-distance measurement",
                              "Difficulty values are JP indices, not an asserted USA tier mapping"]}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    def save():
        args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    save()
    for difficulty in args.difficulty:
        for weather in args.weather:
            case = measure(exe, rom, weather, difficulty, args.port,
                           args.boot_wait, args.timeout, args.max_events)
            result["runs"].append(case)
            print(json.dumps(case), flush=True)
            save()
    result["complete"] = True
    save()
    return 0 if all(row["complete"] for row in result["runs"]) else 1


if __name__ == "__main__":
    raise SystemExit(main())
