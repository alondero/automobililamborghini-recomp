"""Vulkan texture readback passes a complete buffer image copy (issue #366).

No ROM or GPU needed. Pinned Plume only handled a placed-footprint source and
a subresource destination, so a placed-footprint destination fell through to
vkCmdCopyImage and dereferenced a null destination texture. The port backports
Plume's later image-to-buffer branch. This fixture compiles the production
VulkanCommandList::copyTextureRegion against a recording Vulkan boundary and
checks the aspect, buffer offset/pitch, subresource, image layout and extent.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import PLUME, ROOT, method, patched_plume_vulkan


@unittest.skipUnless((PLUME / ".git").exists(), "Plume submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for Vulkan texture readback fixture")
class TextureReadbackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-texture-readback-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        source = patched_plume_vulkan(tree)
        fixture = (ROOT / "tests/fixtures/vulkan_texture_readback.cpp").read_text(encoding="utf-8")
        fixture += "\n" + method(source, "void VulkanCommandList::copyTextureRegion(") + "\n"
        cpp = tree / "fixture.cpp"
        cpp.write_text(fixture, encoding="utf-8")
        cls.exe = tree / "fixture.exe"
        result = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def copy(self, scenario):
        result = subprocess.run([str(self.exe), scenario], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return result.stdout.splitlines()

    def test_padded_footprint_honors_offset_destination_xy_and_row_pitch(self):
        self.assertEqual(self.copy("padded-offset"), [
            "image-to-buffer src=41 layout=6 dst=71 offset=100 row=8 height=5 "
            "aspect=1 mip=0 layer=0 image=0,0,0 extent=5,3,1"
        ])

    def test_subresource_and_source_box_select_depth_aspect_mip_and_layer(self):
        self.assertEqual(self.copy("subresource-srcbox"), [
            "image-to-buffer src=42 layout=6 dst=71 offset=328 row=16 height=6 "
            "aspect=2 mip=2 layer=5 image=4,2,0 extent=6,4,1"
        ])

    def test_compressed_format_uses_block_width_and_size(self):
        self.assertEqual(self.copy("compressed"), [
            "image-to-buffer src=43 layout=6 dst=71 offset=296 row=16 height=8 "
            "aspect=1 mip=0 layer=0 image=0,0,0 extent=16,8,1"
        ])

    def test_buffer_to_image_upload_branch_is_unchanged(self):
        self.assertEqual(self.copy("upload"), [
            "buffer-to-image src=71 layout=7 dst=44 offset=512 row=8 height=3 "
            "aspect=1 mip=0 layer=0 image=0,0,0 extent=5,3,1"
        ])

    def test_texture_to_texture_fallback_is_unchanged(self):
        self.assertEqual(self.copy("image-to-image"), ["image-to-image"])
        self.assertNotIn("image-to-buffer", self.copy("image-to-image"))


if __name__ == "__main__":
    unittest.main()
