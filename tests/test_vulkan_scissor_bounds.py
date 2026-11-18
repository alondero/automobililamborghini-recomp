"""Exercise Plume's dynamic scissors at the Vulkan command boundary."""

import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import PLUME, method, patched_plume_vulkan


FIXTURE = r"""
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <vector>

struct VkOffset2D { int32_t x, y; };
struct VkExtent2D { uint32_t width, height; };
struct VkRect2D { VkOffset2D offset; VkExtent2D extent; };
struct RenderRect { int32_t left, top, right, bottom; };

struct VulkanCommandList {
    int vk = 1;
    void setScissors(const RenderRect *, uint32_t);
};

void vkCmdSetScissor(int, uint32_t, uint32_t count, const VkRect2D *rects) {
    for (uint32_t i = 0; i < count; i++) {
        const auto &rect = rects[i];
        std::printf("%d,%d,%u,%u\n", rect.offset.x, rect.offset.y,
                    rect.extent.width, rect.extent.height);
    }
}

int main() {
    VulkanCommandList commands;
    // RT64 can align a full-width HUD scissor one pixel left of the target.
    const RenderRect hud{-1, 0, 1065, 480};
    commands.setScissors(&hud, 1);

    // The array path must preserve visible bounds and keep empty scissors.
    const RenderRect mixed[]{{10, 20, 30, 40}, {-3, -4, 12, 16}, {-8, 1, -2, 9}};
    commands.setScissors(mixed, 3);
}
"""


@unittest.skipUnless((PLUME / ".git").exists(), "Plume submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for Vulkan scissor fixture")
class VulkanScissorBoundsTests(unittest.TestCase):
    def test_scissor_intersects_nonnegative_coordinates(self):
        with tempfile.TemporaryDirectory(prefix="lambo-vulkan-scissor-") as directory:
            tree = Path(directory)
            source = patched_plume_vulkan(tree)
            # Include any conversion helper between these adjacent commands.
            start = source.index("void VulkanCommandList::setViewports(")
            start += len(method(source, "void VulkanCommandList::setViewports("))
            end = source.index("void VulkanCommandList::setFramebuffer(", start)
            cpp = tree / "fixture.cpp"
            cpp.write_text(FIXTURE + source[start:end], encoding="utf-8")
            exe = tree / "fixture.exe"
            build = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.check_output([str(exe)], text=True, timeout=10)
            self.assertEqual(result.splitlines(), [
                "0,0,1065,480",
                "10,20,20,20",
                "0,0,12,16",
                "0,1,0,8",
            ])


if __name__ == "__main__":
    unittest.main()
