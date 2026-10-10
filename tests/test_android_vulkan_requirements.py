"""Android Vulkan requirement regression: system drivers without descriptor indexing.

Background: the Android startup check in patch 0013 rejected every driver without
descriptor indexing, so devices on older Adreno system drivers (Vulkan 1.1 without
VK_EXT_descriptor_indexing) needed a Turnip import. RT64 only needed that feature for
its variable-size texture table; patch 0033 gives those drivers a fixed-size table.
The startup check must keep rejecting drivers without scalar block layout, which the
shaders still require, and every platform must apply patch 0033 after 0030.
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCHES = ROOT / "patches"


def added_lines(patch):
    return [
        line[1:]
        for line in patch.read_text(encoding="utf-8").splitlines()
        if line.startswith("+") and not line.startswith("+++")
    ]


class AndroidStartupCheckTests(unittest.TestCase):
    def setUp(self):
        (self.android_patch,) = PATCHES.glob("0013-*.patch")
        self.checks = [
            line for line in added_lines(self.android_patch)
            if re.search(r"\bif\s*\(.*getCapabilities\(\)", line)
        ]

    def test_descriptor_indexing_is_not_required(self):
        self.assertFalse(
            [line for line in self.checks if "descriptorIndexing" in line],
            "patch 0013 must not reject drivers without descriptor indexing",
        )

    def test_scalar_block_layout_is_still_required(self):
        self.assertTrue(
            [line for line in self.checks if "scalarBlockLayout" in line],
            "patch 0013 must still reject drivers without scalar block layout",
        )


class FixedTextureTablePatchTests(unittest.TestCase):
    def test_every_platform_applies_the_fixed_table_after_0030(self):
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        receiver = cmake.find("0030-rt64-sun-shadow-receiver.patch)")
        fixed = cmake.find('lib/rt64" 0033-')
        self.assertNotEqual(fixed, -1, "CMakeLists.txt must apply patch 0033 to lib/rt64")
        self.assertGreater(fixed, receiver, "patch 0033 must apply after 0030")


if __name__ == "__main__":
    unittest.main()
