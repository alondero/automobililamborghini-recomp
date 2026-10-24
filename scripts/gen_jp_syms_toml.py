#!/usr/bin/env python3
"""Generate the Japanese cartridge's recompile inputs from the USA symbol table.

The Japanese release (Super Speed Race 64, cartridge NLCJ) has a different link
order, a different placement of libultra (Nintendo's N64 system library) and one
extra code overlay. Most functions are the same instruction sequence at a different
address. This script carries the USA function boundaries and names across to the
Japanese ROM wherever the instructions match, discovers the remaining Japanese
functions from the code itself, and writes:

  * lamborghini.jp.syms.toml - the Japanese symbol table (two code sections);
  * lamborghini.jp.toml      - the Japanese N64Recomp configuration.

The naming rules, the section evidence and the current counts are in
docs/japanese-rom.md. The printed report is the source of those counts.

Usage (from the repository root, with both ROMs present):
    python scripts/gen_jp_syms_toml.py           # write
    python scripts/gen_jp_syms_toml.py --check   # compare with the checked-in files
"""
from __future__ import annotations

import argparse
import collections
import difflib
import hashlib
import struct
import sys
import tomllib
from dataclasses import dataclass
from pathlib import Path
from typing import NamedTuple

REPO = Path(__file__).resolve().parent.parent
US_ROM = REPO / "Automobili Lamborghini (USA).z64"
JP_ROM = REPO / "Super Speed Race 64 (Japan).z64"
US_SYMS = REPO / "lamborghini.syms.toml"
US_CONFIG = REPO / "lamborghini.us.toml"
OUT_SYMS = REPO / "lamborghini.jp.syms.toml"
OUT_CONFIG = REPO / "lamborghini.jp.toml"

US_SHA1 = "78b36b48040426478011caef5e11884af2e80375"
JP_SHA1 = "98009e4948faf885864e52e1268700a0144c1b2e"

# The generated configuration records this digest of the USA inputs, so a test
# without ROMs can tell that the Japanese tables predate a USA symbol or patch change.
USA_INPUTS_PREFIX = "# Derived from USA inputs with SHA-1 "


class Func(NamedTuple):
    name: str
    vram: int
    size: int


@dataclass(frozen=True)
class Section:
    """A contiguous code range: ROM [rom, rom + size) runs at VRAM [vram, vram + size)."""
    name: str
    rom: int
    vram: int
    size: int
    # The image this code belongs to, for reading rodata such as jump tables.
    data_end_rom: int

    def contains_vram(self, vram: int) -> bool:
        return self.vram <= vram < self.vram + self.size

    def rom_of(self, vram: int) -> int:
        return vram - self.vram + self.rom

    def image_rom_of(self, vram: int) -> int | None:
        rom = self.rom_of(vram)
        return rom if self.rom <= rom < self.data_end_rom else None


# USA code, as in lamborghini.syms.toml. No USA rodata is read, so its image ends with
# the code.
US_TEXT = Section(".text", 0x1000, 0x80000400, 0x86690, 0x87690)
# Main image: the boot code (IPL3) copies ROM 0x1000 onward to 0x80000400. Code ends
# where the RSP boot microcode begins (the word 0x09000419 at ROM 0x7BB80, the same
# marker as USA ROM 0x87690). Its data runs to the zero-filled (BSS) range that boot
# starts clearing at 0x80084110.
JP_MAIN = Section(".text", 0x1000, 0x80000400, 0x7AB80, 0x84D10)
# Boot copies ROM [0xFA1F0, 0x151840) to 0x801F9800 through func_80009B60 (JP vram
# 0x80000734..0x8000075C, repeated before several game states). Code ends after the
# last `jr $ra` at ROM 0x12878C; the rest is the overlay's data.
JP_OVERLAY = Section(".ovl_801F9800", 0xFA1F0, 0x801F9800, 0x2E5B0, 0x151840)
JP_SECTIONS = (JP_MAIN, JP_OVERLAY)

NOP = 0x00000000
JR_RA = 0x03E00008
OP_J, OP_JAL, OP_SLTIU, OP_LUI, OP_LW = 0x02, 0x03, 0x0B, 0x0F, 0x23
REG_AT = 1


def word(rom: bytes, off: int) -> int:
    return struct.unpack_from(">I", rom, off)[0]


