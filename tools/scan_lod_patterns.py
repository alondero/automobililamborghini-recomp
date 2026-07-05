#!/usr/bin/env python3
"""Scan the ROM for MIPS distance-test LOD-selection fingerprints and map each
hit to its containing function. Output is HYPOTHESES, not proof — verify each
hit with a real disassembler before patching.

WHY: distance-test LOD selection is "c.lt.s/c.eq.s immediately followed by bc1t
or bc1f within the same function", the textbook MIPS shape. We do proximity
matching (compare + branch within 32 instructions = typical in-function branch
distance) and cross-reference each hit against lamborghini.syms.toml so you
can see which function the candidate lives in.

MIPS encoding (big-endian) — load-bearing for the masks below:
  c.lt.s fX, fY     -> 0x4600_0000 | (Y<<16) | (X<<11) | (0xC<<8) | 0x30  (cond=0xC)
  c.eq.s fX, fY     -> same shape with cond=0x2
  bc1t/bc1f offset  -> 0x4501_0000 / 0x4500_0000 | (offset & 0xFFFF)
The C.cond.S function field is 0x30-0x3F; the BC family shares COP1 (0x45) but
flips to BC format via RS=0x08 (top byte 0x45 vs 0x46 — easy to get wrong).
"""
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ROM = REPO / "Automobili Lamborghini (USA).z64"
SYMS = REPO / "lamborghini.syms.toml"

# MIPS R4300 big-endian opcodes (relevant subset)
#
# COP1 main opcode = 010001 (bits 31-26) → 0x44000000 base
# RS field (bits 25-21) selects COP1 sub-op:
#   01000 (0x08) = BC1F/BC1T branch    → 0x45000000 base, cond in bit 16
#   10000 (0x10) = S (single FP op)    → 0x46000000 base
# Compare instructions (C.cond.S) are S-format with function field 0x30-0x3F,
# cond code in bits 20-18, Fs/Ft fields standard.
#   c.lt.s $fX, $fY  → 0x46000000 | (Y<<16) | (X<<11) | (0xC << 8) | 0x30
#                          cond=0xC (LT), Fs=X, Ft=Y, function=0x30 (C.cond.S)
#   c.le.s $fX, $fY  → ... cond=0xE (LE)
#   c.eq.s $fX, $fY  → ... cond=0x2 (EQ)
#   c.olt.s $fX, $fY → ... cond=0x4 (OLT, signaling)
#
# BC1F/BC1T encoding: 0x45000000 | (cc<<18) | (0 << 17) | (likely<<16) | (offset&0xFFFF)
#   bc1t offset → likely=1, opcode base 0x45010000
#   bc1f offset → likely=0, opcode base 0x45000000
BC1T_BASE = 0x45010000   # bc1t
BC1F_BASE = 0x45000000   # bc1f
COP1_S_BASE = 0x46000000 # COP1 S-format (compare + arithmetic)

LWC1_BASE = 0xC4000000
SWC1_BASE = 0xE4000000

def load_syms():
    """Load lamborghini.syms.toml into a list of (name, vram, size)."""
    syms = []
    in_funcs = False
    with SYMS.open("r") as f:
        for line in f:
            line = line.strip()
            if line.startswith("functions = ["):
                in_funcs = True
                continue
            if in_funcs and line == "]":
                in_funcs = False
                continue
            if in_funcs:
                m = re.match(r'\s*\{\s*name\s*=\s*"([^"]+)",\s*vram\s*=\s*0x([0-9A-Fa-f]+),\s*size\s*=\s*0x([0-9A-Fa-f]+)\s*\}', line)
                if m:
                    syms.append((m.group(1), int(m.group(2), 16), int(m.group(3), 16)))
    return syms

def vram_to_rom(vram):
    """Translate runtime VRAM to ROM offset (whole-ROM linear mapping, vram = rom + 0x7FFFF400)."""
    return vram - 0x7FFFF400

def find_function_at(syms, vram):
    """Return the function name containing a given runtime VRAM address, or None."""
    for name, fvram, size in syms:
        if fvram <= vram < fvram + size:
            return name, fvram, size
    return None

def is_fp_compare(instr):
    """COP1.S compare instructions (c.eq.s/c.lt.s/c.le.s/etc.) — opcode 010001 + format S (10000)
    with function field 0x30-0x3F (C.cond.S family). Mask on bits 31-21 = 0x46000000."""
    if (instr & 0xFFE00000) != COP1_S_BASE:
        return False
    func = instr & 0x3F
    return 0x30 <= func <= 0x3F

def is_bc1(instr):
    """BC1F/BC1T (branch on FP condition) — COP1 BC1 family, bits 31-16 = 0x4500 or 0x4501."""
    return (instr & 0xFFFF0000) in (BC1T_BASE, BC1F_BASE)

