"""Recompute the car-select card's derived stats and pin its hook from the USA ROM."""
import math
from pathlib import Path
import re
import struct
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
ROM = ROOT / "Automobili Lamborghini (USA).z64"
SOURCE = ROOT / "src" / "lambo_car_card.cpp"
HOOK_NAME = "lambo_car_card_draw"
DISPATCHER = "func_800028D0"
HOOK_VRAM = 0x80002480
ENGINE_TABLE = 0xAFAE0
ENGINE_ROW = 52
CURVE_RUNTIME_BASE = 0x8013D6F0
CURVE_ROM_BASE = 0xAF0E0
DESCRIPTORS = 0xAEE80
# Engine speed per (km/h x ratio multiplier x gear ratio), fitted from the
# Diablo's recorded top-gear samples (median 13.74-13.79); see
# docs/CAR_DIFFERENCES.md.
RPM_PER_KMH = 13.77
# The speedometer's own KM/H to MPH factor (double at 0x8008E598).
MPH_FACTOR_VRAM = 0x8008E598
# Chrome-font tables, loaded with the engine table's bank (runtime =
# ROM + 0x8008E610): glyph map 0x8015DA16 and advances 0x8015DA8A.
FONT_MAP_ROM = 0xCF406
FONT_ADVANCE_ROM = 0xCF47A
NAME_SCALE = 0.625
STATS_SCALE = 0.5
# Centred on x 159 inside a strip from x 54 to 265.
STRIP_WIDTH = 2 * min(159 - 54, 265 - 159)
CAR = re.compile(r'\{"([A-Z0-9 ]+)", (\d+), (\d+), (\d+), (\d+), (\d+), (\d+), '
                 r'\{(\d+), (\d+)\}, \{(\d+), (\d+)\}\}')
# Decoded instructions: (vram, opcode, rt, rs, unsigned immediate).
PINNED = (
    (0x80002464, 0x04, 0, 0, 0x0006),     # b 0x80002480 (join from the other path)
    (0x80002480, 0x0F, 9, 0, 0x800A),     # lui t1, 0x800A
    (0x80002484, 0x23, 9, 9, 0x39CC),     # lw t1, DL cursor
    (0x8000249C, 0x0F, 11, 0, 0xE900),    # lui t3, G_RDPFULLSYNC
    (0x800024C8, 0x0F, 24, 0, 0xB800),    # lui t8, G_ENDDL
    (0x80001008, 0x09, 14, 25, 0x0068),   # frame block pointer = block + 0x68
    (0x80001024, 0x09, 10, 15, 0x01C0),   # display list starts 0x1C0 later
    (0x8004C2C8, 0x0F, 8, 0, 0x8016),     # chrome-font advance table
    (0x8004C2D8, 0x21, 8, 8, 0xDA8A),     # lh t0, -0x2576(t0)
    (0x8004C3E8, 0x0F, 10, 0, 0x8016),    # chrome-font glyph map
    (0x8004C3F0, 0x21, 10, 10, 0xDA16),   # lh t2, -0x25EA(t2)
    (0x8004C450, 0x0F, 8, 0, 0x8014),     # chrome-font textures
    (0x8004C454, 0x09, 8, 8, 0xF820),     # addiu t0, t0, -0x7E0
)


def source_cars():
    return [tuple([m[0]] + [int(v) for v in m[1:]]) for m in CAR.findall(SOURCE.read_text(encoding="utf-8"))]


def derived(rom, category):
    row = ENGINE_TABLE + category * ENGINE_ROW
    idle, _, limit = struct.unpack_from(">3H", rom, row)
    gears, = struct.unpack_from(">H", rom, row + 8)
    multiplier, = struct.unpack_from(">f", rom, row + 12)
    top_ratio, = struct.unpack_from(">f", rom, row + 16 + gears * 4)
    curve_address, = struct.unpack_from(">I", rom, row + 48)
    curve = struct.unpack_from(">160h", rom, curve_address - CURVE_RUNTIME_BASE + CURVE_ROM_BASE)
    # The engine updater's torque is curve[rpm >> 6] at full throttle, in N m.
    power_w = max(rpm * curve[rpm >> 6] for rpm in range(idle, limit + 1)) * 2 * math.pi / 60
    torque = max(curve[rpm >> 6] for rpm in range(idle, limit + 1))
    top_kmh = int(limit / (RPM_PER_KMH * multiplier * top_ratio))
    mph_factor, = struct.unpack_from(">d", rom, MPH_FACTOR_VRAM - 0x80000000 + 0xC00)
    return (round(power_w / 745.7), torque, limit, gears, top_kmh, int(top_kmh * mph_factor))


