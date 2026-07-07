#!/usr/bin/env python3
"""Decode an RT64 texture dump into viewable PNGs (issue #9).

RT64's "Start dumping textures" (or this port's `texture_dump` config / LAMBO_TEXTURE_DUMP
env) writes, per unique texture, a set of files named by the 64-bit texture hash:

    <hash>.v5.tmem              raw 4 KB TMEM snapshot -- this tool decodes texels from here
    <hash>.v5.tile.json         { tile:{fmt,siz,line,tmem,palette,...}, width, height, tlut }
    <hash>.v5.rice.rdram        linear RDRAM slice the tile loaded from (not used by this tool;
                                a block source can't be un-interleaved without replaying DXT)
    <hash>.v5.rice.json         { tile, type, texture:{address,fmt,siz,width} }
    <hash>.v5.rice.palette.rdram  raw TLUT bytes, CI textures only -- the palette source

Neither RT64 nor its texture_hasher/texture_packer tools turn these into an image, so you
cannot *see* a texture to decide whether it is the hard-to-read text you want to replace.
This tool does that decode, emitting <hash>.png per texture plus an index.html contact
sheet for eyeballing the whole dump at once.

The PNG the tool emits is only for identification/upscaling reference -- the actual
replacement texture is authored fresh at higher resolution, so a close-enough decode is
fine. Byte-order/swizzle flags exist because the port hands RT64 its RDRAM in a
mupen/N64Recomp convention; calibrate once against an in-game screenshot, then leave the
working flags as the defaults.

CI4/CI8 (palettized) textures used to come out "sheared and miscoloured" (issue #50).
The root cause was the TLUT byte order, not the texel decode: the .rice.palette.rdram
dump is a *raw* copy of RT64's RDRAM, which stores every 32-bit word byte-swapped
(logical byte A lives at physical A^3, see loadWord() in rt64_rdp.cpp). Reading the
16-bit TLUT entries without that swap maps each palette index to a garbled RGBA5551
value; because the noise still carries the image's index structure it reads as a
diagonal "shear". Applying the ^3 swap when reading the palette fixes both symptoms.

Usage:
    python tools/decode_dump.py <dump_dir> [--out <png_dir>]
                                           [--no-swizzle] [--pal-no-swap]
                                           [--filter <hex-substr>]

Self-contained: standard library only (zlib for PNG deflate).
"""

import argparse
import json
import struct
import sys
import zlib
from pathlib import Path

# N64 image format (fmt) and pixel size (siz) enums.
FMT_RGBA, FMT_YUV, FMT_CI, FMT_IA, FMT_I = 0, 1, 2, 3, 4
SIZ_4B, SIZ_8B, SIZ_16B, SIZ_32B = 0, 1, 2, 3

FMT_NAME = {FMT_RGBA: "RGBA", FMT_YUV: "YUV", FMT_CI: "CI", FMT_IA: "IA", FMT_I: "I"}
SIZ_BITS = {SIZ_4B: 4, SIZ_8B: 8, SIZ_16B: 16, SIZ_32B: 32}


def fmt_label(fmt, siz):
    return f"{FMT_NAME.get(fmt, '?')}{SIZ_BITS.get(siz, '?')}"


# --- tiny PNG writer (RGBA8, no dependencies) ---------------------------------

def write_png(path, width, height, rgba_rows):
    """rgba_rows: list of `height` bytes objects, each width*4 bytes (RGBA8)."""
    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data +
                struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    raw = bytearray()
    for row in rgba_rows:
        raw.append(0)  # filter type 0 (None)
        raw.extend(row)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)  # 8-bit RGBA
    png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", ihdr) +
           chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b""))
    path.write_bytes(png)


# --- texel conversions to RGBA8 -----------------------------------------------

def rgba16_to_rgba8(px):
    r = (px >> 11) & 0x1F
    g = (px >> 6) & 0x1F
    b = (px >> 1) & 0x1F
    a = px & 0x1
    return ((r << 3) | (r >> 2), (g << 3) | (g >> 2), (b << 3) | (b >> 2),
            255 if a else 0)


def ia16_to_rgba8(px):
    i = (px >> 8) & 0xFF
    a = px & 0xFF
    return (i, i, i, a)


def i_expand(v, bits):
    # replicate high bits down so 4-bit 0xF -> 0xFF
    v &= (1 << bits) - 1
    return (v << (8 - bits)) | (v >> (2 * bits - 8)) if bits >= 4 else v * 255 // ((1 << bits) - 1)


