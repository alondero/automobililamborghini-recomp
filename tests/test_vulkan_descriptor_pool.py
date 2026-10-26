"""Exercise Plume's allocation path with a layout-accounting Vulkan driver.

No ROM or GPU needed. Compile the production constructor and pool builder
against a recording Vulkan boundary; do not duplicate their sizing logic.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PLUME = ROOT / "lib/rt64/src/contrib/plume"


def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]


@unittest.skipUnless((PLUME / ".git").exists(), "Plume submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for Vulkan allocation fixture")
class DescriptorPoolTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-descriptor-pool-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        # Replay the same source as CMake without mutating the user's checkout.
        source = subprocess.check_output(
            ["git", "show", "HEAD:plume_vulkan.cpp"], cwd=PLUME, text=True)
        (tree / "plume_vulkan.cpp").write_bytes(source.encode("utf-8"))
        for number in ("0014", "0023", "0028", "0041"):
            patches = list((ROOT / "patches").glob(f"{number}-*.patch"))
            if len(patches) != 1:
                raise RuntimeError(f"Expected one patch {number}: {patches}")
            subprocess.run(["git", "apply", "--include=plume_vulkan.cpp", str(patches[0])],
                           cwd=tree, check=True, capture_output=True)
        source = (tree / "plume_vulkan.cpp").read_text(encoding="utf-8")
        fixture = (ROOT / "tests/fixtures/vulkan_descriptor_pool.cpp").read_text(encoding="utf-8")
        for signature in ("VulkanDescriptorSet::VulkanDescriptorSet(",
                          "VkDescriptorPool VulkanDescriptorSet::createDescriptorPool("):
            fixture += "\n" + method(source, signature) + "\n"
        cpp = tree / "fixture.cpp"
        cpp.write_text(fixture, encoding="utf-8")
        cls.exe = tree / "fixture.exe"
        result = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_scenario(self, scenario):
        return subprocess.run([str(self.exe), scenario], capture_output=True, text=True, timeout=10)

    def test_variable_table_retries_with_layout_capacity(self):
        result = self.run_scenario("layout-capacity")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_normal_driver_keeps_small_pool(self):
        result = self.run_scenario("normal")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_retry_preserves_nonzero_variable_count(self):
        result = self.run_scenario("nonzero-count")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_fixed_table_needs_no_retry(self):
        result = self.run_scenario("fixed")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_persistent_failure_stops_before_using_a_null_set(self):
        result = self.run_scenario("persistent-failure")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("descriptor set allocation failed; cannot continue", result.stderr)

    def test_full_capacity_failure_is_not_retried(self):
        result = self.run_scenario("full-capacity-failure")
        self.assertNotEqual(result.returncode, 0)
        self.assertNotIn("retrying", result.stderr)
        self.assertIn("descriptor set allocation failed; cannot continue", result.stderr)


if __name__ == "__main__":
    unittest.main()
