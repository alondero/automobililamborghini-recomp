"""Keep the public USA data references aligned with the port's named layouts."""
from pathlib import Path
import re
import tomllib
import unittest

ROOT = Path(__file__).resolve().parents[1]
SYMBOLS = {
    "lambo_game_state": "LAMBO_GUEST_GAME_STATE_ADDR",
    "lambo_race_mode": "LAMBO_GUEST_RACE_MODE_ADDR",
    "lambo_player_count": "LAMBO_GUEST_PLAYER_COUNT_ADDR",
    "lambo_field_count": "LAMBO_GUEST_FIELD_COUNT_ADDR",
    "lambo_race_difficulty": "LAMBO_GUEST_RACE_DIFFICULTY_ADDR",
    "lambo_menu_track": "LAMBO_GUEST_MENU_TRACK_ADDR",
}


class ModDataSymbolsTests(unittest.TestCase):
    def test_public_symbols_match_named_guest_addresses(self):
        definitions = dict(re.findall(
            r"#define\s+(LAMBO_GUEST_\w+_ADDR)\s+UINT32_C\((0x[0-9A-Fa-f]+)\)",
            (ROOT / "src/lambo_vehicle.h").read_text(encoding="utf-8")))
        with (ROOT / "lamborghini.datasyms.toml").open("rb") as source:
            sections = tomllib.load(source)["section"]
        actual = {symbol["name"]: symbol["vram"]
                  for section in sections for symbol in section["symbols"]}
        self.assertEqual(actual, {symbol: int(definitions[name], 16)
                                  for symbol, name in SYMBOLS.items()})
        self.assertTrue(all(address % 2 == 0 for address in actual.values()))


if __name__ == "__main__":
    unittest.main()
