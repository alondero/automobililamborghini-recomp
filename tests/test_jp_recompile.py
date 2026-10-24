"""Japanese cartridge recompile inputs.

Structure checks always run. With both ROMs present the checked-in tables must be
reproducible and cover all code, and with a built N64Recomp the Japanese recompile must
complete without a function that branches outside its own boundaries and must compile.
"""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))
import gen_jp_syms_toml as gen  # noqa: E402

CONFIG_TEXT = (ROOT / "lamborghini.jp.toml").read_text(encoding="utf-8")
SYMS = tomllib.loads((ROOT / "lamborghini.jp.syms.toml").read_text(encoding="utf-8"))
CONFIG = tomllib.loads(CONFIG_TEXT)
US_NAMES = {f["name"] for f in tomllib.loads(
    (ROOT / "lamborghini.syms.toml").read_text(encoding="utf-8"))["section"][0]["functions"]}
FUNCS = [(s["name"], f) for s in SYMS["section"] for f in s["functions"]]
RUNTIME = ROOT / "lib/N64ModernRuntime"


def word_bytes(words):
    return b"".join(w.to_bytes(4, "big") for w in words)


def addu(rd, rs, rt):
    return (rs << 21) | (rt << 16) | (rd << 11) | 0x21


def rom_problem():
    """Why the ROM tests cannot run, or None; the reason names the identity it wants."""
    return next(filter(None, (gen.check_rom(gen.US_ROM, gen.US_SHA1),
                              gen.check_rom(gen.JP_ROM, gen.JP_SHA1))), None)


ROM_PROBLEM = rom_problem()


def n64recomp():
    base = ROOT / "build/lib/N64ModernRuntime/librecomp/N64Recomp"
    return next((p for p in (base / "N64Recomp.exe", base / "N64Recomp") if p.is_file()), None)


class JapaneseInputStructureTests(unittest.TestCase):
    def test_sections_match_the_recorded_layout(self):
        got = [(s["name"], s["rom"], s["vram"], s["size"]) for s in SYMS["section"]]
        want = [(s.name, s.rom, s.vram, s.size) for s in gen.JP_SECTIONS]
        self.assertEqual(got, want)

    def test_functions_are_aligned_and_inside_their_section(self):
        sections = {s.name: s for s in gen.JP_SECTIONS}
        for section, f in FUNCS:
            s = sections[section]
            with self.subTest(f["name"]):
                self.assertEqual(f["vram"] % 4, 0)
                self.assertEqual(f["size"] % 4, 0)
                self.assertGreater(f["size"], 0)
                self.assertTrue(s.contains_vram(f["vram"]))
                self.assertLessEqual(f["vram"] + f["size"], s.vram + s.size)

    def test_names_are_unique_and_traceable(self):
        names = [f["name"] for _, f in FUNCS]
        self.assertEqual(len(names), len(set(names)))
        for name in names:
            with self.subTest(name):
                self.assertTrue(name.startswith("jp_func_") or name in US_NAMES)

    def test_entrypoint_has_a_function(self):
        self.assertEqual(CONFIG["input"]["entrypoint"], gen.JP_MAIN.vram)
        self.assertIn(gen.JP_MAIN.vram, {f["vram"] for _, f in FUNCS})

    def test_config_names_the_japanese_inputs_and_a_separate_output(self):
        inputs = CONFIG["input"]
        self.assertEqual(inputs["rom_file_path"], gen.JP_ROM.name)
        self.assertEqual(inputs["symbols_file_path"], "lamborghini.jp.syms.toml")
        self.assertEqual(inputs["output_func_path"], "RecompiledFuncsJP")

    def test_patch_lists_name_existing_functions_once(self):
        names = {f["name"] for _, f in FUNCS}
        stubs, ignored = CONFIG["patches"]["stubs"], CONFIG["patches"]["ignored"]
        self.assertEqual(len(stubs), len(set(stubs)), "duplicate entries in stubs")
        self.assertEqual(len(ignored), len(set(ignored)), "duplicate entries in ignored")
        self.assertFalse(set(stubs) & set(ignored))
        for name in stubs + ignored:
            with self.subTest(name):
                self.assertIn(name, names)

    def test_function_start_addresses_are_unique(self):
        # Two functions starting at one address would give the recompiler two bodies
        # for one jump target. Alternate entries inside a body start elsewhere.
        vrams = [f["vram"] for _, f in FUNCS]
        self.assertEqual(len(vrams), len(set(vrams)), "two functions start at one address")

    def test_derived_from_the_current_usa_inputs(self):
        # Pull requests run without ROMs, so this is what catches a USA symbol or
        # patch-list change that was not carried to the Japanese tables.
        recorded = next((line[len(gen.USA_INPUTS_PREFIX):].rstrip(".")
                         for line in CONFIG_TEXT.splitlines()
                         if line.startswith(gen.USA_INPUTS_PREFIX)), None)
        self.assertEqual(recorded, gen.usa_inputs_digest(),
                         "USA inputs changed; run scripts/gen_jp_syms_toml.py")