def sha1(path: Path) -> str:
    return hashlib.sha1(path.read_bytes()).hexdigest()


def usa_inputs_digest() -> str:
    h = hashlib.sha1()
    for path in (US_SYMS, US_CONFIG):
        h.update(path.read_bytes().replace(b"\r\n", b"\n"))
    return h.hexdigest()


def opcode(w: int) -> int:
    return w >> 26


def rs(w: int) -> int:
    return (w >> 21) & 31


def rt(w: int) -> int:
    return (w >> 16) & 31


def jump_target(w: int, pc: int) -> int:
    """Target of a `j` or `jal` at address `pc`. The top four bits come from `pc`, so a
    jump's region is never assumed."""
    return (pc & 0xF0000000) | ((w & 0x03FFFFFF) << 2)


# ---------------------------------------------------------------------------
# Instruction masking
# ---------------------------------------------------------------------------

_IMM_ALU = {0x08, 0x09, 0x0A, 0x0B, 0x0C, 0x0D, 0x0E}  # addi..xori (not lui)


def coarse_mask(w: int) -> int:
    """Keep opcode and register fields; drop every immediate that could be an address."""
    op = opcode(w)
    if op in (OP_J, OP_JAL):
        return w & 0xFC000000
    if op == OP_LUI or op in _IMM_ALU or op >= 0x20:
        return w & 0xFFFF0000
    return w


def reloc_mask(words: list[int]) -> list[int]:
    """Mask only fields a relocation can change.

    Registers loaded by `lui` are tracked linearly; an immediate that combines with one
    of them is an address half and is masked. The decision depends only on opcode and
    register fields, so it is applied identically to both regions' code.
    """
    hi_regs: set[int] = set()
    out = []
    for w in words:
        op = opcode(w)
        if op in (OP_J, OP_JAL):
            out.append(w & 0xFC000000)
            continue
        if op == OP_LUI:
            out.append(w & 0xFFFF0000)
            hi_regs.add(rt(w))
            continue
        if (op in _IMM_ALU or op >= 0x20) and rs(w) in hi_regs:
            out.append(w & 0xFFFF0000)
        else:
            out.append(w)
        # The IDO compiler indexes arrays as `lui at,%hi; addu at,at,idx;
        # lw r,%lo(at)`: the sum still carries the high half.
        if op == 0 and (w & 0x3F) in (0x20, 0x21) and (rs(w) in hi_regs or rt(w) in hi_regs):
            hi_regs.add((w >> 11) & 31)
            continue
        # Any other write to a tracked register ends its address role.
        dest = None
        if op == 0:
            dest = (w >> 11) & 31
        elif op in _IMM_ALU or 0x20 <= op <= 0x27:
            dest = rt(w)
        if dest is not None and not (op in _IMM_ALU and rs(w) == dest and dest in hi_regs):
            hi_regs.discard(dest)
    return out


def code_words(buf: bytes, rom: int, size: int, mask=lambda w: w) -> list[int]:
    return [mask(word(buf, rom + i)) for i in range(0, size, 4)]


# ---------------------------------------------------------------------------
# USA inputs
# ---------------------------------------------------------------------------

def load_us_functions() -> list[Func]:
    syms = tomllib.loads(US_SYMS.read_text(encoding="utf-8"))
    (section,) = syms["section"]
    if (section["rom"], section["vram"], section["size"]) != (US_TEXT.rom, US_TEXT.vram, US_TEXT.size):
        raise ValueError(f"{US_SYMS.name}: .text layout differs from US_TEXT")
    return [Func(f["name"], f["vram"], f["size"]) for f in section["functions"]]


def load_us_patches() -> dict:
    return tomllib.loads(US_CONFIG.read_text(encoding="utf-8"))["patches"]


# ---------------------------------------------------------------------------
# Matching USA functions into the Japanese ROM
# ---------------------------------------------------------------------------

def section_of(vram: int) -> Section | None:
    return next((s for s in JP_SECTIONS if s.contains_vram(vram)), None)


# Matching keys on the first KEY_WORDS masked words. A repeated or short body is placed
# from a matched neighbour up to NEIGHBOUR_WINDOW functions away, over at most
# NEIGHBOUR_ROUNDS passes (each pass can anchor the next).
KEY_WORDS = 4
NEIGHBOUR_WINDOW = 8
NEIGHBOUR_ROUNDS = 4