# --- TMEM addressing ----------------------------------------------------------

def tmem_byte(tmem, addr, swizzle, row_odd):
    """Read one byte from the 4KB TMEM snapshot, applying the N64 odd-row
    32-bit-word swap (byte address ^ 4) that TMEM uses for <=16bpp textures."""
    if swizzle and row_odd:
        addr ^= 0x4
    if 0 <= addr < len(tmem):
        return tmem[addr]
    return 0


# --- linear RDRAM addressing (for the .rice.palette.rdram TLUT slice) -----------
#
# RT64 keeps RDRAM byte-swapped within each 32-bit word: the logical byte at N64
# address A physically lives at RDRAM[A ^ 3] (see loadWord() in rt64_rdp.cpp,
# `RDRAM[(textureAddress + i) ^ 3]`). The .rice.palette.rdram dump is a *raw* copy
# of that physical RDRAM, so reading a logical byte back needs the same `^ 3`.
# A TLUT is DMA-loaded, so its slice always starts 8-aligned -- the swap therefore
# reduces to a plain `logical_off ^ 3` with no per-slice address anchor. (Texels
# come from the logical-order .tmem snapshot, which needs no such swap.)

def rdram_byte(buf, logical_off, swap=True):
    """Read one logical byte at `logical_off` from a raw, 8-aligned RDRAM slice.
    With swap, undoes RT64's 32-bit-word byteswap (`^ 3`)."""
    file_off = (logical_off ^ 0x3) if swap else logical_off
    if 0 <= file_off < len(buf):
        return buf[file_off]
    return 0


def read_palette_rdram(pal_bytes, count, swap=True):
    """TLUT entries are big-endian uint16 in logical N64 memory. The dump is raw
    physical RDRAM, so read each byte through RT64's 32-bit-word swap."""
    entries = []
    for i in range(count):
        hi = rdram_byte(pal_bytes, i * 2, swap)
        lo = rdram_byte(pal_bytes, i * 2 + 1, swap)
        entries.append((hi << 8) | lo)
    return entries


def decode_from_tmem(tmem, tile, width, height, tlut_kind, palette, swizzle):
    fmt, siz = tile["fmt"], tile["siz"]
    base = tile["tmem"] * 8       # tmem word (64-bit) -> byte
    line = tile["line"] * 8       # line stride in bytes
    pal_base = (tile.get("palette", 0) << 4)  # CI4 sub-palette

    def pal_rgba(idx):
        if idx < len(palette):
            v = palette[idx]
            return ia16_to_rgba8(v) if tlut_kind == "IA16" else rgba16_to_rgba8(v)
        return (255, 0, 255, 255)

    rows = []
    for t in range(height):
        odd = bool(t & 1)
        row = bytearray()
        for s in range(width):
            if siz == SIZ_16B:
                a = base + t * line + s * 2
                hi = tmem_byte(tmem, a, swizzle, odd)
                lo = tmem_byte(tmem, a + 1, swizzle, odd)
                px = (hi << 8) | lo
                row += bytes(rgba16_to_rgba8(px) if fmt == FMT_RGBA else ia16_to_rgba8(px))
            elif siz == SIZ_8B:
                a = base + t * line + s
                b = tmem_byte(tmem, a, swizzle, odd)
                if fmt == FMT_CI:
                    row += bytes(pal_rgba(b))
                elif fmt == FMT_IA:
                    i = (b >> 4) & 0xF
                    al = b & 0xF
                    row += bytes((i_expand(i, 4),) * 3 + (i_expand(al, 4),))
                else:  # I8
                    row += bytes((b, b, b, b))
            elif siz == SIZ_4B:
                a = base + t * line + (s >> 1)
                b = tmem_byte(tmem, a, swizzle, odd)
                nib = (b >> 4) if (s & 1) == 0 else (b & 0xF)
                if fmt == FMT_CI:
                    row += bytes(pal_rgba(pal_base | nib))
                elif fmt == FMT_IA:
                    i = (nib >> 1) & 0x7
                    al = nib & 0x1
                    row += bytes((i_expand(i, 3),) * 3 + (255 if al else 0,))
                else:  # I4
                    v = i_expand(nib, 4)
                    row += bytes((v, v, v, v))
            else:  # 32b RGBA: RG in low bank, BA in high bank (+0x800)
                a = base + t * line + s * 2
                r = tmem_byte(tmem, a, swizzle, odd)
                g = tmem_byte(tmem, a + 1, swizzle, odd)
                b_ = tmem_byte(tmem, a + 0x800, swizzle, odd)
                al = tmem_byte(tmem, a + 0x801, swizzle, odd)
                row += bytes((r, g, b_, al))
        rows.append(bytes(row))
    return rows


