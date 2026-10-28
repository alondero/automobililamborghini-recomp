"""Plume's Vulkan swap chain honors surface capabilities (issue #357).

No ROM or GPU needed. Compile Plume's patched VulkanSwapChain declaration and
implementation against a recording Vulkan boundary. The window-system surface
block and getWindowSize are the only replaced parts. The fake driver enforces
the VkSwapchainCreateInfoKHR contract and the Android loader's rule that an
oldSwapchain is retired even when its replacement fails.

The implementation is compiled twice, once with its Android branches (#329).
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import PLUME, ROOT, method, patched_file, patched_plume_vulkan

SCENARIOS = (
    "android-defaults", "query-failure-create", "query-failure-resize", "fixed-extent",
    "zero-current-extent", "surface-extent-lag", "fixed-bounds",
    "variable-extent", "zero-window", "zero-surface-extent", "unbounded-count", "bounded-count",
    "minimum-count", "count-stable", "usage-color-only", "alpha-opaque", "alpha-inherit",
    "alpha-premultiplied", "alpha-postmultiplied", "transform-refusal", "replacement-failure",
    "abandoned-surface", "short-image-count", "present-mode",
)
# Android 15 on an Adreno 730: the format query leaves gralloc's lock with its thread.
ANDROID_SCENARIOS = SCENARIOS + ("gralloc-probe-lock",)
LIFECYCLE_SCENARIOS = (
    "native-replacement", "native-unavailable", "native-create-failure",
    "native-repeated", "native-timeout", "native-present-loss",
    "native-zero-size", "native-shutdown-unavailable",
    "native-present-mode-change",
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
        section = swap_chain_section(source)
        cls.exe = cls.build(tree / "fixture", header, section)
        # The surface and window-size blocks are already replaced, so the define reaches only the
        # Android branches of the shared swap chain code.
        cls.android_exe = cls.build(tree / "fixture-android", header,
                                    "#define __ANDROID__ 1\n" + section + "\n#undef __ANDROID__")
        cls.lifecycle_exe = cls.build(tree / "fixture-lifecycle", header,
                                      section, "#define __ANDROID__ 1\n#define PLUME_SDL_VULKAN_ENABLED 1\n")

    @staticmethod
    def build(stem, header, section, defines=""):
        fixtures = ROOT / "tests/fixtures"
        # Exercise the production caller as well as Plume's Vulkan boundary.
        # In particular a failed acquisition does not necessarily invalidate a chain.
        present = patched_file(ROOT / "lib/rt64", "src/hle/rt64_present_queue.cpp", stem.parent / "rt64")
        acquire = method(present, "bool PresentQueue::threadAcquire(")
        acquire = acquire.replace("PresentQueue::threadAcquire", "Harness::acquire").replace("ext.swapChain", "swapChain")
        scenarios = (fixtures / "vulkan_swapchain_scenarios.cpp").read_text(encoding="utf-8")
        scenarios = scenarios.replace("// PRODUCTION_ACQUIRE", acquire)
        cpp = stem.with_suffix(".cpp")
        cpp.write_text("\n".join([
            (fixtures / "vulkan_swapchain.cpp").read_text(encoding="utf-8"),
            defines,
            "namespace plume {",
            swap_chain_declaration(header),
            section,
            "}",
            scenarios,
        ]), encoding="utf-8")
        exe = stem.with_suffix(".exe")
        # Undefine the platform macros so only the fixture's surface path is compiled.
        result = subprocess.run(["g++", "-std=c++17", "-U__APPLE__", str(cpp), "-o", str(exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)
        return exe

    def run_scenario(self, scenario, exe=None):
        result = subprocess.run([str(exe or self.exe), scenario], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, f"{scenario}\n{result.stdout}\n{result.stderr}")
        return result

    def test_scenarios(self):
        for scenario in SCENARIOS:
            with self.subTest(scenario=scenario):
                self.run_scenario(scenario)

    def test_android_scenarios(self):
        for scenario in ANDROID_SCENARIOS:
            with self.subTest(scenario=scenario):
                self.run_scenario(scenario, self.android_exe)

    def test_android_native_surface_lifecycle(self):
        for scenario in LIFECYCLE_SCENARIOS:
            with self.subTest(scenario=scenario):
                self.run_scenario(scenario, self.lifecycle_exe)

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

    def test_device_loss_drain_and_destructor_do_not_abort(self):
        for exe in (self.exe, self.lifecycle_exe):
            with self.subTest(exe=exe.name):
                log = self.run_scenario("drain-device-lost", exe).stderr
                self.assertIn("Vulkan presentation drain failed: 0xFFFFFFFC", log)

    def test_teardown_abandons_gpu_objects_after_memory_error(self):
        for exe in (self.exe, self.lifecycle_exe):
            for scenario in ("drain-device-memory", "drain-host-memory"):
                with self.subTest(exe=exe.name, scenario=scenario):
                    self.run_scenario(scenario, exe)

    def test_failed_drains_log_once_per_result_and_skip_refresh_queries(self):
        for exe in (self.exe, self.lifecycle_exe):
            with self.subTest(exe=exe.name):
                log = self.run_scenario("drain-failure-logging", exe).stderr
                self.assertEqual(log.count("Vulkan presentation drain failed: 0xFFFFFFFC"), 2, log)
                self.assertEqual(log.count("Vulkan presentation drain failed: 0xFFFFFFFE"), 1, log)
                self.assertNotIn("vkGetRefreshCycleDurationGOOGLE failed", log)

    def test_refresh_failures_log_once_per_result(self):
        for exe in (self.exe, self.lifecycle_exe):
            with self.subTest(exe=exe.name):
                log = self.run_scenario("refresh-failure-logging", exe).stderr
                self.assertEqual(log.count("vkGetRefreshCycleDurationGOOGLE failed with error code 0xFFFFFFFC"), 2, log)
                self.assertEqual(log.count("vkGetRefreshCycleDurationGOOGLE failed with error code 0xFFFFFFFE"), 1, log)

    def test_surface_recovery_failures_log_once_per_reason_and_result(self):
        log = self.run_scenario("native-recovery-failures", self.lifecycle_exe).stderr
        for reason in (
            "vkCreateAndroidSurfaceKHR failed (0xC4653600)",
            "vkCreateAndroidSurfaceKHR failed (0xFFFFFFFE)",
            "vkGetPhysicalDeviceSurfaceSupportKHR failed (0xC4653600)",
            "the Android surface cannot be presented to (0x0)",
            "the Android surface changed during recovery (0x0)",
        ):
            self.assertEqual(log.count(reason), 1, log)
        self.assertEqual(log.count("the Android native window is unavailable (0x0)"), 2, log)


if __name__ == "__main__":
    unittest.main()