def match_functions(us: bytes, jp: bytes, us_funcs: list[Func]) -> dict[str, tuple[int, bool]]:
    """Return {us_name: (jp_vram, exact)} for USA functions found in the Japanese code."""
    jp_coarse: dict[int, int] = {}   # jp vram -> coarse-masked word
    for s in JP_SECTIONS:
        for i in range(0, s.size, 4):
            jp_coarse[s.vram + i] = coarse_mask(word(jp, s.rom + i))
    index: dict[tuple, list[int]] = collections.defaultdict(list)
    for s in JP_SECTIONS:
        for v in range(s.vram, s.vram + s.size - 4 * KEY_WORDS + 4, 4):
            index[tuple(jp_coarse[v + 4 * i] for i in range(KEY_WORDS))].append(v)

    def fits(vram, size):
        s = section_of(vram)
        return s is not None and s.contains_vram(vram + size - 4)

    def coarse_equal(vram, us_masked):
        return fits(vram, 4 * len(us_masked)) and all(
            jp_coarse[vram + 4 * i] == m for i, m in enumerate(us_masked))

    candidates: dict[str, list[int]] = {}
    us_coarse: dict[str, list[int]] = {}
    for f in us_funcs:
        masked = code_words(us, US_TEXT.rom_of(f.vram), f.size, coarse_mask)
        us_coarse[f.name] = masked
        if len(masked) < KEY_WORDS:
            candidates[f.name] = []      # resolved from neighbours only
            continue
        candidates[f.name] = [v for v in index.get(tuple(masked[:KEY_WORDS]), [])
                              if coarse_equal(v, masked)]

    result: dict[str, int] = {n: c[0] for n, c in candidates.items() if len(c) == 1}

    # A short body (often a libultra register accessor) is accepted when its raw
    # bytes occur exactly once in the Japanese code. A two-instruction body must
    # also land on a call target.
    jp_code = {s: jp[s.rom:s.rom + s.size] for s in JP_SECTIONS}
    calls = jal_targets(jp)
    for f in us_funcs:
        if f.name in result or f.size // 4 >= KEY_WORDS or f.size < 8:
            continue
        rom = US_TEXT.rom_of(f.vram)
        raw = us[rom:rom + f.size]
        hits = []
        for s, code in jp_code.items():
            i = code.find(raw)
            while i != -1 and len(hits) < 2:
                if i % 4 == 0:
                    hits.append(s.vram + i)
                i = code.find(raw, i + 1)
        if len(hits) == 1 and (f.size >= 12 or hits[0] in calls):
            result[f.name] = hits[0]

    for _ in range(NEIGHBOUR_ROUNDS):
        changed = False
        for i, f in enumerate(us_funcs):
            if f.name in result:
                continue
            nearby = [*range(i - 1, max(-1, i - NEIGHBOUR_WINDOW), -1),
                      *range(i + 1, min(len(us_funcs), i + NEIGHBOUR_WINDOW))]
            for j in nearby:
                nb = us_funcs[j]
                if nb.name not in result:
                    continue
                guess = f.vram + (result[nb.name] - nb.vram)
                if ((not candidates[f.name] or guess in candidates[f.name])
                        and coarse_equal(guess, us_coarse[f.name])):
                    result[f.name] = guess
                    changed = True
                    break
        if not changed:
            break

    # One Japanese function cannot carry two USA names. Keying on the address alone also
    # covers a short body that is a prefix of a longer one, which differs in size. The
    # first name in USA address order is kept.
    us_by_name = {f.name: f for f in us_funcs}
    by_addr: dict[int, list[str]] = collections.defaultdict(list)
    for name, v in sorted(result.items(), key=lambda item: us_by_name[item[0]].vram):
        by_addr[v].append(name)
    for names in by_addr.values():
        for extra in names[1:]:
            del result[extra]

    out = {}
    for name, v in result.items():
        f = us_by_name[name]
        exact = (reloc_mask(code_words(us, US_TEXT.rom_of(f.vram), f.size))
                 == reloc_mask(code_words(jp, section_of(v).rom_of(v), f.size)))
        out[name] = (v, exact)
    return out


