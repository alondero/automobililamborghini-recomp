"""Vulkan clear rectangles stay inside the render area (issue #350).

No ROM or GPU needed. N64 fill rectangles are inclusive, so RT64 asks Plume to
clear 321x241 on a 320x241 target. vkCmdClearAttachments requires every
rectangle inside the render area (VUID-vkCmdClearAttachments-pRects-00016).
Compile Plume's production clear path against a recording Vulkan boundary.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import PLUME, ROOT, method, patched_plume_vulkan


@unittest.skipUnless((PLUME / ".git").exists(), "Plume submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for Vulkan clear fixture")
class ClearBoundsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-clear-bounds-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        source = patched_plume_vulkan(tree)
        fixture = (ROOT / "tests/fixtures/vulkan_clear_bounds.cpp").read_text(encoding="utf-8")
        for signature in ("static void clearCommonRectVector(",
                          "void VulkanCommandList::clearColor(",
                          "void VulkanCommandList::clearDepthStencil("):
            fixture += "\n" + method(source, signature) + "\n"
        cpp = tree / "fixture.cpp"
        cpp.write_text(fixture, encoding="utf-8")
        cls.exe = tree / "fixture.exe"
        result = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def clears(self, scenario):
        result = subprocess.run([str(self.exe), scenario], capture_output=True, text=True,
                                timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.splitlines()

    def test_inclusive_fill_is_clipped_to_render_area(self):
        self.assertEqual(self.clears("native-fill"), ["clear 0,0,320,241"])

    def test_scaled_depth_fill_is_clipped_to_render_area(self):
        self.assertEqual(self.clears("scaled-depth-fill"), ["clear 0,0,640,482"])

    def test_negative_origin_is_clipped(self):
        self.assertEqual(self.clears("negative-origin"), ["clear 0,0,10,10"])

    def test_inside_rectangle_is_unchanged(self):
        self.assertEqual(self.clears("inside"), ["clear 10,20,20,20"])

    def test_rectangle_outside_target_issues_no_clear(self):
        self.assertEqual(self.clears("outside"), [])

    def test_only_visible_rectangles_are_cleared(self):
        self.assertEqual(self.clears("mixed"), ["clear 300,230,20,11"])

    def test_whole_target_clear_is_unchanged(self):
        self.assertEqual(self.clears("whole-target"), ["clear 0,0,320,241"])


if __name__ == "__main__":
    unittest.main()
