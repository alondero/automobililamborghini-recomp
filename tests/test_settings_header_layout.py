"""The settings header must keep Quit and Close inside the modal.

Background: the config modal's header is a flex row. Its left side holds the tab
labels and its right side the Quit and Close buttons. RecompFrontend gives the left side
no way to shrink, so once the labels are wider than the modal the buttons are laid
out past its edge and cannot be clicked. Patch 0032 lets the tab side take only the
width the right side leaves and wrap its labels onto further rows.

The layout needs a live RmlUi context, which no host test has, so this test checks
the build input instead: the patch is applied by CMake after 0031, the right side
never shrinks, and the tab side shrinks and wraps. RT64 captures with eight and
twelve tabs are recorded in docs/recompfrontend.md.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCH_NAME = "0032-recompfrontend-settings-header-wrap.patch"
PATCH = ROOT / "patches" / PATCH_NAME
MODAL = "recompui/src/elements/ui_modal.cpp"


def added_lines(patch_text, path):
    body = patch_text.split(f"+++ b/{path}\n", 1)[1]
    body = re.split(r"^diff --git ", body, maxsplit=1, flags=re.MULTILINE)[0]
    return [line[1:] for line in body.splitlines() if line.startswith("+")]


class SettingsHeaderLayoutTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue(PATCH.is_file(), f"missing {PATCH.name}")
        self.added = added_lines(PATCH.read_text(encoding="utf-8"), MODAL)

    def test_patch_is_applied_after_the_existing_frontend_patches(self):
        cmake = (ROOT / "cmake" / "Frontend.cmake").read_text(encoding="utf-8")
        self.assertIn(PATCH_NAME, cmake)
        self.assertLess(
            cmake.index("0031-recompfrontend-document-source-url.patch"),
            cmake.index(PATCH_NAME),
        )

    def test_right_side_never_shrinks(self):
        self.assertTrue(
            any("get_right()->set_flex_shrink(0.0f)" in line for line in self.added),
            "the Quit/Close side can still be squeezed out of the header",
        )

    def test_tab_side_shrinks_and_wraps(self):
        joined = "\n".join(self.added)
        self.assertIn("set_flex(1.0f, 1.0f, 0.0f)", joined)
        self.assertIn("set_min_width(0.0f)", joined)
        self.assertIn("set_flex_wrap(FlexWrap::Wrap)", joined)


if __name__ == "__main__":
    unittest.main()