# ---------------------------------------------------------------------------
# Discovering the remaining Japanese functions
# ---------------------------------------------------------------------------

def _sign16(w: int) -> int:
    v = w & 0xFFFF
    return v - 0x10000 if v & 0x8000 else v


def branch_target(w: int, pc: int) -> int | None:
    op = opcode(w)
    if op in (0x04, 0x05, 0x06, 0x07, 0x14, 0x15, 0x16, 0x17):
        return pc + 4 + (_sign16(w) << 2)
    if op == 0x01 and rt(w) in (0x00, 0x01, 0x02, 0x03, 0x10, 0x11, 0x12, 0x13):
        return pc + 4 + (_sign16(w) << 2)
    if op == 0x11 and rs(w) == 0x08:
        return pc + 4 + (_sign16(w) << 2)
    return None


# How far back from a `jr` to look for the jump-table load and its range check.
SWITCH_LOOKBACK = 0x40


def switch_entry_count(jp: bytes, sec: Section, start: int, pc: int) -> int | None:
    """Return N from the nearest range check `sltiu at,reg,N` before `pc`; the table has
    entries 0 to N-1. Without it the table's length is unknown."""
    v = pc - 4
    while v >= max(start, pc - SWITCH_LOOKBACK):
        w = word(jp, sec.rom_of(v))
        if opcode(w) == OP_SLTIU and rt(w) == REG_AT:
            return w & 0xFFFF
        v -= 4
    return None


def jump_table_targets(jp: bytes, sec: Section, start: int, pc: int, end_limit: int) -> list[int]:
    """Resolve an IDO switch: `sltiu at,r,N; ...; lui at,%hi(T); addu at,at,r;
    lw r,%lo(T)(at); jr r`. A jump with no range check is not a switch the scan can
    size, so it contributes no targets rather than reading whatever follows the table."""
    reg = rs(word(jp, sec.rom_of(pc)))
    lo = hi = None
    base = None
    v = pc - 4
    while v >= max(start, pc - SWITCH_LOOKBACK):
        w = word(jp, sec.rom_of(v))
        if lo is None and opcode(w) == OP_LW and rt(w) == reg:
            lo = _sign16(w)
            base = rs(w)
        elif lo is not None and opcode(w) == OP_LUI and rt(w) == base:
            hi = w & 0xFFFF
            break
        v -= 4
    if lo is None or hi is None:
        return []
    count = switch_entry_count(jp, sec, start, pc)
    if count is None:
        return []
    table = ((hi << 16) + lo) & 0xFFFFFFFF
    rom = sec.image_rom_of(table)
    out = []
    while rom is not None and rom + 4 <= sec.data_end_rom and len(out) < count:
        t = word(jp, rom)
        if not (start <= t < end_limit) or t % 4:
            break
        out.append(t)
        rom += 4
    return out


def sweep_end(jp: bytes, sec: Section, start: int, region_end: int) -> int:
    """Return the end of the function at `start`: the first return or exit jump that no
    earlier branch, jump or switch case reaches past."""
    furthest = start
    pc = start
    while pc < region_end:
        w = word(jp, sec.rom_of(pc))
        t = branch_target(w, pc)
        if t is not None and start <= t < region_end:
            furthest = max(furthest, t)
        if opcode(w) == OP_J:
            t = jump_target(w, pc)
            if start <= t < region_end:
                furthest = max(furthest, t)
            elif pc + 8 > furthest:
                return pc + 8
        if opcode(w) == 0 and (w & 0x3F) == 0x08:
            if w == JR_RA:
                if pc + 8 > furthest:
                    return pc + 8
            else:
                for t in jump_table_targets(jp, sec, start, pc, region_end):
                    furthest = max(furthest, t)
        pc += 4
    return region_end


def skip_padding(jp: bytes, sec: Section, v: int, end: int) -> int:
    while v < end and word(jp, sec.rom_of(v)) == NOP:
        v += 4
    return v


def follows_exit(jp: bytes, sec: Section, v: int, floor: int) -> bool:
    """True when `v` follows a return or an unconditional jump (plus padding)."""
    after_exit = v
    while after_exit - 4 > floor and word(jp, sec.rom_of(after_exit - 4)) == NOP:
        after_exit -= 4
    # The exit is the last instruction or the one before its delay slot.
    for pc in (after_exit - 8, after_exit - 4):
        if pc < floor:
            continue
        w = word(jp, sec.rom_of(pc))
        if w == JR_RA or opcode(w) == OP_J or (w >> 16) == 0x1000:   # jr ra, j, b
            return True
    return False