def main():
    ap = argparse.ArgumentParser(description="Decode an RT64 texture dump to PNGs.")
    ap.add_argument("dump_dir", type=Path)
    ap.add_argument("--out", type=Path, default=None, help="output dir (default <dump>/png)")
    ap.add_argument("--no-swizzle", action="store_true",
                    help="disable the odd-row TMEM word swap (calibration knob)")
    ap.add_argument("--pal-no-swap", action="store_true",
                    help="read the TLUT without RT64's 32-bit-word byteswap "
                         "(calibration knob; the swapped read is correct for this port "
                         "and is the default -- issue #50)")
    ap.add_argument("--filter", default=None, help="only decode hashes containing this hex substring")
    args = ap.parse_args()

    out = args.out or (args.dump_dir / "png")
    out.mkdir(parents=True, exist_ok=True)
    swizzle = not args.no_swizzle

    tiles = sorted(args.dump_dir.glob("*.tile.json"))
    if not tiles:
        print(f"No *.tile.json in {args.dump_dir} -- is this a dump directory?", file=sys.stderr)
        return 1

    index = []
    for tj in tiles:
        base = tj.name[:-len(".tile.json")]     # e.g. "0001d055....v5"
        hexhash = base.split(".")[0]
        if args.filter and args.filter.lower() not in hexhash.lower():
            continue
        meta = json.loads(tj.read_text())
        tile = meta["tile"]
        width, height = int(meta["width"]), int(meta["height"])
        tlut_kind = meta.get("tlut", "None")

        tmem_path = args.dump_dir / (base + ".tmem")
        if not tmem_path.exists():
            print(f"skip {hexhash}: no .tmem", file=sys.stderr)
            continue
        tmem = tmem_path.read_bytes()

        palette = []
        if tile["fmt"] == FMT_CI:
            pal_path = args.dump_dir / (base + ".rice.palette.rdram")
            if pal_path.exists():
                # Read up to a full 256-entry TLUT: CI4 indexes it as
                # (tile.palette << 4) | nib, which reaches past the low 16 when a
                # non-zero palette bank is selected. read_palette_rdram zero-fills
                # a short file, so over-requesting is safe. The .rice.palette.rdram
                # is raw physical RDRAM, so the entries need RT64's 32-bit-word
                # byteswap (issue #50: without it every index maps to a garbled
                # RGBA5551 value, which reads as "sheared and miscoloured").
                pal_data = pal_path.read_bytes()
                count = min(256, len(pal_data) // 2)
                palette = read_palette_rdram(pal_data, count, not args.pal_no_swap)

        if width <= 0 or height <= 0 or width > 1024 or height > 1024:
            print(f"skip {hexhash}: implausible {width}x{height}", file=sys.stderr)
            continue

        rows = decode_from_tmem(tmem, tile, width, height, tlut_kind, palette, swizzle)
        png_path = out / (hexhash + ".png")
        write_png(png_path, width, height, rows)
        index.append((hexhash, width, height, fmt_label(tile["fmt"], tile["siz"]), tlut_kind))

    # Contact sheet for eyeballing the whole dump.
    html = ["<!doctype html><meta charset=utf8><title>texture dump</title>",
            "<style>body{background:#222;color:#ddd;font:12px monospace}",
            "figure{display:inline-block;margin:6px;text-align:center;vertical-align:top}",
            "img{image-rendering:pixelated;background:#000;border:1px solid #444;max-width:256px}",
            "figcaption{max-width:256px;word-break:break-all}</style>"]
    for h, w, ht, fl, tl in sorted(index):
        html.append(f'<figure><img src="{h}.png" width="{min(w*2,256)}">'
                    f'<figcaption>{h}<br>{w}x{ht} {fl} {tl}</figcaption></figure>')
    (out / "index.html").write_text("\n".join(html))

    print(f"decoded {len(index)} textures -> {out}")
    print(f"open {out / 'index.html'} to browse")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