class GeneratorRuleTests(unittest.TestCase):
    """Generator rules checked on small synthetic code, so they run without the ROMs."""

    def patch_layout(self, jp_section, us_text):
        for name, value in (("JP_SECTIONS", (jp_section,)), ("US_TEXT", us_text)):
            patcher = mock.patch.object(gen, name, value)
            patcher.start()
            self.addCleanup(patcher.stop)

    def test_two_usa_functions_never_share_an_entry_address(self):
        # The short body is a prefix of the long one, so both match the same Japanese
        # entry. Only one USA name may keep it.
        short = [addu(1, 2, 3), addu(4, 5, 6), addu(7, 8, 9), addu(10, 11, 12)]
        long_extra = [addu(13, 14, 15), addu(16, 17, 18), addu(19, 20, 21), addu(22, 23, 24)]
        self.patch_layout(gen.Section(".test", 0, 0x80000000, 0x40, 0x40),
                          gen.Section(".text", 0, 0x80000000, 0x30, 0x30))
        us = word_bytes(short + short + long_extra)
        jp = word_bytes(short + long_extra + [0] * 8)
        us_funcs = [gen.Func("func_short", 0x80000000, 16), gen.Func("func_long", 0x80000010, 32)]
        matched = gen.match_functions(us, jp, us_funcs)
        entries = [v for v, _ in matched.values()]
        self.assertEqual(len(entries), len(set(entries)), matched)
        self.assertEqual(set(matched), {"func_short"})

    def test_switch_table_stops_at_its_range_check(self):
        # The compiler range-checks the index with `sltiu at,v0,2`, so the table has two
        # entries. The two valid words after them must not be read as cases.
        code = [
            (0x0B << 26) | (2 << 21) | (1 << 16) | 2,   # sltiu at,v0,2
            (0x04 << 26) | (1 << 21) | 4,               # beq at,zero,...
            (2 << 16) | (2 << 11) | (2 << 6),           # sll v0,v0,2
            (0x0F << 26) | (1 << 16) | 0x8000,          # lui at,0x8000
            addu(1, 1, 2),                              # addu at,at,v0
            (0x23 << 26) | (1 << 21) | (25 << 16) | 0x80,  # lw t9,0x80(at)
            0x03200008,                                 # jr t9
        ]
        table = [0x80000040, 0x80000044, 0x80000048, 0x8000004C]
        rom = word_bytes(code).ljust(0x80, b"\0") + word_bytes(table).ljust(0x80, b"\0")
        sec = gen.Section(".test", 0, 0x80000000, 0x100, 0x100)
        got = gen.jump_table_targets(rom, sec, sec.vram, 0x80000018, sec.vram + sec.size)
        self.assertEqual(got, [0x80000040, 0x80000044])

    def test_jump_target_takes_its_region_from_the_jump_address(self):
        self.assertEqual(gen.jump_target(0x0C000001, 0x90000000), 0x90000004)
        with self.assertRaises(TypeError):
            gen.jump_target(0x0C000001)