def jal_targets(jp: bytes) -> set[int]:
    out = set()
    for s in JP_SECTIONS:
        for v in range(s.vram, s.vram + s.size, 4):
            w = word(jp, s.rom_of(v))
            if opcode(w) == OP_JAL and section_of(jump_target(w, v)):
                out.add(jump_target(w, v))
    return out


def discover(jp: bytes, known: list[Func]) -> list[Func]:
    """Fill every code range not covered by a matched function."""
    calls = jal_targets(jp)
    known_starts = {f.vram for f in known}
    found: list[Func] = []

    def add(vram, end):
        found.append(Func(f"jp_func_{vram:08X}", vram, end - vram))

    for sec in JP_SECTIONS:
        end = sec.vram + sec.size
        covered = sorted((f.vram, f.vram + f.size) for f in known if sec.contains_vram(f.vram))
        gaps = []
        cur = sec.vram
        for a, b in covered:
            if a > cur:
                gaps.append((cur, a))
            cur = max(cur, b)
        if cur < end:
            gaps.append((cur, end))
        body_end = sec.vram   # end of the last body that ran past a matched function
        for gap_start, gap_end in gaps:
            calls_here = sorted(t for t in calls if gap_start <= t < gap_end and t not in known_starts)
            if gap_start < body_end:
                # The gap is the rest of a body that ran through the matched function
                # before it; its call targets are further alternate entries.
                for f in calls_here:
                    if f < body_end:
                        add(f, body_end)
                if gap_end <= body_end:
                    continue
                gap_start = body_end
            forced = sorted({t for t in calls_here if t >= gap_start}
                            | {skip_padding(jp, sec, gap_start, gap_end)})
            forced = [f for f in forced if f < gap_end and f not in known_starts]
            v = forced[0] if forced else gap_end
            while v < gap_end:
                fn_end = sweep_end(jp, sec, v, gap_end)
                # A call target that follows an exit starts a new function; the sweep
                # ran past it because the body before it never returns.
                cut = next((f for f in forced if v < f < fn_end and follows_exit(jp, sec, f, v)), None)
                if cut is not None:
                    fn_end = cut
                elif fn_end == gap_end and gap_end < end and not follows_exit(jp, sec, gap_end, v):
                    # The body falls through into the next matched function, which is
                    # then an alternate entry sharing this body's epilogue (the USA
                    # BootLoadInitialAssets pattern). Size the body to its real end.
                    fn_end = sweep_end(jp, sec, v, end)
                    body_end = fn_end
                add(v, fn_end)
                # Any other call target inside the body is an alternate entry that
                # shares the epilogue.
                for f in forced:
                    if v < f < min(fn_end, gap_end):
                        add(f, fn_end)
                v = skip_padding(jp, sec, min(fn_end, gap_end), gap_end)
    return found


# A changed function keeps its USA name when it corresponds to exactly one discovered
# Japanese function. Two signals are used, both independent of addresses:
#   * sequence similarity of the masked instructions, with branch displacements also
#     masked because any inserted code shifts them;
#   * agreement of the named functions each body calls (Jaccard index), which survives
#     register-allocation and scheduling changes.
# A pair is accepted at FUZZY_STRONG similarity with FUZZY_MARGIN over the runner-up,
# or at FUZZY_WEAK similarity when it is FUZZY_RATIO times the runner-up and the callee
# sets agree at CALLEE_MIN. Only Japanese bodies within FUZZY_SIZE_BAND of the USA size
# compete. Size and boundaries stay those discovered in the JP code.
FUZZY_STRONG = 0.75
FUZZY_MARGIN = 0.10
FUZZY_WEAK = 0.20
FUZZY_RATIO = 2.0
CALLEE_MIN = 0.5
FUZZY_ROUNDS = 3
FUZZY_MIN_SIZE = 16
FUZZY_SIZE_BAND = (0.6, 1.6)