def main():
    if not ROM.exists():
        print(f"ROM not found at {ROM}", file=sys.stderr)
        return 1
    if not SYMS.exists():
        print(f"syms not found at {SYMS}", file=sys.stderr)
        return 1

    syms = load_syms()
    print(f"Loaded {len(syms)} functions from {SYMS.name}")
    print(f"ROM size: {ROM.stat().st_size:#x} bytes")
    print()

    data = ROM.read_bytes()
    instr_count = len(data) // 4
    print(f"Scanning {instr_count:#x} MIPS instructions ({len(data):#x} bytes)...")
    print()

    # Phase 1: collect indices of FP-compare + bc1 instructions
    fp_cmp_indices = []
    bc1_indices = []
    for i in range(instr_count):
        instr = struct.unpack(">I", data[i*4:i*4+4])[0]
        if is_fp_compare(instr):
            fp_cmp_indices.append((i, instr))
        elif is_bc1(instr):
            bc1_indices.append((i, instr))

    print(f"FP compare instructions: {len(fp_cmp_indices)}")
    print(f"BC1 (branch on FP cond): {len(bc1_indices)}")
    print()

    # Phase 2: find FP-compare within 16 instructions of a BC1 (likely distance test)
    # Window of 16 instructions is typical for an in-function branch to a near label.
    # Window of 32 captures the geometry-LOD pattern where the FP load + sub + compare
    # are followed by a bc1 that's further out (bc1f + ~8 to +20 instructions).
    WINDOW = 32
    candidates = []
    for cmp_idx, cmp_instr in fp_cmp_indices:
        for bc_idx, bc_instr in bc1_indices:
            if 0 < (bc_idx - cmp_idx) <= WINDOW:
                candidates.append((cmp_idx, cmp_instr, bc_idx, bc_instr))
                break

    print(f"FP compare -> BC1 within {WINDOW} instructions: {len(candidates)}")
    print()

    # Phase 3: enrich each candidate with function context
    # FILTER: prefer candidates in the state-8 demo-race rendering hot path
    # (where car actors are updated per-frame, where LOD swapping would happen).
    state8_keywords = ("func_80060464", "func_80060DE4", "func_8005E86C",
                       "func_8005F12C", "func_8005F42C", "func_80060464",
                       "func_80062854", "func_80064980", "func_800657CC",
                       "func_80063AE8", "func_80064DE0", "func_80067770")
    print(f"{'ROM offset':>10}  {'VRAM':>10}  {'FP compare':>14}  {'BC1':>10}  Function")
    print("-" * 100)
    seen = set()
    for cmp_idx, cmp_instr, bc_idx, bc_instr in sorted(candidates):
        rom_offset = cmp_idx * 4
        vram = rom_offset + 0x7FFFF400
        ctx = find_function_at(syms, vram)
        if ctx is None:
            continue
        fname, fvram, fsize = ctx
        if fname in seen:
            continue
        seen.add(fname)
        # Condition code is split across bits 20-18 (3 bits) and bits 10-8 (3 bits) — combined.
        cond = ((cmp_instr >> 18) & 0x7) << 3 | ((cmp_instr >> 8) & 0x7)
        cond_name = {0xC: "LT", 0xE: "LE", 0x2: "EQ", 0x4: "OLT", 0x6: "OLE",
                     0x10: "LT", 0x12: "EQ"}.get(cond, f"cond{cond}")
        bc1_taken = (bc_instr >> 16) & 1
        dist = bc_idx - cmp_idx
        fs = (cmp_instr >> 11) & 0x1F
        ft = (cmp_instr >> 16) & 0x1F
        marker = "*" if fname in state8_keywords else " "
        print(f"{marker} {rom_offset:#10x}  {vram:#10x}  c.{cond_name}.s f{fs},f{ft:<2}  "
              f"bc1{'t' if bc1_taken else 'f'}+{dist:<3}  {fname}")
        # Show context for * markers (likely candidates)
        if fname in state8_keywords:
            print(disassemble_at(data, cmp_idx))
            print()
    print()
    print(f"Total distinct functions with distance-test candidates: {len(seen)}")
    print()
    print("VERIFY before patching: disassemble each hit and confirm it gates")
    print("texture/geometry selection. The hits here are HYPOTHESES, not proof.")

    return 0

def disassemble_at(data, idx, before=4, after=4):
    """Return a printable slice of instructions around the hit, with a marker."""
    out = []
    start = max(0, idx - before)
    end = min(len(data) // 4, idx + after + 1)
    for i in range(start, end):
        instr = struct.unpack(">I", data[i*4:i*4+4])[0]
        marker = " <--" if i == idx else "    "
        out.append(f"  {i*4:06x} {instr:08x}{marker}")
    return "\n".join(out)

def fname_short(instr):
    """Extract Fs/Ft from a COP1 compare instruction."""
    ft = (instr >> 16) & 0x1F
    fs = (instr >> 11) & 0x1F
    return f"${fs},${ft}"

if __name__ == "__main__":
    sys.exit(main())