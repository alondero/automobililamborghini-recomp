"""The settings overlay draws in the swap chain's format (issue #350).

No ROM or GPU needed. Patch 0013 creates RT64's Android swap chain as
R8G8B8A8_UNORM. RecompFrontend's UI pipeline, which draws the settings overlay
into that swap chain, hard-coded B8G8R8A8_UNORM, so Android validation reported
VUID-vkCmdDrawIndexed-renderPass-02684. Compile both production format
selections for Android and desktop and require them to agree.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import ROOT, patched_file

RT64 = ROOT / "lib/rt64"
FRONTEND = ROOT / "lib/RecompFrontend"


def selection(source, marker):
    """Return the line containing marker, with the #if block that opens just above it."""
    lines = source.splitlines()
    index = next(i for i, line in enumerate(lines) if marker in line)
    opening = index - 1
    while lines[opening].strip().startswith("//"):
        opening -= 1
    if not lines[opening].strip().startswith("#if"):
        return lines[index]
    closing = next(i for i in range(index, len(lines)) if lines[i].strip() == "#endif")
    return "\n".join(lines[opening:closing + 1])


@unittest.skipUnless((RT64 / ".git").exists() and (FRONTEND / ".git").exists(),
                     "RT64 and RecompFrontend submodules not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for swap chain format fixture")
class SwapChainFormatTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-swapchain-format-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        rt64 = patched_file(RT64, "src/hle/rt64_application.cpp", tree / "rt64")
        frontend = patched_file(FRONTEND, "recompui/src/renderer/ui_renderer.cpp", tree / "frontend")
        cls.cpp = tree / "fixture.cpp"
        cls.cpp.write_text("\n".join([
            "enum class RenderFormat { B8G8R8A8_UNORM, R8G8B8A8_UNORM };",
            "namespace plume { using RenderFormat = ::RenderFormat; }",
            "struct RenderSwapChainDesc { RenderFormat format; };",
            "constexpr RenderFormat rt64SwapChainFormat() {",
            "    RenderSwapChainDesc swapChainDesc{};",
            selection(rt64, "swapChainDesc.format ="),
            "    return swapChainDesc.format;",
            "}",
            "struct Frontend {",
            selection(frontend, "SwapChainFormat ="),
            "};",
            "static_assert(rt64SwapChainFormat() == Frontend::SwapChainFormat,",
            "              \"overlay pipeline format differs from the swap chain\");",
            "int main() { return 0; }",
            "",
        ]), encoding="utf-8")

    def compile(self, *defines):
        return subprocess.run(["g++", "-std=c++17", "-fsyntax-only", *defines, str(self.cpp)],
                              capture_output=True, text=True)

    def test_android_overlay_matches_rgba_swap_chain(self):
        result = self.compile("-D__ANDROID__")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_desktop_overlay_matches_bgra_swap_chain(self):
        result = self.compile("-U__ANDROID__")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
