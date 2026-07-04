#!/usr/bin/env python3
"""Diff named RDRAM regions across multiple 8 MB ares/PC RDRAM dumps.

Usage:
    python tools/emu_instrumentation/diff_rdram_regions.py \\
        --dumps copyright="<path>" pressstart="<path>" demorace="<path>" pc_state6="<path>"

Reports for each region: first N bytes per dump, byte-equal map, first divergence
offset. Designed for Stage 0 diagnostic baseline.
"""
import argparse
import sys
from pathlib import Path

VRAM_BASE = 0x80000000

# Each region: (name, vram_address, length_bytes, kind)
# kind in {"hex", "u16_be", "u32_be"}
REGIONS = [
    # Sub-DL targets — copied from ROM 0x908D0 at boot
    ("subdl_EFD8",        0x8011EFD8, 32, "hex"),
    ("subdl_EF98",        0x8011EF98, 32, "hex"),
    ("subdl_F100",        0x8011F100, 32, "hex"),
    ("subdl_F208",        0x8011F208, 32, "hex"),
    # Layout / state globals
    ("D_800CE6AC_state",  0x800CE6AC,  2, "u16_be"),
    ("D_800A39CC_dmahead",0x800A39CC,  4, "u32_be"),
    ("D_800986B8",        0x800986B8,  2, "u16_be"),
    ("D_800986D8",        0x800986D8,  2, "u16_be"),
    ("D_800A2BFC_slot",   0x800A2BFC,  4, "u32_be"),
    ("D_800A2D04_limit",  0x800A2D04,  2, "u16_be"),
    ("D_800A2D06_delay",  0x800A2D06,  2, "u16_be"),
    ("D_800A2D08_count",  0x800A2D08,  2, "u16_be"),
    ("D_800985AC_ctr18",  0x800985AC,  2, "u16_be"),
    # Tables read by state-10 TEXRECT helper
    ("D_8016F650_first64",0x8016F650, 64, "hex"),
    ("D_800A6738_first64",0x800A6738, 64, "hex"),
    # Pad state candidates (boot writes 0 at 0x800A3A2C)
    ("BOOT_PAD_SEQ",      0x800A3A2C,  4, "u32_be"),
    ("D_800A3A58_first16",0x800A3A58, 16, "hex"),
    # Scene table at 0x800918A0 (region containing scene_table per funcs_21.c:62 era)
    ("scene_at_918A0",    0x800918A0, 32, "hex"),
    # Framebuffer pointers per scene_table-falsified memory (0x8036A000 etc)
    ("fb_36A000_first16", 0x8036A000, 16, "hex"),
    ("fb_3B5000_first16", 0x803B5000, 16, "hex"),
]


def load(path: Path) -> bytes:
    with path.open("rb") as f:
        data = f.read()
    if len(data) != 8 * 1024 * 1024:
        print(f"WARNING: {path} has length {len(data)}, expected 8 MB", file=sys.stderr)
    return data


def fmt_region(data: bytes, addr: int, length: int, kind: str) -> str:
    off = addr - VRAM_BASE
    if off < 0 or off + length > len(data):
        return "OOB"
    chunk = data[off:off + length]
    if kind == "u16_be":
        return f"0x{int.from_bytes(chunk, 'big'):04X}"
    if kind == "u32_be":
        return f"0x{int.from_bytes(chunk, 'big'):08X}"
    return chunk.hex()


def first_divergence(chunks: dict) -> int:
    """Return index of first byte where any two dumps differ, or -1."""
    values = list(chunks.values())
    if not values:
        return -1
    length = min(len(v) for v in values)
    for i in range(length):
        b0 = values[0][i]
        for v in values[1:]:
            if v[i] != b0:
                return i
    return -1


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--dumps", nargs="+", required=True,
                   help="key=path pairs, e.g. copyright=cp.bin pressstart=ps.bin")
    args = p.parse_args()

    dumps = {}
    for kv in args.dumps:
        key, _, path = kv.partition("=")
        dumps[key] = load(Path(path))

    keys = list(dumps.keys())
    print(f"# RDRAM Region Diff — {len(keys)} dumps: {', '.join(keys)}\n")

    print("| region | addr | " + " | ".join(keys) + " | first_diff |")
    print("|---" * (3 + len(keys)) + "|")

    for name, addr, length, kind in REGIONS:
        cells = []
        chunks = {}
        for k in keys:
            chunk = dumps[k][addr - VRAM_BASE:addr - VRAM_BASE + length]
            chunks[k] = chunk
            cells.append(fmt_region(dumps[k], addr, length, kind))
        fd = first_divergence(chunks)
        fd_str = "-" if fd < 0 else f"@{fd}"
        print(f"| {name} | 0x{addr:08X} | " + " | ".join(cells) + f" | {fd_str} |")


if __name__ == "__main__":
    main()
