#!/usr/bin/env python3
"""
dump_memory2.py — Capture memory from ares once game is confirmed running.
Usage: python dump_memory2.py
"""
import sys
import os
import time
import socket
import subprocess
import argparse

sys.path.insert(0, os.path.dirname(__file__))
from ares_debug_client import AresDebugClient

ARES_DIR = os.path.join(os.path.dirname(__file__), '..', '..', 'tools', 'emulators', 'ares-base', 'ares-v147')
ARES_EXE = os.path.join(ARES_DIR, 'ares.exe')
ROM = os.path.join(os.path.dirname(__file__), '..', '..', 'Automobili Lamborghini (USA).z64')

REGIONS = [
    ("DMA_HEAD_ptr",       0x800A39CC, 16),     # pointer to current DL head
    ("DL_base_static",     0x800BF1D8, 0x400),  # fallback DL base
    ("sprite_desc_table",  0x800A5EE8, 0x400),
    ("rsp_state_main",     0x800DE6A4, 0x200),
    ("rsp_state_2",        0x800DE790, 0x100),
    ("tile_state",         0x800D5F38, 0x80),
    ("tmem_texture",      0x80090000, 0x2000),
]

def wait_for_port(port, timeout=15.0):
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
    parser = argparse.ArgumentParser(description="Capture memory from ares once the game is running")
    parser.add_argument("--ares-exe", default=None,
                        help="Path to ares executable (default: built-in ares-v147)")
    parser.add_argument("--rom", default=ROM,
                        help="Path to ROM file (default: <repo>/Automobili Lamborghini (USA).z64)")
    args = parser.parse_args()

    ares_exe = args.ares_exe or ARES_EXE
    rom     = args.rom

    if not wait_for_port(9150, timeout=2.0):
        print("Launching ares...")
        if not os.path.exists(ares_exe):
            print(f"ERROR: ares not found at {ares_exe}")
            return
        if not os.path.exists(rom):
            print(f"ERROR: ROM not found at {rom}")
            return
        subprocess.Popen([
            ares_exe,
            '--kiosk', '--no-file-prompt',
            '--setting', 'DebugServer/Enabled=true',
            '--setting', 'DebugServer/Port=9150',
            '--setting', 'DebugServer/UseIPv4=true',
            '--setting', 'Boot/AwaitGDBClient=false',
            rom
        ], cwd=os.path.dirname(ares_exe), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        print("Waiting for ares to start...")
        if not wait_for_port(9150, timeout=15.0):
            print("ERROR: ares failed to start")
            return
        print("ares started.")
    else:
        print("ares already running.")

    client = AresDebugClient(port=9150)
    client.connect()
    print("Connected to ares DebugServer.")

    # Poll VI_CURRENT until non-zero (game is running)
    print("Polling for VI_CURRENT != 0 (waiting for game to start)...")
    for i in range(60):  # up to 30 seconds
        vi = int.from_bytes(client.read_mem(0xA4400010, 4), 'big')
        if vi != 0:
            print(f"  VI_CURRENT = 0x{vi:08X} after {i+1} polls ({i*0.5:.1f}s)")
            break
        time.sleep(0.5)
    else:
        print("WARNING: VI_CURRENT still 0 after 30s — proceeding anyway")

    # Now wait a bit more for the game to reach the copyright screen (~frame 90)
    # At ~60fps, frame 90 is ~1.5 seconds. Wait 3 more seconds.
    print("Waiting 5s for copyright screen...")
    time.sleep(5)

    # Check VI one more time
    vi = int.from_bytes(client.read_mem(0xA4400010, 4), 'big')
    print(f"VI_CURRENT at capture = 0x{vi:08X}")

    print("\n" + "="*60)
    print("MEMORY DUMP")
    print("="*60)

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
            # u32 scan for opcode-like patterns
            buckets = {0x4:0, 0x6:0, 0x7:0, 0xA:0, 0xB:0, 0x1:0, 0x3:0, 0x2:0, 0x5:0, 0x8:0, 0x9:0, 0xC:0, 0xD:0, 0xE:0, 0xF:0}
            nw = size // 4
            for wi in range(nw):
                try:
                    w = int.from_bytes(data[wi*4:wi*4+4], 'big')
                    tn = (w >> 28) & 0xF
                    if tn in buckets:
                        buckets[tn] += 1
                except Exception:
                    pass
            interesting = {k: v for k, v in buckets.items() if v > 0}
            print(f"  top-nibble: {interesting}")
        except Exception as e:
            print(f"  ERROR: {e}")

    # Also: follow the DMA head pointer to find the actual DL
    print("\n" + "="*60)
    print("FOLLOWING DMA HEAD POINTER")
    print("="*60)
    try:
        ptr_data = client.read_mem(0x800A39CC, 8)
        ptr_val = int.from_bytes(ptr_data[:4], 'big')
        print(f"DMA_QUEUE_HEAD @ 0x800A39CC = 0x{ptr_val:08X}")
        if ptr_val >= 0x80000000 and ptr_val < 0x80400000:
            dl_data = client.read_mem(ptr_val, 0x400)
            print(f"DL at 0x{ptr_val:08X} (first 0x400 bytes):")
            for off in range(0, min(0x400, len(dl_data)), 16):
                chunk = dl_data[off:off+16]
                if not chunk:
                    break
                hex_str = ' '.join(f'{b:02x}' for b in chunk)
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
                print(f"  +0x{off:04X}: {hex_str:48s}  {ascii_str}")
            buckets = {0x4:0, 0x6:0, 0x7:0, 0xA:0, 0x1:0, 0x3:0, 0x2:0}
            nw = min(0x400, len(dl_data)) // 4
            for wi in range(nw):
                try:
                    w = int.from_bytes(dl_data[wi*4:wi*4+4], 'big')
                    tn = (w >> 28) & 0xF
                    if tn in buckets:
                        buckets[tn] += 1
                except Exception:
                    pass
            interesting = {k: v for k, v in buckets.items() if v > 0}
            print(f"  top-nibble: {interesting}")
        else:
            print(f"  Pointer value 0x{ptr_val:08X} out of expected range")
    except Exception as e:
        print(f"  ERROR following pointer: {e}")

    client.disconnect()
    print("\nDone.")

if __name__ == "__main__":
    main()