def fuzzy_mask(w: int) -> int:
    m = coarse_mask(w)
    if branch_target(w, 0) is not None:
        m &= 0xFFFF0000
    return m


def callees(buf: bytes, rom: int, vram: int, size: int) -> set[int]:
    """Targets of the `jal` calls in a body that starts at `vram` in the code."""
    out = set()
    for i in range(0, size, 4):
        w = word(buf, rom + i)
        if opcode(w) == OP_JAL:
            out.add(jump_target(w, vram + i))
    return out


def fuzzy_names(us: bytes, jp: bytes, us_funcs: list[Func], matched: dict,
                found: list[Func]) -> dict[int, str]:
    """Return {jp_vram: us_name} for changed functions that still clearly correspond."""
    us_name_at = {f.vram: f.name for f in us_funcs}
    jp_name_at = {v: n for n, (v, _) in matched.items()}
    us_bodies = {}
    for f in us_funcs:
        if f.name in matched or f.size < FUZZY_MIN_SIZE:
            continue
        rom = US_TEXT.rom_of(f.vram)
        us_bodies[f.name] = (f.size, code_words(us, rom, f.size, fuzzy_mask),
                             callees(us, rom, f.vram, f.size))
    jp_bodies = []
    for f in found:
        rom = section_of(f.vram).rom_of(f.vram)
        jp_bodies.append((f.vram, f.size, code_words(jp, rom, f.size, fuzzy_mask),
                          callees(jp, rom, f.vram, f.size)))

    low, high = FUZZY_SIZE_BAND
    out: dict[int, str] = {}
    for _ in range(FUZZY_ROUNDS):
        taken = set(out.values())
        scores: list[tuple[float, str, int]] = []
        for name, (size, us_masked, us_calls) in us_bodies.items():
            if name in taken:
                continue
            us_named = {us_name_at[t] for t in us_calls if t in us_name_at}
            ranked = []
            for v, s, jp_masked, jp_calls in jp_bodies:
                if v in out or not (low * size <= s <= high * size):
                    continue
                sm = difflib.SequenceMatcher(None, us_masked, jp_masked, autojunk=False)
                if sm.real_quick_ratio() < FUZZY_WEAK or sm.quick_ratio() < FUZZY_WEAK:
                    continue
                ranked.append((sm.ratio(), v, jp_calls))
            if not ranked:
                continue
            ranked.sort(key=lambda r: r[0], reverse=True)
            best, v, jp_calls = ranked[0]
            second = ranked[1][0] if len(ranked) > 1 else 0.0
            if best >= FUZZY_STRONG and best - second >= FUZZY_MARGIN:
                scores.append((best, name, v))
                continue
            jp_named = {jp_name_at.get(t, out.get(t)) for t in jp_calls} - {None}
            union = us_named | jp_named
            agree = len(us_named & jp_named) / len(union) if union else 0.0
            if best >= FUZZY_WEAK and best >= FUZZY_RATIO * second and agree >= CALLEE_MIN:
                scores.append((best, name, v))
        added = False
        for score, name, v in sorted(scores, reverse=True):
            if v not in out and name not in out.values():
                out[v] = name
                added = True
        if not added:
            break
    return out


# Assumption, not measured: link order inside a module is unchanged, so a run of
# unnamed USA functions between two named neighbours corresponds to an equally long
# run of unnamed Japanese functions between the same neighbours. Each pair must still
# reach SLOT_MIN similarity, which rejects a run where the Japanese build inserted or
# dropped a function.
SLOT_MIN = 0.15


def top_level(funcs: list[Func]) -> list[Func]:
    """Functions in address order, without alternate entries inside another body."""
    out = []
    end = 0
    for f in sorted(funcs, key=lambda f: (f.vram, -f.size)):
        if f.vram >= end:
            out.append(f)
            end = f.vram + f.size
    return out


