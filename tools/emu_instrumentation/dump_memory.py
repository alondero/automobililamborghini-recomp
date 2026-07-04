#!/usr/bin/env python3
"""
dump_memory.py — Quick script to launch ares and dump memory regions.
Usage: python dump_memory.py
"""
import sys
import os
import time
import socket
import subprocess

sys.path.insert(0, os.path.dirname(__file__))
from ares_debug_client import AresDebugClient

ARES_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'tools', 'emulators', 'ares-base', 'ares-v147')
ARES_EXE = os.path.join(ARES_DIR, 'ares.exe')
ROM = os.path.join(os.path.dirname(__file__), '..', '..', 'Automobili Lamborghini (USA).z64')

REGIONS = [
    ("DL_buffer",          0x800A39CC, 0x400),
    ("sprite_desc_table",   0x800A5EE8, 0x400),
    ("sprite_state_1",      0x800A8278, 0x100),
    ("sprite_state_2",      0x800A8670, 0x100),
    ("rsp_state_main",      0x800DE6A4, 0x200),
    ("rsp_state_2",         0x800DE790, 0x100),
    ("tile_state",          0x800D5F38, 0x80),
    ("tmem_texture",        0x80090000, 0x2000),
]

def wait_for_port(port, timeout=15.0):
    """Wait for a TCP port to become reachable."""
    start = time.time()
    while time.time() - start < timeout:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(1)
            r = s.connect_ex(('127.0.0.1', port))
            s.close()
            if r == 0:
                return True
        except Exception:
            pass
        time.sleep(0.5)
    return False

def main():
    # Check if ares is already running on port 9150
    if wait_for_port(9150, timeout=2.0):
        print("ares already running on port 9150")
    else:
        print("Launching ares...")
        if not os.path.exists(ARES_EXE):
            print(f"ERROR: ares not found at {ARES_EXE}")
            return
        if not os.path.exists(ROM):
            print(f"ERROR: ROM not found at {ROM}")
            return
        subprocess.Popen([
            ARES_EXE,
            '--kiosk', '--no-file-prompt',
            '--setting', 'DebugServer/Enabled=true',
            '--setting', 'DebugServer/Port=9150',
            '--setting', 'DebugServer/UseIPv4=true',
            '--setting', 'Boot/AwaitGDBClient=false',
            ROM
        ], cwd=ARES_DIR, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("Waiting for ares to start...")
        if not wait_for_port(9150, timeout=15.0):
            print("ERROR: ares failed to start on port 9150")
            return
        print("ares started.")

    # Give the game a moment to reach the copyright screen
    # (At 60fps, frame 90 is ~1.5 seconds; we wait 3 seconds to be safe)
    print("Waiting 3s for game to reach copyright screen...")
    time.sleep(3)

    client = AresDebugClient(port=9150)
    client.connect()
    print("Connected to ares DebugServer.")

    # Quick VI check
    vi = int.from_bytes(client.read_mem(0xA4400010, 4), 'big')
    print(f"VI_CURRENT = 0x{vi:08X}")

    print("\n" + "="*60)
    for name, base, size in REGIONS:
        print(f"\n--- {name} @ 0x{base:08X} ({size} bytes) ---")
        try:
            data = client.read_mem(base, size)
            if not data:
                print("  (empty/error)")
                continue
            for off in range(0, size, 16):
                chunk = data[off:off+16]
                if not chunk:
                    break
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                print(f"  +0x{off:04X}: {hex_str:48s}  {ascii_str}")
            # top-nibble scan
            buckets = {0x4:0, 0x6:0, 0x7:0, 0xA:0, 0x1:0, 0x3:0, 0x2:0, 0x5:0}
            nw = size // 4
            for wi in range(nw):
                try:
                    w = int.from_bytes(data[wi*4:wi*4+4], 'big')
                    tn = (w >> 28) & 0xF
                    if tn in buckets:
                        buckets[tn] += 1
                except Exception:
                    pass
            print(f"  top-nibble: {buckets}")
        except Exception as e:
            print(f"  ERROR: {e}")

    client.disconnect()
    print("\nDone.")

if __name__ == "__main__":
    main()
