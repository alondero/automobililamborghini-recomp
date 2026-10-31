"""Relocation evidence must not turn stale registers into Japanese addresses."""
import json
import contextlib
import io
from pathlib import Path
import sys
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import audit_jp_addresses as audit
import gen_jp_syms_toml as gen


def immediate(op, rt, rs, value):
    return (op << 26) | (rs << 21) | (rt << 16) | (value & 0xFFFF)


def lui(rt, value=0x800A):
    return immediate(15, rt, 0, value)


def addu(rd, rs, rt):
    return (rs << 21) | (rt << 16) | (rd << 11) | 33


def refs(words):
    return list(audit.references(words, 0x80000400))


class AddressExpressionTests(unittest.TestCase):
    def test_signed_load_offset_and_unsigned_ori(self):
        self.assertEqual(refs([lui(1), immediate(35, 2, 1, 0xFFFF)]),
                         [(1, 0x8009FFFF, False)])
        self.assertEqual(refs([lui(1), immediate(13, 1, 1, 0xFFFF)]),
                         [(1, 0x800AFFFF, False)])

    def test_addiu_and_move_preserve_address(self):
        self.assertEqual(refs([lui(1), immediate(9, 1, 1, 0x8000), addu(3, 1, 0),
                               immediate(35, 2, 3, 4)]),
                         [(1, 0x80098000, False), (3, 0x80098004, False)])

    def test_zero_register_cannot_carry_an_address(self):
        self.assertEqual(refs([lui(0), immediate(35, 2, 0, 4)]), [])

    def test_indexed_base_is_labelled_and_not_propagated_through_ori(self):
        self.assertEqual(refs([lui(1), addu(1, 1, 3), immediate(35, 2, 1, 4),
                               immediate(13, 1, 1, 8), immediate(35, 2, 1, 12)]),
                         [(2, 0x800A0004, True)])

    def test_two_known_bases_are_not_an_index_expression(self):
        self.assertEqual(refs([lui(1), lui(2), addu(1, 1, 2), immediate(35, 3, 1, 4)]), [])

    def test_integer_load_overwrites_its_base_register(self):
        self.assertEqual(refs([lui(1), immediate(35, 1, 1, 4), immediate(35, 2, 1, 8)]),
                         [(1, 0x800A0004, False)])

    def test_float_load_does_not_overwrite_integer_base(self):
        self.assertEqual(refs([lui(1), immediate(49, 1, 1, 4), immediate(35, 2, 1, 8)]),
                         [(1, 0x800A0004, False), (2, 0x800A0008, False)])

    def test_store_conditional_invalidates_result_register(self):
        self.assertEqual(refs([lui(1), immediate(56, 1, 1, 4), immediate(35, 2, 1, 8)]),
                         [(1, 0x800A0004, False)])

    def test_register_overwrites_invalidate_provenance(self):
        for instruction in [immediate(12, 1, 3, 4), (3 << 16) | (1 << 11),
                            (17 << 26) | (1 << 16),  # mfc1 r1,f0
                            (3 << 21) | (4 << 16) | (1 << 11) | 10]:  # movz
            with self.subTest(instruction=instruction):
                self.assertEqual(refs([lui(1), instruction, immediate(35, 2, 1, 4)]), [])

    def test_call_keeps_delay_slot_but_not_return_path(self):
        self.assertEqual(refs([lui(1), 0x0C000400, immediate(35, 2, 1, 4),
                               immediate(35, 2, 1, 8)]), [(2, 0x800A0004, False)])

    def test_call_overwrites_link_before_delay_slot(self):
        self.assertEqual(refs([lui(31), 0x0C000400, immediate(35, 2, 31, 4)]), [])

    def test_branch_target_drops_fallthrough_provenance(self):
        # bne -> instruction 3; its other predecessor skips the LUI at 2.
        self.assertEqual(refs([immediate(5, 2, 1, 2), 0, lui(1),
                               immediate(35, 2, 1, 4)]), [])

    def test_jump_target_drops_fallthrough_provenance(self):
        self.assertEqual(refs([0x08000103, 0, lui(1), immediate(35, 2, 1, 4)]), [])

    def test_unknown_instruction_drops_provenance(self):
        self.assertEqual(refs([lui(1), 0x70000000, immediate(35, 2, 1, 4)]), [])

    def test_indirect_case_entry_cannot_inherit_fallthrough_provenance(self):
        # jr t9 may jump directly to the final load, skipping the preceding
        # LUI. Its targets are unknown, so reject this function's evidence.
        self.assertEqual(refs([0x03200008, 0, lui(1), immediate(35, 2, 1, 4)]), [])

    def test_return_does_not_disqualify_a_function(self):
        self.assertEqual(refs([lui(1), 0x03E00008, immediate(35, 2, 1, 4)]),
                         [(2, 0x800A0004, False)])