@unittest.skipIf(ROM_PROBLEM, ROM_PROBLEM)
class JapaneseInputRomTests(unittest.TestCase):
    def test_checked_in_inputs_are_current(self):
        syms_text, cfg_text, _ = gen.generate(gen.US_ROM.read_bytes(), gen.JP_ROM.read_bytes())
        self.assertEqual(syms_text, gen.OUT_SYMS.read_text(encoding="utf-8"))
        self.assertEqual(cfg_text, gen.OUT_CONFIG.read_text(encoding="utf-8"))

    def test_every_code_word_outside_a_function_is_padding(self):
        # N64Recomp only reports code it was told about; code left in a gap and
        # reached through a pointer or jump table would fail only at runtime.
        jp = gen.JP_ROM.read_bytes()
        covered = set()
        for _, f in FUNCS:
            covered.update(range(f["vram"], f["vram"] + f["size"], 4))
        for s in gen.JP_SECTIONS:
            for v in range(s.vram, s.vram + s.size, 4):
                if v not in covered:
                    with self.subTest(f"{v:#010x}"):
                        self.assertEqual(gen.word(jp, s.rom_of(v)), gen.NOP)


@unittest.skipIf(ROM_PROBLEM, ROM_PROBLEM)
@unittest.skipUnless(n64recomp(), "build N64Recomp first (build.ps1 or build.sh)")
class JapaneseRecompileTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        tmp = Path(cls.tmp.name)
        text = CONFIG_TEXT
        for key, value in (("output_func_path", tmp / "out"),
                           ("symbols_file_path", ROOT / "lamborghini.jp.syms.toml"),
                           ("rom_file_path", gen.JP_ROM)):
            text = text.replace(f'{key} = "{CONFIG["input"][key]}"',
                                f'{key} = "{value.as_posix()}"')
        (tmp / "out").mkdir()
        config = tmp / "jp.toml"
        config.write_text(text, encoding="utf-8")
        cls.run_result = subprocess.run([str(n64recomp()), str(config)], cwd=tmp,
                                        capture_output=True, text=True, errors="replace")
        cls.out = tmp / "out"

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def test_recompile_completes_inside_function_boundaries(self):
        log = self.run_result.stdout + self.run_result.stderr
        self.assertEqual(self.run_result.returncode, 0, log[-2000:])
        self.assertNotIn("branching outside", log)
        self.assertNotIn("[Warn]", log)
        generated = "".join(p.read_text(encoding="utf-8", errors="replace")
                            for p in self.out.glob("funcs_*.c"))
        self.assertIn("recomp_entrypoint", generated)
        # A missing call target would be invented as a mis-sized static function.
        self.assertNotIn("RECOMP_FUNC void static_", generated)

    @unittest.skipUnless(shutil.which("gcc") and shutil.which("g++"), "gcc and g++ required")
    def test_output_compiles_with_the_recompiled_library_flags(self):
        self.assertEqual(self.run_result.returncode, 0)
        includes = [f"-I{RUNTIME / d / 'include'}" for d in ("ultramodern", "librecomp", "N64Recomp")]
        # The C diagnostics CMakeLists.txt relaxes for RecompiledFuncs.
        c_flags = ["-std=c17", "-Wno-unused-variable", "-Wno-implicit-function-declaration"]
        jobs = [["gcc", *c_flags, "-fsyntax-only", *includes, str(p)]
                for p in sorted(self.out.glob("*.c"))]
        jobs += [["g++", "-std=c++20", "-Wno-unused-variable", "-fsyntax-only", *includes, str(p)]
                 for p in sorted(self.out.glob("*.cpp"))]
        self.assertTrue(jobs)
        with ThreadPoolExecutor() as pool:
            results = list(pool.map(lambda cmd: subprocess.run(
                cmd, capture_output=True, text=True, errors="replace"), jobs))
        for cmd, result in zip(jobs, results):
            with self.subTest(Path(cmd[-1]).name):
                self.assertEqual(result.returncode, 0, result.stderr[-2000:])


if __name__ == "__main__":
    unittest.main()
