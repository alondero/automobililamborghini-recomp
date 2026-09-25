"""Regression: the Android on-screen Menu button must act as the menu toggle.

The native event loop (src/main.cpp) toggles the settings overlay for the
menu button (F1 / controller Back) before the event reaches the frontend.
Escape takes a different path: it is queued to the frontend, where the
modal closes on the same press, so an Escape-based Menu button opens
Settings and instantly closes it again. The on-screen Menu must therefore
send the toggle key (F1), not Escape.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
GAME_ACTIVITY = ROOT / "android/app/src/main/java/io/github/alondero/lamborghinirecomp/GameActivity.java"
MAIN_CPP = ROOT / "src/main.cpp"

# Android KeyEvent constants the touch overlay can send, mapped to the SDL
# keysym the native loop observes. Unknown keys fail the test explicitly.
ANDROID_TO_SDL = {
    "KEYCODE_F1": "SDLK_F1",
    "KEYCODE_ESCAPE": "SDLK_ESCAPE",
}


def read_menu_button_keycode():
    text = GAME_ACTIVITY.read_text(encoding="utf-8")
    match = re.search(r'button\("Menu",\s*KeyEvent\.(KEYCODE_\w+)', text)
    assert match is not None, "Menu button declaration not found in GameActivity"
    return match.group(1)


def read_toggle_keysyms():
    text = MAIN_CPP.read_text(encoding="utf-8")
    match = re.search(r"const bool menu_button\s*=(.*?);", text, re.DOTALL)
    assert match is not None, "menu_button predicate not found in src/main.cpp"
    return set(re.findall(r"SDLK_\w+", match.group(1)))


class AndroidMenuButtonTests(unittest.TestCase):
    def test_menu_button_sends_toggle_key(self):
        keycode = read_menu_button_keycode()
        self.assertIn(keycode, ANDROID_TO_SDL, f"unmapped Android key: {keycode}")
        self.assertIn(
            ANDROID_TO_SDL[keycode],
            read_toggle_keysyms(),
            f"Menu sends {keycode}, which the native loop does not treat as the menu toggle",
        )


if __name__ == "__main__":
    unittest.main()
