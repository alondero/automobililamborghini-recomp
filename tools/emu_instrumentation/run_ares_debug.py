#!/usr/bin/env python3
"""
run_ares_debug.py — Headless ares DebugServer instrumentation for RDP tracing.

Launches ares with DebugServer enabled, connects via GDB Remote Protocol TCP,
captures RDP register state per frame, and outputs structured JSON.

Key discovery (2026-04-21): ares DebugServer is halted by default when GDB connects.
The game is NOT running until 'c' is sent. In stop-mode, 'c' does NOT return until
a breakpoint/halt. So the emulator effectively runs only when we send 'c', and after
each 'c' the target halts again (single-step mode effectively).

The architecture requires stepping continuously to let the game run. This is slow
(~1000 steps/second) but should allow DPC registers to advance.

Note: VI_CURRENT is at 0xA4400010 in ares (VI base is 0xA4400000, not 0xA43C0000).
VI_CURRENT returns live vcounter (dynamically updated). DPC registers at 0xA3C0
are not accessible via ares GDB (mapped differently than real N64 hardware).

Usage:
  python tools/emu_instrumentation/run_ares_debug.py --frames 60 --output emu_rdp.json ROM.z64
"""

import argparse
import json
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[2]
ARES_DIR = REPO_ROOT / "tools" / "emulators" / "ares-base" / "ares-v147"
ARES_EXE = ARES_DIR / "ares.exe"

DEFAULT_PORT = 9150

# Override path for patched ares builds (set via --ares-exe)
_patched_ares_exe = None


