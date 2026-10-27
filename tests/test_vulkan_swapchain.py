"""Plume's Vulkan swap chain honors surface capabilities (issue #357).

No ROM or GPU needed. Compile Plume's patched VulkanSwapChain declaration and
implementation against a recording Vulkan boundary. The window-system surface
block and getWindowSize are the only replaced parts. The fake driver enforces
the VkSwapchainCreateInfoKHR contract and the Android loader's rule that an
oldSwapchain is retired even when its replacement fails.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import PLUME, ROOT, method, patched_file, patched_plume_vulkan

SCENARIOS = (
    "android-defaults", "query-failure-create", "query-failure-resize", "fixed-extent",
    "zero-current-extent", "fixed-bounds",
    "variable-extent", "zero-window", "zero-surface-extent", "unbounded-count", "bounded-count",
    "minimum-count", "count-stable", "usage-color-only", "alpha-opaque", "alpha-inherit",
    "alpha-premultiplied", "alpha-postmultiplied", "transform-refusal", "replacement-failure",
    "short-image-count", "present-mode",
)


def swap_chain_section(source):
    """Return Plume's swap chain implementation with the surface block stubbed."""
    section = source[source.index("    // VulkanSwapChain\n"):source.index("    // VulkanFramebuffer\n")]
    section = section.replace(method(section, "void VulkanSwapChain::getWindowSize("), "")
    start = section.index("#   ifdef _WIN64")
    end = section.index("#   endif", start) + len("#   endif")
    return section[:start] + "surface = fixtureCreateSurface();\n        (void)res;" + section[end:]


def swap_chain_declaration(header):
    start = header.index("    struct VulkanSwapChain : RenderSwapChain {")
    return header[start:header.index("\n    };\n", start) + len("\n    };\n")]


@unittest.skipUnless((PLUME / ".git").exists(), "Plume submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for Vulkan swap chain fixture")
class SwapChainCapabilityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-swapchain-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        source = patched_plume_vulkan(tree)
        header = patched_file(PLUME, "plume_vulkan.h", tree)
        fixtures = ROOT / "tests/fixtures"
        cpp = tree / "fixture.cpp"
        cpp.write_text("\n".join([
            (fixtures / "vulkan_swapchain.cpp").read_text(encoding="utf-8"),
            "namespace plume {",
            swap_chain_declaration(header),
            swap_chain_section(source),
            "}",
            (fixtures / "vulkan_swapchain_scenarios.cpp").read_text(encoding="utf-8"),
        ]), encoding="utf-8")
        cls.exe = tree / "fixture.exe"
        # Undefine the platform macros so only the fixture's surface path is compiled.
        result = subprocess.run(["g++", "-std=c++17", "-U__APPLE__", str(cpp), "-o", str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_scenario(self, scenario):
        result = subprocess.run([str(self.exe), scenario], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, f"{scenario}\n{result.stdout}\n{result.stderr}")
        return result

    def test_scenarios(self):
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario):
                self.run_scenario(scenario)

    def test_creation_records_capabilities_and_choices(self):
        log = self.run_scenario("android-defaults").stderr
        self.assertIn("Vulkan surface capabilities: extent current 1280x720 min 1x1 max 4096x4096", log)
        self.assertIn("Vulkan swap chain created: extent 1280x720 (window 1280x720)", log)
        self.assertIn("usage 0x10", log)
        self.assertIn("alpha 0x8", log)

    def test_refusals_explain_the_surface(self):
        self.assertIn("identity transform", self.run_scenario("transform-refusal").stderr)
        self.assertIn("composite alpha", self.run_scenario("alpha-premultiplied").stderr)
        self.assertIn("vkGetPhysicalDeviceSurfaceCapabilitiesKHR failed",
                      self.run_scenario("query-failure-create").stderr)

    def test_repeated_failure_is_logged_once(self):
        log = self.run_scenario("replacement-failure").stderr
        self.assertEqual(log.count("vkCreateSwapchainKHR failed"), 1, log)


if __name__ == "__main__":
    unittest.main()