class InventoryTests(unittest.TestCase):
    def test_continued_line_comment_is_not_code(self):
        source = "// continued \\\n0x80000001\n0x80000002u"
        self.assertEqual(list(audit.literal_addresses(source)), [(3, 0x80000002)])

    def test_comments_strings_raw_strings_and_character_literals_are_ignored(self):
        source = '''// 0x80000001
/* 0x80000002
   0x80000003 */ const auto a = 0x80000004u;
const auto b = "escaped \\\" 0x80000005";
const auto c = R"tag(0x80000006 "quoted")tag";
const char d = '\\''; auto e = UINT32_C(0x80000007);
auto f = 0x81000000ULL; auto g = 0X80000008;
auto h = 0x82000000; auto i = 0x7FFFFFFF;
'''
        self.assertEqual(list(audit.literal_addresses(source)),
                         [(3, 0x80000004), (6, 0x80000007), (7, 0x81000000), (7, 0x80000008)])

    def test_checked_in_report_has_current_inputs_and_source_inventory(self):
        report = json.loads(audit.OUTPUT.read_text(encoding="utf-8"))
        self.assertEqual(report["input_sha256"], audit.input_digest(),
                         "run python scripts/audit_jp_addresses.py with both ROMs")
        self.assertEqual({int(a["usa"], 16): a["uses"] for a in report["addresses"]}, audit.inventory())
        self.assertEqual(report["rom_sha1"], {"usa": gen.US_SHA1, "japan": gen.JP_SHA1})


class CandidateTests(unittest.TestCase):
    def test_wrong_cartridge_fails_before_generating_or_writing(self):
        with mock.patch.object(gen, "check_rom", return_value="wrong cartridge"), \
             mock.patch.object(audit, "generate") as generate, \
             contextlib.redirect_stderr(io.StringIO()):
            self.assertEqual(audit.main([]), 1)
            generate.assert_not_called()

    def test_only_relocation_matches_are_used_and_overlay_mapping_is_respected(self):
        us = b"".join(w.to_bytes(4, "big") for w in [lui(1), immediate(35, 2, 1, 4)])
        jp = bytes(16) + b"".join(w.to_bytes(4, "big") for w in [lui(1, 0x8010), immediate(35, 2, 1, 8)])
        us_section = gen.Section("us", 0, 0x80000400, 8, 8)
        jp_section = gen.Section("overlay", 16, 0x801F9800, 8, 24)
        f = gen.Func("function", us_section.vram, 8)
        with mock.patch.object(gen, "US_TEXT", us_section), mock.patch.object(gen, "JP_SECTIONS", (jp_section,)):
            self.assertFalse(audit.address_candidates(us, jp, [f], {f.name: (jp_section.vram, False)}))
            candidates = audit.address_candidates(us, jp, [f], {f.name: (jp_section.vram, True)})
        self.assertEqual(candidates[0x800A0004][0x80100008],
                         {(f.name, 0x80000404, 0x801F9804, False)})

    def test_conflicting_targets_are_retained(self):
        evidence = {0x80000004: {0x80000008: {("f", 4, 8, False)},
                                 0x8000000C: {("g", 4, 12, False)}}}
        with mock.patch.object(gen, "load_us_functions", return_value=[]), \
             mock.patch.object(gen, "match_functions", return_value={}), \
             mock.patch.object(audit, "address_candidates", return_value=evidence), \
             mock.patch.object(audit, "inventory", return_value={0x80000004: ["test:1"]}):
            entry = audit.generate(b"", b"")["addresses"][0]
        self.assertEqual(entry["status"], "conflict")
        self.assertEqual([c["japan"] for c in entry["candidates"]], ["0x80000008", "0x8000000C"])


ROM_PROBLEM = gen.check_rom(gen.US_ROM, gen.US_SHA1) or gen.check_rom(gen.JP_ROM, gen.JP_SHA1)


@unittest.skipIf(ROM_PROBLEM, ROM_PROBLEM)
class RomEvidenceTests(unittest.TestCase):
    def test_report_reproduces_from_verified_cartridges(self):
        self.assertEqual(json.loads(audit.OUTPUT.read_text(encoding="utf-8")),
                         audit.generate(gen.US_ROM.read_bytes(), gen.JP_ROM.read_bytes()))


if __name__ == "__main__":
    unittest.main()
