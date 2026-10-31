#!/usr/bin/env python3
"""Inventory literal guest addresses and derive Japanese relocation candidates.

This is evidence for issue 338, not a runtime translation table. A matching
instruction can identify an address expression, but cannot prove that a whole
structure, its indices, or its lifetime stayed the same in the other region.
No ROM bytes or instruction words are written to the report.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import sys

import gen_jp_syms_toml as gen

ROOT = gen.REPO
OUTPUT = ROOT / "docs/japanese-address-audit.json"
# Preserve line positions while ignoring comments, strings and character literals.
TOKENS = re.compile(
    r'//(?:\\\r?\n|[^\n])*|/\*[\s\S]*?\*/|R"([^ ()\\\t\r\n]{0,16})\([\s\S]*?\)\1"'
    r'|"(?:\\[\s\S]|[^"\\])*"|\'(?:\\[\s\S]|[^\'\\])*\'')
HEX = re.compile(r"\b0[xX][0-9a-fA-F]+\b|\b0[xX][0-9a-fA-F]+(?=[uUlL]+\b)")


def source_files():
    return sorted(p for p in (ROOT / "src").rglob("*")
                  if p.suffix in {".c", ".cpp", ".h", ".hpp"} and p.name != "aspMain.cpp")


def input_digest():
    """Allows a ROM-free check to reject a report made against older inputs."""
    paths = [Path(__file__), Path(gen.__file__), gen.US_SYMS]
    digest = hashlib.sha256()
    for p in sorted(set(paths)):
        digest.update(p.relative_to(ROOT).as_posix().encode() + b"\0")
        digest.update(p.read_bytes().replace(b"\r\n", b"\n") + b"\0")
    digest.update(json.dumps(inventory(), sort_keys=True).encode())
    return digest.hexdigest()


def literal_addresses(text):
    code = TOKENS.sub(lambda m: re.sub(r"[^\n]", " ", m.group()), text)
    for line, content in enumerate(code.splitlines(), 1):
        for match in HEX.finditer(content):
            value = int(match.group(), 16)
            if 0x80000000 <= value < 0x82000000:
                yield line, value


def inventory():
    uses = defaultdict(lambda: defaultdict(int))
    for path in source_files():
        for _, value in literal_addresses(path.read_text(encoding="utf-8")):
            uses[value][path.relative_to(ROOT).as_posix()] += 1
    return {value: dict(sorted(sites.items())) for value, sites in sorted(uses.items())}


def transfer(w):
    op = gen.opcode(w)
    return (op in {1, 2, 3, 4, 5, 6, 7, 20, 21, 22, 23} or
            (op == 0 and (w & 63) in {8, 9}) or
            (op in {16, 17, 18} and gen.rs(w) == 8))


def references(words, start):
    """Yield (instruction index, base address, indexed) within basic blocks.

    Track only LUI, ADDIU/ORI and ADDU with an unknown index. Do not follow
    loads, calls, branches or joins. An indexed result is the constant part
    of an address expression, not the effective address of any particular
    access. Even unindexed results remain candidates for human review.
    """
    # An indirect non-return jump can enter a case label whose predecessors
    # are invisible to this local scan. Do not carry fallthrough provenance
    # into such a label; omit the whole function until its CFG is resolved.
    if any(gen.opcode(w) == 0 and (w & 63) == 8 and gen.rs(w) != 31 for w in words):
        return
    targets = {gen.branch_target(w, start + i * 4) for i, w in enumerate(words)}
    targets.update(start + i * 4 + 4 + gen._sign16(w) * 4
                   for i, w in enumerate(words)
                   if gen.opcode(w) in {16, 17, 18} and gen.rs(w) == 8)
    targets.update(gen.jump_target(w, start + i * 4) for i, w in enumerate(words)
                   if gen.opcode(w) == gen.OP_J)
    regs = {}
    delay = False
    for i, w in enumerate(words):
        if start + i * 4 in targets:
            regs.clear()
        op, rs, rt, rd, fn = gen.opcode(w), gen.rs(w), gen.rt(w), (w >> 11) & 31, w & 63
        was_delay = delay
        delay = transfer(w)
        base = regs.get(rs)
        if op == 15:  # lui
            regs[rt] = ((w & 0xFFFF) << 16, False)
        elif op in {9, 13} and base is not None:  # addiu / ori
            value, indexed = base
            # OR after an unknown index is not a relocatable address sum.
            if op == 13 and indexed:
                regs.pop(rt, None)
            else:
                value = ((value + gen._sign16(w)) if op == 9 else (value | (w & 0xFFFF))) & 0xFFFFFFFF
                regs[rt] = (value, indexed)
                if 0x80000000 <= value < 0x82000000:
                    yield i, value, indexed
        elif op in {32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46,
                    48, 49, 52, 53, 55, 56, 57, 60, 61, 63}:
            if base is not None:
                value = (base[0] + gen._sign16(w)) & 0xFFFFFFFF
                if 0x80000000 <= value < 0x82000000:
                    yield i, value, base[1]
            # Integer loads and store-conditionals overwrite rt; FP loads don't.
            if op in {32, 33, 34, 35, 36, 37, 38, 39, 48, 52, 55, 56, 60}:
                regs.pop(rt, None)
        elif op == 0 and fn == 33:  # addu / move
            left, right = regs.get(rs), regs.get(rt)
            regs.pop(rd, None)
            if rs == 0 and right is not None:
                regs[rd] = right
            elif rt == 0 and left is not None:
                regs[rd] = left
            elif (left is None) != (right is None):
                regs[rd] = ((left or right)[0], True)
        elif op == 0:
            # Only these SPECIAL instructions write rd. Do not invent a write
            # for mult/div or mtlo/mthi, whose rd encoding is reserved.
            if fn in {0, 2, 3, 4, 6, 7, 9, 10, 11, 16, 18, 20, 22, 23,
                      32, 34, 35, 36, 37, 38, 39, 42, 43, 44, 45, 46, 47,
                      56, 58, 59, 60, 62, 63}:
                regs.pop(rd, None)
            elif fn not in {8, 17, 19, 24, 25, 26, 27, 28, 29, 30, 31}:
                regs.clear()
        elif op in {8, 9, 10, 11, 12, 13, 14, 24, 25, 26, 27}:
            regs.pop(rt, None)
        elif op in {16, 17, 18} and rs in {0, 1, 2}:
            regs.pop(rt, None)  # mfc / dmfc / cfc
        elif not transfer(w):
            # Unknown opcode: prefer an omitted candidate to stale provenance.
            regs.clear()
        if op == 3 or (op == 1 and rt in {16, 17, 18, 19}):
            regs.pop(31, None)
        regs.pop(0, None)
        if was_delay:
            regs.clear()


def address_candidates(us, jp, us_funcs, matched):
    result = defaultdict(lambda: defaultdict(set))
    for f in us_funcs:
        pair = matched.get(f.name)
        if pair is None or not pair[1]:
            continue
        jp_start = pair[0]
        section = gen.section_of(jp_start)
        us_words = gen.code_words(us, gen.US_TEXT.rom_of(f.vram), f.size)
        jp_words = gen.code_words(jp, section.rom_of(jp_start), f.size)
        jp_refs = {i: (addr, indexed) for i, addr, indexed in references(jp_words, jp_start)}
        for i, addr, indexed in references(us_words, f.vram):
            other = jp_refs.get(i)
            if other is not None and other[1] == indexed:
                result[addr][other[0]].add((f.name, f.vram + i * 4, jp_start + i * 4, indexed))
    return result


def generate(us, jp):
    funcs = gen.load_us_functions()
    matched = gen.match_functions(us, jp, funcs)
    candidates = address_candidates(us, jp, funcs, matched)
    records = []
    for address, uses in inventory().items():
        choices = []
        for target, witnesses in sorted(candidates.get(address, {}).items()):
            # Keep every competing target, with a bounded sample of its evidence.
            choices.append({"japan": f"0x{target:08X}", "witness_count": len(witnesses), "witnesses": [
                {"function": name, "usa_pc": f"0x{usa_pc:08X}",
                 "japan_pc": f"0x{jp_pc:08X}", "indexed": indexed}
                for name, usa_pc, jp_pc, indexed in sorted(witnesses)[:3]]})
        records.append({"usa": f"0x{address:08X}", "uses": uses,
                        "status": "unresolved" if not choices else "candidate" if len(choices) == 1 else "conflict",
                        "candidates": choices})
    return {"schema": 1, "input_sha256": input_digest(),
            "rom_sha1": {"usa": gen.US_SHA1, "japan": gen.JP_SHA1},
            "scope": "Explicit 0x80000000..0x81FFFFFF literals in handwritten src C/C++; includes masks, code and port allocations. Excludes comments, strings and computed/split addresses. Candidates are not approved runtime mappings.",
            "addresses": records}


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    checks = parser.add_mutually_exclusive_group()
    checks.add_argument("--check", action="store_true", help="compare the complete report using both ROMs")
    checks.add_argument("--check-inputs", action="store_true", help="check report freshness without ROMs")
    args = parser.parse_args(argv)
    if args.check_inputs:
        if not OUTPUT.exists() or json.loads(OUTPUT.read_text(encoding="utf-8"))["input_sha256"] != input_digest():
            print("Japanese address audit is stale; run python scripts/audit_jp_addresses.py", file=sys.stderr)
            return 1
        return 0
    for path, digest in ((gen.US_ROM, gen.US_SHA1), (gen.JP_ROM, gen.JP_SHA1)):
        problem = gen.check_rom(path, digest)
        if problem:
            print(problem, file=sys.stderr)
            return 1
    report = generate(gen.US_ROM.read_bytes(), gen.JP_ROM.read_bytes())
    text = json.dumps(report, indent=2) + "\n"
    counts = {status: sum(r["status"] == status for r in report["addresses"])
              for status in ("candidate", "conflict", "unresolved")}
    print(f"Literal address inventory: {len(report['addresses'])}; {counts}")
    if args.check:
        if not OUTPUT.exists() or OUTPUT.read_text(encoding="utf-8") != text:
            print("Japanese address audit differs; regenerate it", file=sys.stderr)
            return 1
    else:
        OUTPUT.write_text(text, encoding="utf-8", newline="\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