def slot_names(us: bytes, jp: bytes, us_funcs: list[Func], jp_funcs: list[Func]) -> dict[int, str]:
    """Return {jp_vram: us_name} for unnamed functions that fill a USA function's slot."""
    jp_named = {f.name: f.vram for f in jp_funcs if not f.name.startswith("jp_func_")}
    us_seq = top_level(us_funcs)
    jp_seq = top_level(jp_funcs)
    jp_index = {f.vram: i for i, f in enumerate(jp_seq)}

    def similarity(u: Func, j: Func) -> float:
        us_masked = code_words(us, US_TEXT.rom_of(u.vram), u.size, fuzzy_mask)
        jp_masked = code_words(jp, section_of(j.vram).rom_of(j.vram), j.size, fuzzy_mask)
        return difflib.SequenceMatcher(None, us_masked, jp_masked, autojunk=False).ratio()

    out: dict[int, str] = {}
    i = 0
    while i < len(us_seq):
        if us_seq[i].name in jp_named:
            i += 1
            continue
        k = i
        while k < len(us_seq) and us_seq[k].name not in jp_named:
            k += 1
        run = us_seq[i:k]
        i = k
        if i - len(run) == 0 or k == len(us_seq):
            continue
        prev_v = jp_named[us_seq[i - len(run) - 1].name]
        next_v = jp_named[us_seq[k].name]
        if prev_v not in jp_index or next_v not in jp_index:
            continue
        a, b = jp_index[prev_v], jp_index[next_v]
        between = jp_seq[a + 1:b]
        if b <= a or len(between) != len(run) or any(not f.name.startswith("jp_func_") for f in between):
            continue
        if section_of(prev_v) is not section_of(next_v):
            continue
        if all(similarity(u, j) >= SLOT_MIN for u, j in zip(run, between)):
            for u, j in zip(run, between):
                out[j.vram] = u.name
    return out


# ---------------------------------------------------------------------------
# Output
# ---------------------------------------------------------------------------

def toml_array(names: list[str]) -> str:
    if not names:
        return "[]"
    return "[\n" + ",\n".join(f'    "{n}"' for n in names) + ",\n]"


def alternate_entries(funcs: list[Func]) -> set[str]:
    """USA names that start inside another USA function's body."""
    spans = sorted((f.vram, f.vram + f.size, f.name) for f in funcs)
    return {f.name for f in funcs if any(a < f.vram < b for a, b, n in spans if n != f.name)}