def wait_for_port(host, port, timeout=30.0):
    """Poll TCP port until ares DebugServer is ready."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect((host, port))
            s.close()
            return True
        except (socket.error, socket.timeout):
            time.sleep(0.5)
    return False


def launch_ares_via_batch(ares_exe, rom_path, port):
    """Launch ares via batch file + 'start' command (required on Windows).

    Without 'start', the cmd.exe subprocess blocks until ares exits, which prevents
    us from connecting to the DebugServer. Using 'start /b' runs ares in background
    while allowing cmd.exe to return immediately.
    """
    fd, bat_path = tempfile.mkstemp(suffix=".bat", prefix="ares_debug_")
    os.close(fd)
    bat_path = Path(bat_path)

    ares_cmd = (
        f'@echo off\n'
        f'cd /d "{ARES_DIR}"\n'
        f'start /b "" "{ares_exe}" --kiosk --no-file-prompt '
        f'--setting DebugServer/Enabled=true '
        f'--setting DebugServer/Port={port} '
        f'--setting DebugServer/UseIPv4=true '
        f'--setting Boot/AwaitGDBClient=false '
        f'"{rom_path}"\n'
    )
    with open(bat_path, "w") as f:
        f.write(ares_cmd)

    process = subprocess.Popen(
        ["cmd.exe", "/c", str(bat_path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        cwd=str(ARES_DIR),
    )
    return process, bat_path


def main():
    parser = argparse.ArgumentParser(description="ares DebugServer RDP tracer")
    parser.add_argument("rom", help="Path to ROM file (.z64)")
    parser.add_argument("--frames", type=int, default=60,
                        help="Number of frames to capture (default: 60)")
    parser.add_argument("--output", "-o", default="emu_rdp_trace.json",
                        help="Output JSON file (default: emu_rdp_trace.json)")
    parser.add_argument("--port", type=int, default=DEFAULT_PORT,
                        help=f"DebugServer TCP port (default: {DEFAULT_PORT})")
    parser.add_argument("--host", default="127.0.0.1",
                        help="DebugServer host (default: 127.0.0.1)")
    parser.add_argument("--timeout", type=int, default=120,
                        help="Total timeout in seconds (default: 120)")
    parser.add_argument("--step-loop", action="store_true",
                        help="Use continuous stepping to advance game state (slow)")
    parser.add_argument("--ares-exe", default=None,
                        help="Path to ares executable (default: built-in ares-v147)")
    args = parser.parse_args()

    rom_path = Path(args.rom)
    if not rom_path.is_absolute():
        rom_path = REPO_ROOT / rom_path
    rom_path = rom_path.resolve()

    ares_exe = Path(args.ares_exe) if args.ares_exe else ARES_EXE

    if not ares_exe.exists():
        print(f"Error: ares not found at {ares_exe}")
        sys.exit(1)
    if not rom_path.exists():
        print(f"Error: ROM not found at {rom_path}")
        sys.exit(1)

    print(f"Launching ares with DebugServer on port {args.port}...")
    process, bat_path = launch_ares_via_batch(ares_exe, rom_path, args.port)

    try:
        print(f"Waiting for DebugServer at {args.host}:{args.port}...")
        if not wait_for_port(args.host, args.port, timeout=30.0):
            print("Error: DebugServer did not become ready in time")
            sys.exit(1)

        sys.path.insert(0, str(REPO_ROOT / "tools" / "emu_instrumentation"))
        from ares_debug_client import AresDebugClient

        print(f"Connecting to ares DebugServer at {args.host}:{args.port}...")
        client = AresDebugClient(host=args.host, port=args.port, timeout=30.0)
        client.connect()

        # Wait a moment for game to initialize
        time.sleep(0.5)

        # The game runs freely with AwaitGDBClient=false
        # Poll VI_CURRENT until it becomes non-zero (game boot)
        print("Polling for game boot (VI_CURRENT != 0)...")
        boot_timeout = 30.0
        start = time.time()
        vi_current = 0

        if args.step_loop:
            # Step in a tight loop to advance game state (very slow but reliable)
            print("Using step-loop mode (slow stepping to advance state)...")
            step_count = 0
            while time.time() - start < boot_timeout:
                try:
                    client.step()
                except Exception:
                    pass
                step_count += 1
                if step_count % 1000 == 0:
                    vi_current = client.read32(0xA4400010)
                    vcnt = vi_current & 0x1FF
                    print(f"  stepped {step_count} instructions, VI=0x{vi_current:04X} vcnt={vcnt}")
                    if vcnt != 0:
                        print(f"Game booted after {step_count} steps!")
                        break
            if vi_current == 0:
                print(f"Warning: VI_CURRENT still 0 after {step_count} steps")
        else:
            # Poll without step — just read VI_CURRENT
            while time.time() - start < boot_timeout:
                vi_current = client.read32(0xA4400010)
                vcnt = vi_current & 0x1FF
                if vcnt != 0:
                    print(f"Game booted! VI=0x{vi_current:04X} vcnt={vcnt}")
                    break
                time.sleep(0.1)

        if vi_current == 0:
            print("Warning: Game may not have booted (VI_CURRENT = 0)")
            print("Proceeding anyway with frame capture...")

        # Sync to frame boundary: wait for vcnt=0 (vertical blank) before starting frame detection
        print("Syncing to frame boundary (waiting for vcnt=0)...")
        sync_start = time.time()
        last_debug = 0
        while time.time() - sync_start < 5.0:
            vi_current = client.read32(0xA4400010)
            vcnt = vi_current & 0x1FF
            elapsed = time.time() - sync_start
            if elapsed - last_debug > 0.5:
                print(f"  sync t={elapsed:.2f}s: vcnt={vcnt}")
                last_debug = elapsed
            if vcnt == 0:
                print(f"  Synced at vcnt=0 (t={elapsed:.2f}s), starting capture...")
                prev_vi = vi_current
                break
            time.sleep(0.005)

        # Measure actual ares GDB round-trip latency on first few reads
        print("Measuring ares GDB round-trip latency...")
        for _ in range(5):
            t0 = time.perf_counter()
            client.read32(0xA4400010)
            dt = (time.perf_counter() - t0) * 1000
            print(f"  read32 latency: {dt:.1f}ms")

        # Capture frames
        frames_data = []
        frame_count = 0
        poll_start = time.time()
        prev_vi = 0
        pending_frame = {}  # {vi_control, vi_dram_addr, vi_width, ...}

        print(f"Capturing {args.frames} frames...")
        last_capture_debug = 0.0
        while frame_count < args.frames and (time.time() - poll_start) < args.timeout:
            # Poll VI_CURRENT — one read per iteration instead of six.
            # Read full VI block only during active display (not vblank) so values are live.
            vi_current = client.read32(0xA4400010)
            curr_vcnt = vi_current & 0x1FF
            elapsed = time.time() - poll_start

            if curr_vcnt != 0:
                # Active display — capture live VI state for the next frame.
                # Read individual VI registers (ares only returns data for 4-byte-aligned reads).
                pending_frame = {
                    "vi_current":    client.read32(0xA4400010),
                    "vi_v_sync":     client.read32(0xA4400018),
                    "vi_timing":     client.read32(0xA4400014),
                    "vi_width":      client.read32(0xA4400008),
                    "vi_dram_addr":  client.read32(0xA4400004),
                    "vi_control":    client.read32(0xA4400000),
                }
                prev_vi = vi_current
            elif prev_vi != 0 and pending_frame:
                # Transition from active display to vblank — frame is complete.
                # Use the values captured during active display (pending_frame).
                state = {"frame": frame_count}
                state.update(pending_frame)
                frames_data.append(state)
                frame_count += 1
                print(f"  Frame {frame_count}: vcnt={curr_vcnt:3d}, DRAM=0x{pending_frame['vi_dram_addr']:08X}")
                pending_frame = {}

            if elapsed - last_capture_debug > 5.0 and frame_count < args.frames:
                print(f"  [t={elapsed:.1f}s] frames={frame_count}, vcnt={curr_vcnt}, prev_vi=0x{prev_vi:04X}")
                last_capture_debug = elapsed

        client.disconnect()
        print(f"Captured {frame_count} frames")

    finally:
        subprocess.run(["taskkill", "/F", "/T", "/IM", "ares.exe"],
                       capture_output=True)
        process.wait()
        if bat_path.exists():
            try:
                os.remove(bat_path)
            except Exception:
                pass

    output_path = Path(args.output)
    with open(output_path, "w") as f:
        json.dump(frames_data, f, indent=2)

    print(f"\nCaptured {len(frames_data)} frames -> {output_path}")


if __name__ == "__main__":
    main()