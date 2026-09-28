"""Regression: the Windows native menu bar stays retired (issue #242).

The in-game overlay owns every setting, so the Win32 menu bar was a second,
Windows-only settings surface. This is a source-level guard because the thing
being retired is a platform surface: it cannot be observed from a unit test that
does not create a window. It checks that no native menu is built or attached, and
that the shortcuts the issue kept -- F11 and Alt+Enter, plus the overlay's own
input -- are still wired in the main-thread event pump.
"""

import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "src"
CMAKE = ROOT / "CMakeLists.txt"
MAIN = SRC / "main.cpp"

# Win32 menu construction and attachment. Each call only ever appeared in the
# retired src/lambo_menu.cpp; a new one anywhere in src/ is the regression.
WIN32_MENU_CALLS = (
    "SetMenu(",
    "CreateMenu(",
    "CreatePopupMenu(",
    "AppendMenuW(",
    "CheckMenuItem(",
    "CheckMenuRadioItem(",
    "DrawMenuBar(",
)


def source_files():
    return sorted(path for path in SRC.rglob("*")
                  if path.suffix in {".cpp", ".h", ".c", ".hpp"})


class NativeMenuRetiredTests(unittest.TestCase):
    def test_menu_translation_units_are_gone(self):
        for name in ("lambo_menu.cpp", "lambo_menu.h"):
            self.assertFalse((SRC / name).exists(),
                             f"src/{name} still exists; the native menu bar was retired in #242")

    def test_menu_sources_are_not_in_the_build(self):
        listed = [line.strip() for line in
                  CMAKE.read_text(encoding="utf-8").splitlines()
                  if "lambo_menu.cpp" in line]
        # lambo_menu_stick.cpp is unrelated pre-race stick scaling and stays.
        listed = [line for line in listed if "lambo_menu_stick" not in line]
        self.assertEqual([], listed, "CMakeLists.txt still compiles the native menu")

    def test_no_win32_menu_call_remains(self):
        offenders = [f"{path.relative_to(ROOT)}: {call}"
                     for path in source_files()
                     for call in WIN32_MENU_CALLS
                     if call in path.read_text(encoding="utf-8", errors="replace")]
        self.assertEqual([], offenders, "a native Win32 menu is still built or attached")

    def test_menu_namespace_callers_are_gone(self):
        callers = [f"{path.relative_to(ROOT)}"
                   for path in source_files()
                   if "lambo::menu::" in path.read_text(encoding="utf-8", errors="replace")]
        self.assertEqual([], callers, "a caller still routes through lambo::menu")

    def test_fullscreen_shortcuts_still_toggle(self):
        source = MAIN.read_text(encoding="utf-8")
        self.assertIn("SDLK_F11", source, "F11 fullscreen was lost with the menu")
        self.assertIn("KMOD_ALT", source, "Alt+Enter fullscreen was lost with the menu")
        self.assertIn("SDL_SetWindowFullscreen", source,
                      "the fullscreen toggle no longer applies the window mode")
        self.assertIn("lambo::config::update_saved_window_mode", source,
                      "the fullscreen toggle no longer persists the window mode")

    def test_overlay_is_still_the_settings_entry_point(self):
        source = MAIN.read_text(encoding="utf-8")
        self.assertIn("lambo::ui::toggle_settings();", source,
                      "F1 / controller Back no longer toggles the settings overlay")
        self.assertIn("lambo::ui::open_settings();", source,
                      "Escape no longer opens the settings overlay")


if __name__ == "__main__":
    unittest.main()
