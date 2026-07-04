#!/usr/bin/env python3
"""
ares_session.py — high-level context manager for ares DebugServer sessions.

Handles headless launch, port wait, GDB connect, and guaranteed cleanup
(taskkill on Windows). Yields an AresDebugClient ready to use.

Usage:
    from ares_session import ares_session

    with ares_session() as c:
        c.set_watchpoint(0x800A39CC, kind='write')
        stop = c.continue_until_halt(timeout=5)
        info = c.parse_stop_reason(stop)
        regs = c.read_kseg0_registers()
        c.clear_watchpoint(0x800A39CC, kind='write')

    # ares is taskkilled on exit even if the block raises.

CLI:
    python tools/emu_instrumentation/ares_session.py --port 9150
    # ...keeps ares running until you Ctrl-C; useful for ad-hoc poking.
"""

import argparse
import contextlib
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
DEFAULT_ROM = REPO_ROOT / "Automobili Lamborghini (USA).z64"

sys.path.insert(0, str(Path(__file__).parent))
from ares_debug_client import AresDebugClient  # noqa: E402


def _wait_port(host: str, port: int, timeout: float = 30.0) -> bool:
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1.0)
            s.connect((host, port))
            s.close()
            return True
        except (socket.error, socket.timeout):
            time.sleep(0.3)
    return False


def _launch_ares(rom_path: Path, port: int, ares_exe: Path = ARES_EXE, await_gdb: bool = False):
    """Launch ares headlessly via a temp .bat file. Returns (process, bat_path).
    `start /b` is required on Windows so cmd.exe returns immediately.

    await_gdb=True sets Boot/AwaitGDBClient=true so the CPU freezes at instruction 0
    until the GDB client continues — REQUIRED to arm a watchpoint BEFORE early-boot
    writes (e.g. the ~0.5s object registration / menu-init). With the default False the
    game has already run ~2.5s by the time the client connects, so any one-shot early
    write is missed (ORCH15 W53, 2026-06-07)."""
    fd, bat_path = tempfile.mkstemp(suffix=".bat", prefix="ares_debug_")
    os.close(fd)
    bat_path = Path(bat_path)
    await_str = "true" if await_gdb else "false"
    with open(bat_path, "w") as f:
        f.write(
            f'@echo off\n'
            f'cd /d "{ares_exe.parent}"\n'
            f'start /b "" "{ares_exe}" --kiosk --no-file-prompt '
            f'--setting DebugServer/Enabled=true '
            f'--setting DebugServer/Port={port} '
            f'--setting DebugServer/UseIPv4=true '
            f'--setting Boot/AwaitGDBClient={await_str} '
            f'"{rom_path}"\n'
        )
    process = subprocess.Popen(
        ["cmd.exe", "/c", str(bat_path)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        cwd=str(ares_exe.parent),
    )
    return process, bat_path


@contextlib.contextmanager
def ares_session(
    rom: Path = DEFAULT_ROM,
    port: int = 9150,
    host: str = "127.0.0.1",
    boot_wait: float = 2.0,
    ares_exe: Path = ARES_EXE,
    no_ack: bool = True,
    await_gdb: bool = False,
):
    """Context manager: launch ares, connect via GDB, yield AresDebugClient.

    Guarantees ares is taskkilled on exit. `boot_wait` is the post-connect
    sleep before yielding — gives the game time to reach a steady state
    (~2s gets you past the boot stub).

    `await_gdb=True` freezes the CPU at instruction 0 (Boot/AwaitGDBClient=true) so
    you can arm a watchpoint BEFORE early-boot writes (~0.5s object registration /
    menu-init). When True, boot_wait is forced to 0 (the CPU isn't running yet) — arm
    your watchpoints/breakpoints, THEN call continue_until_halt() to start execution.
    """
    if await_gdb:
        boot_wait = 0.0
    rom_path = Path(rom).resolve()
    if not rom_path.exists():
        raise FileNotFoundError(f"ROM not found: {rom_path}")
    if not Path(ares_exe).exists():
        raise FileNotFoundError(f"ares not found: {ares_exe}")

    process, bat_path = _launch_ares(rom_path, port, Path(ares_exe), await_gdb=await_gdb)
    client = None
    try:
        if not _wait_port(host, port, timeout=30.0):
            raise RuntimeError(f"ares DebugServer did not open on {host}:{port}")
        client = AresDebugClient(host=host, port=port, timeout=15.0)
        client.connect()
        time.sleep(boot_wait)
        client._drain(0.3)
        if no_ack:
            try:
                client.enable_no_ack_mode()
            except Exception:
                pass
        yield client
    finally:
        if client:
            try:
                client.disconnect()
            except Exception:
                pass
        subprocess.run(
            ["taskkill", "/F", "/T", "/IM", "ares.exe"],
            capture_output=True,
        )
        try:
            process.wait(timeout=5)
        except Exception:
            pass
        try:
            os.remove(bat_path)
        except Exception:
            pass


def _cli():
    p = argparse.ArgumentParser(description="Launch ares with DebugServer and idle until Ctrl-C")
    p.add_argument("--rom", default=str(DEFAULT_ROM))
    p.add_argument("--port", type=int, default=9150)
    p.add_argument("--boot-wait", type=float, default=2.0)
    args = p.parse_args()
    print(f"Launching ares on port {args.port}...")
    with ares_session(rom=Path(args.rom), port=args.port, boot_wait=args.boot_wait) as c:
        print(f"Connected. VI_CURRENT=0x{c.read32(0xA4400010):08X}")
        print("Ctrl-C to stop ares.")
        try:
            while True:
                time.sleep(1)
        except KeyboardInterrupt:
            print("Stopping.")


if __name__ == "__main__":
    _cli()
