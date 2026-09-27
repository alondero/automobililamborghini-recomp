"""The quit confirmation must dismiss on the controller's Back action.

Background: the quit confirmation is the shared recompui prompt, opened from the
config modal's quit button while the game is running. Upstream builds the prompt
from plain ``Element``s with ``events_enabled = 0``, so no element in that tree
attaches an RmlUi Keydown listener and ``recompui::menu_action_mapping`` never
turns a controller press into an ``EventType::MenuAction``. A is accepted because
``ACCEPT_MENU`` maps to Return, which RmlUi handles natively on the focused
button; the Back binding maps to the synthetic F15 key, which nothing consumes.
The result is a prompt no controller button can back out of (issue #250).

The affected code lives in the pinned RecompFrontend submodule, so the behavior
ships as patches/0019. The prompt itself needs a live RmlUi render context, which
no host test has, so this test checks the build input instead: the patch must
route Back to the existing cancel path, must not name a physical controller
button (a remapped Back has to keep working), and must not leave the affirmative
quit action reachable from Back. The pressing device's own Back binding is
covered executably by tests/test_frontend_settings.cpp.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH = ROOT / "patches" / "0019-recompfrontend-prompt-back-cancels.patch"
PROMPT = "recompui/src/composites/ui_prompt.cpp"


def touched_files(patch_text):
    return re.findall(r"^diff --git a/\S+ b/(\S+)$", patch_text, re.MULTILINE)


def changed_lines(patch_text):
    """Return the added and removed lines of the single file the patch edits."""
    body = patch_text.split(f"+++ b/{PROMPT}\n", 1)[1]
    added, removed = [], []
    for line in body.splitlines():
        if line.startswith("+++") or line.startswith("---"):
            continue
        if line.startswith("+"):
            added.append(line[1:])
        elif line.startswith("-"):
            removed.append(line[1:])
    return added, removed


class PromptBackActionTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATCH.is_file(), f"missing {PATCH.name}")
        self.patch = PATCH.read_text(encoding="utf-8")
        self.added, self.removed = changed_lines(self.patch)

    def test_patch_only_edits_the_prompt(self):
        self.assertEqual(touched_files(self.patch), [PROMPT])

    def test_prompt_listens_for_menu_actions(self):
        # Without a Keydown listener there is no menu action to dispatch at all.
        self.assertTrue(
            any("Events(EventType::MenuAction)" in line for line in self.added),
            "the patched prompt does not enable EventType::MenuAction",
        )

    def test_back_action_runs_the_cancel_path(self):
        self.assertTrue(
            any("MenuAction::Back" in line for line in self.added),
            "the patched prompt does not compare against MenuAction::Back",
        )
        self.assertTrue(
            any("run_cancel_callback()" in line for line in self.added),
            "the Back action does not reuse the cancel path that hides the prompt",
        )

    def test_back_is_not_bound_to_a_physical_button(self):
        # cont_button_to_key already resolves the pressing device's Back binding,
        # so naming a button here would override a player's remap.
        offenders = [
            line for line in self.added if "SDL_CONTROLLER_BUTTON" in line
        ]
        self.assertEqual(offenders, [], "the prompt hard-codes a controller button")

    def test_quit_stays_on_explicit_confirmation(self):
        # Back must never reach the affirmative action, and the quit prompt's own
        # quit callback must not become part of this change.
        self.assertFalse(
            any("run_confirm_callback()" in line for line in self.added),
            "the Back action was wired to the confirm path",
        )
        self.assertFalse(
            any("ultramodern::quit" in line for line in self.added),
            "the patch moved the quit action into the prompt element",
        )

    def test_cancel_button_is_preserved(self):
        # Cancelling must keep working by click; only the controller path is added.
        self.assertFalse(
            any("run_cancel_callback" in line and line.strip().startswith("prompt_state")
                for line in self.removed),
            "the patch removed the cancel button callback",
        )

    def test_prompt_window_is_created_with_the_new_element(self):
        window = [
            line
            for line in self.added
            if "get_root_element()" in line and "create_element" in line
        ]
        self.assertEqual(len(window), 1, "the prompt window creation was not changed")
        self.assertIn("PromptWindow", window[0])
        self.assertTrue(
            any("class PromptWindow" in line for line in self.added),
            "the patched prompt does not define PromptWindow",
        )


class PatchInventoryTests(unittest.TestCase):
    def test_frontend_cmake_applies_the_patch_to_the_frontend(self):
        cmake = (ROOT / "cmake" / "Frontend.cmake").read_text(encoding="utf-8")
        applied = re.findall(
            r'lambo_frontend_patch\("\$\{CMAKE_CURRENT_SOURCE_DIR\}/lib/RecompFrontend"\s+(\S+)\)',
            cmake,
        )
        self.assertIn("0019-recompfrontend-prompt-back-cancels.patch", applied)
        # 0019 edits recompui/src/composites/ui_prompt.cpp, which 0017 does not
        # touch, so the order is a convention rather than a dependency. Keep 0017
        # first so the inventory numbering still matches the apply order.
        self.assertLess(
            applied.index("0017-recompfrontend-lamborghini-integration.patch"),
            applied.index("0019-recompfrontend-prompt-back-cancels.patch"),
            "0019 must be applied after 0017 in the patch order",
        )

    def test_patch_inventory_has_a_row_for_the_patch(self):
        inventory = (ROOT / "patches" / "README.md").read_text(encoding="utf-8")
        rows = {
            cells[0]: cells[1]
            for cells in (
                [cell.strip() for cell in line.strip("|").split("|")]
                for line in inventory.splitlines()
                if line.startswith("| 00")
            )
        }
        self.assertIn("0019", rows, "patches/README.md has no 0019 row")
        self.assertEqual(rows["0019"], "RecompFrontend")

    def test_every_build_path_reaches_the_patch_through_cmake(self):
        # The scripts apply the lower-numbered patches themselves; 0016 and up are
        # CMake's job. Assert that per build path rather than counting a phrase, so
        # a reworded matrix does not fail while a genuinely unpatched path does.
        inventory = (ROOT / "patches" / "README.md").read_text(encoding="utf-8")
        matrix = inventory.split("## Application matrix", 1)[1]
        paths = {
            cells[0]: cells[1]
            for cells in (
                [cell.strip() for cell in line.strip("|").split("|")]
                for line in matrix.splitlines()
                if line.startswith("| ") and "Build path" not in line
                and "---" not in line
            )
            if cells[0] != "Build path"
        }
        self.assertEqual(
            sorted(paths),
            ["Android script", "Linux script", "Windows script"],
            "the application matrix rows changed shape",
        )
        for name, applies in paths.items():
            self.assertIn("CMake", applies, f"{name} does not reach 0019 through CMake")


if __name__ == "__main__":
    unittest.main()