class CarCardRomTests(unittest.TestCase):
    def test_hook_location(self):
        config = tomllib.loads((ROOT / "lamborghini.us.toml").read_text(encoding="utf-8"))
        hooks = [hook for hook in config["patches"]["hook"] if HOOK_NAME in hook["text"]]
        self.assertEqual([(hook["func"], hook["before_vram"]) for hook in hooks],
                         [(DISPATCHER, HOOK_VRAM)])

    def test_source_table_shape(self):
        cars = source_cars()
        self.assertEqual(len(cars), 8, "one row per physics category")
        for name, *_, n100, e100, n60, e60 in cars:
            with self.subTest(car=name):
                # 60 mph (97 km/h on the speedometer) comes before 100 km/h,
                # and Expert launches faster than Novice in every measurement.
                self.assertLess(n60, n100)
                self.assertLess(e60, e100)
                self.assertLess(e100, n100)

    @unittest.skipUnless(ROM.is_file(), "USA ROM required for derived car stats")
    def test_derived_stats_match_rom(self):
        rom = ROM.read_bytes()
        self.assertAlmostEqual(struct.unpack_from(">d", rom, MPH_FACTOR_VRAM - 0x80000000 + 0xC00)[0], 0.621)
        categories = [struct.unpack_from(">H", rom, DESCRIPTORS + i * 24)[0] for i in range(24)]
        self.assertEqual(categories, [c for c in (0, 4, 1, 2, 3, 5, 6, 7) for _ in range(3)])
        for category, car in enumerate(source_cars()):
            with self.subTest(category=category, car=car[0]):
                self.assertEqual(car[1:7], derived(rom, category))

    @unittest.skipUnless(ROM.is_file(), "USA ROM required for font tables")
    def test_every_card_line_fits_its_strip(self):
        rom = ROM.read_bytes()
        glyph_map = struct.unpack_from(">96h", rom, FONT_MAP_ROM)
        advance = struct.unpack_from(">96h", rom, FONT_ADVANCE_ROM)
        self.assertEqual(glyph_map[ord("0"):ord("9") + 1], tuple(range(0x30, 0x3A)))
        self.assertEqual(advance[ord("0")], 13)
        for name, hp, torque, limit, gears, kmh, mph, n100, e100, n60, e60 in source_cars():
            for units, top, updates in (("KM/H", kmh, (n100, e100)), ("MPH", mph, (n60, e60))):
                for difficulty, label in enumerate(("NOVICE", "EXPERT")):
                    tenths = (updates[difficulty] * 10 + 15) // 30
                    sprint = "0-100 KM/H" if units == "KM/H" else "0-60 MPH"
                    # Mirrors lambo::car_card::card_text, pinned by test_car_card.cpp.
                    lines = ((name, NAME_SCALE),
                             (f"{hp} HP   {torque} NM   {limit} RPM", STATS_SCALE),
                             (f"TOP SPEED {top} {units}   {gears} GEARS", STATS_SCALE),
                             (f"{sprint} {tenths // 10}.{tenths % 10} S   {label}", STATS_SCALE))
                    for text, scale in lines:
                        with self.subTest(text=text):
                            for c in text.replace(" ", ""):
                                self.assertTrue(0x30 <= glyph_map[ord(c)] < 0x60, f"no glyph for {c!r}")
                            width = sum(10 if c == " " else advance[ord(c)] for c in text) * scale
                            self.assertLessEqual(width, STRIP_WIDTH)

    @unittest.skipUnless(ROM.is_file(), "USA ROM required for hook instruction pins")
    def test_rom_instructions(self):
        symbols = tomllib.loads((ROOT / "lamborghini.syms.toml").read_text(encoding="utf-8"))
        functions = {f["name"]: (section, f) for section in symbols["section"]
                     for f in section["functions"]}
        section, dispatcher = functions[DISPATCHER]
        self.assertTrue(dispatcher["vram"] < HOOK_VRAM < dispatcher["vram"] + dispatcher["size"])
        rom = ROM.read_bytes()
        for vram, opcode, rt, rs, immediate in PINNED:
            with self.subTest(vram=f"0x{vram:08X}"):
                word, = struct.unpack_from(">I", rom, section["rom"] + vram - section["vram"])
                self.assertEqual((word >> 26, (word >> 16) & 31, (word >> 21) & 31, word & 0xFFFF),
                                 (opcode, rt, rs, immediate))


if __name__ == "__main__":
    unittest.main()