def generate(us: bytes, jp: bytes) -> tuple[str, str, list[str]]:
    """Return (symbol table text, config text, report lines)."""
    us_funcs = load_us_functions()
    matched = match_functions(us, jp, us_funcs)
    sizes = {f.name: f.size for f in us_funcs}
    known = [Func(n, v, sizes[n]) for n, (v, _) in matched.items()]
    found = discover(jp, known)
    renamed = fuzzy_names(us, jp, us_funcs, matched, found)
    found = [f._replace(name=renamed.get(f.vram, f.name)) for f in found]
    slotted: dict[int, str] = {}
    while True:
        more = slot_names(us, jp, us_funcs, known + found)
        more = {v: n for v, n in more.items() if n not in slotted.values()}
        if not more:
            break
        slotted.update(more)
        found = [f._replace(name=more.get(f.vram, f.name)) for f in found]
    funcs = sorted(known + found, key=lambda f: (f.vram, -f.size, f.name))

    names = [f.name for f in funcs]
    dupes = [n for n, c in collections.Counter(names).items() if c > 1]
    if dupes:
        raise ValueError(f"duplicate names {dupes[:5]}")
    if not any(f.vram == JP_MAIN.vram for f in funcs):
        raise ValueError("no function at the entrypoint")

    present = set(names)
    us_patches = load_us_patches()
    us_stubs, us_ignored = us_patches["stubs"], us_patches["ignored"]
    stubs = [n for n in us_stubs if n in present]
    ignored = [n for n in us_ignored if n in present and n not in stubs]

    lines = [
        "# Autogenerated by scripts/gen_jp_syms_toml.py -- do not edit by hand.",
        "# Runtime-addressed symbols for the Japanese cartridge (Super Speed Race 64, NLCJ).",
        "# Names matched to the USA symbol table keep the USA name; the rest are jp_func_<vram>.",
        "# Layout and evidence: docs/japanese-rom.md.",
    ]
    for sec in JP_SECTIONS:
        lines += [
            "",
            "[[section]]",
            f'name = "{sec.name}"',
            f"rom = {sec.rom:#x}",
            f"vram = {sec.vram:#010x}",
            f"size = {sec.size:#x}",
            "",
            "functions = [",
        ]
        for f in funcs:
            if sec.contains_vram(f.vram):
                lines.append(f'    {{ name = "{f.name}", vram = {f.vram:#010x}, size = {f.size:#x} }},')
        lines.append("]")
    syms_text = "\n".join(lines) + "\n"

    cfg_text = f'''# Autogenerated by scripts/gen_jp_syms_toml.py -- do not edit by hand.
# N64Recomp configuration for the Japanese cartridge (Super Speed Race 64, NLCJ).
# stubs and ignored carry the USA lists across by matched name. Gameplay and
# platform hooks are not carried yet. Evidence: docs/japanese-rom.md.
{USA_INPUTS_PREFIX}{usa_inputs_digest()}.
#
# Run from the repository root:
#   ./build/lib/N64ModernRuntime/librecomp/N64Recomp/N64Recomp lamborghini.jp.toml

[input]
entrypoint = {JP_MAIN.vram:#010x}
output_func_path = "RecompiledFuncsJP"
symbols_file_path = "lamborghini.jp.syms.toml"
rom_file_path = "{JP_ROM.name}"

[patches]
stubs = {toml_array(stubs)}
ignored = {toml_array(ignored)}
'''

    alternates = alternate_entries(us_funcs)
    inferred = set(renamed.values()) | set(slotted.values())
    hooked = sorted({e["func"] for kind in ("hook", "instruction") for e in us_patches.get(kind, [])})

    def unnamed(label, us_names):
        missing = [n for n in us_names if n not in present]
        if not missing:
            return []
        others = [n for n in missing if n not in alternates]
        line = (f"{label} with no JP name: {len(missing)}, of which "
                f"{len(missing) - len(others)} are USA alternate entries")
        return [line + (f"; others: {', '.join(others)}" if others else "")]

    exact = sum(1 for _, e in matched.values() if e)
    report = [
        f"USA functions:            {len(us_funcs)}",
        f"matched into JP:          {len(matched)} ({exact} differ only by relocation)",
        f"discovered JP functions:  {len(found)} ({len(renamed)} named by alignment, "
        f"{len(slotted)} by link-order slot)",
        f"total JP functions:       {len(funcs)}",
        f"stubs carried:            {len(stubs)} / {len(us_stubs)}",
        f"ignored carried:          {len(ignored)} / {len(us_ignored)}",
        f"carried by an inferred name: {', '.join(n for n in stubs + ignored if n in inferred) or 'none'}",
        *unnamed("USA stubs", us_stubs),
        *unnamed("USA ignored", us_ignored),
        f"USA hooked or patched functions with no JP name: "
        f"{sum(n not in present for n in hooked)} of {len(hooked)}: "
        f"{', '.join(n for n in hooked if n not in present)}",
    ]
    return syms_text, cfg_text, report


def check_rom(path: Path, digest: str) -> str | None:
    """Return why `path` is not the expected dump, or None when it is."""
    if not path.is_file():
        return f"missing {path.name}; see docs/japanese-rom.md"
    actual = sha1(path)
    if actual != digest:
        return f"{path.name} SHA-1 is {actual}, expected {digest}"
    return None


def main(argv: list[str]) -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    parser.add_argument("--check", action="store_true",
                        help="compare with the checked-in files instead of writing them")
    args = parser.parse_args(argv)
    for path, digest in ((US_ROM, US_SHA1), (JP_ROM, JP_SHA1)):
        problem = check_rom(path, digest)
        if problem:
            print(f"ERROR: {problem}", file=sys.stderr)
            return 2
        print(f"{path.name}: SHA-1 {digest}")
    syms_text, cfg_text, report = generate(US_ROM.read_bytes(), JP_ROM.read_bytes())
    print("\n".join(report))
    outputs = ((OUT_SYMS, syms_text), (OUT_CONFIG, cfg_text))
    if args.check:
        stale = [p.name for p, text in outputs
                 if not p.exists() or p.read_text(encoding="utf-8") != text]
        if stale:
            print(f"ERROR: stale {', '.join(stale)}; run scripts/gen_jp_syms_toml.py", file=sys.stderr)
            return 1
        print("checked-in Japanese recompile inputs are current")
        return 0
    for path, text in outputs:
        path.write_text(text, encoding="utf-8", newline="\n")
    print(f"wrote {OUT_SYMS.name}, {OUT_CONFIG.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
