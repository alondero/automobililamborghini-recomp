"""Run Plume's production map/unmap paths against a recording VMA boundary."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import PLUME, ROOT, method, patched_file, patched_plume_vulkan


@unittest.skipUnless((PLUME / ".git").exists(), "Plume submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for Vulkan mapped memory fixture")
class MappedMemoryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-mapped-memory-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        source = patched_plume_vulkan(tree)
        header = patched_file(PLUME, "plume_vulkan.h", tree)
        fixture = (ROOT / "tests/fixtures/vulkan_mapped_memory.cpp").read_text(encoding="utf-8")
        types = patched_file(PLUME, "plume_render_interface_types.h", tree)
        fixture = fixture.replace("// RANGE_DECLARATION", method(types, "struct RenderRange {") + ";")
        shadow = patched_file(ROOT / "lib/rt64", "src/render/rt64_sun_shadow_scene.cpp", tree / "rt64")
        fixture = fixture.replace("// SHADOW_UPLOAD", method(shadow, "bool upload("))
        # Keep the production fields too, without pulling in the entire renderer.
        declaration = method(header, "struct VulkanBuffer : RenderBuffer")
        fixture = fixture.replace("// BUFFER_DECLARATION", declaration.replace(" override", "") + ";")
        fixture += "\n" + method(source, "[[noreturn]] static void failBufferMapping(")
        fixture += "\n" + method(source, "void *VulkanBuffer::map(")
        fixture += "\n" + method(source, "void VulkanBuffer::unmap(")
        cpp = tree / "fixture.cpp"
        cpp.write_text(fixture, encoding="utf-8")
        cls.exe = tree / "fixture.exe"
        command = ["g++", "-std=c++17", "-DNDEBUG", str(cpp), "-o", str(cls.exe)]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(f"Compiler exited {result.returncode}: {subprocess.list2cmdline(command)}\n"
                               f"stdout:\n{result.stdout}\nstderr:\n{result.stderr}")

    def run_scenario(self, scenario, coherent=False):
        return subprocess.run([str(self.exe), scenario, "coherent" if coherent else "noncoherent"],
                              capture_output=True, text=True, timeout=10)

    def test_upload_is_visible_before_submission(self):
        for coherent in (False, True):
            with self.subTest(coherent=coherent):
                result = self.run_scenario("upload-default", coherent)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(),
                                 ["map", "cpu-write", "flush 0 96", "unmap", "submit"])

    def test_readback_is_visible_after_completion(self):
        for coherent in (False, True):
            with self.subTest(coherent=coherent):
                result = self.run_scenario("readback-default", coherent)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(),
                                 ["submit", "wait", "map", "invalidate 0 96", "cpu-read", "unmap"])

    def test_explicit_reads_and_writes_keep_exclusive_byte_ranges_and_base_pointer(self):
        for scenario, offset, size in (("partial", 7, 23), ("full", 0, 96), ("tail", 95, 1),
                                       ("default-explicit", 7, 23), ("readback-explicit-write", 7, 23)):
            for coherent in (False, True):
                with self.subTest(scenario=scenario, coherent=coherent):
                    result = self.run_scenario(scenario, coherent)
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout.splitlines(), ["submit", "wait", "map",
                                     f"invalidate {offset} {size}", "cpu-read", "cpu-write",
                                     f"flush {offset} {size}", "unmap", "submit"])

    def test_empty_ranges_do_not_maintain_caches(self):
        for scenario in ("empty-zero", "empty-middle", "empty-end", "readback-empty"):
            with self.subTest(scenario=scenario):
                result = self.run_scenario(scenario)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), ["map", "unmap"])

    def test_gpu_upload_defaults_to_write_only(self):
        result = self.run_scenario("gpu-upload-default")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["map", "cpu-write", "flush 0 96", "unmap", "submit"])

    def test_nested_maps_balance_vma_references(self):
        result = self.run_scenario("nested")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout.splitlines(),
                         ["map", "map", "flush 1 1", "unmap", "flush 2 1", "unmap"])

    def test_invalid_reads_stop_before_mapping(self):
        for suffix in ("reversed", "end", "empty", "overflow", "subresource"):
            with self.subTest(suffix=suffix):
                result = self.run_scenario("invalid-read-" + suffix)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid read range, subresource or allocation; cannot continue", result.stderr)
                self.assertEqual(result.stdout, "")
        result = self.run_scenario("invalid-allocation")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("invalid read range, subresource or allocation", result.stderr)
        self.assertEqual(result.stdout, "")

    def test_nested_read_can_discard_unflushed_bytes_outside_read_range(self):
        # Characterise a permitted outcome of invalidating dirty atoms. If the
        # mapping contract gains automatic protection, update this expectation.
        for coherent in (False, True):
            with self.subTest(coherent=coherent):
                result = self.run_scenario("nested-dirty-atom", coherent)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), ["map", "cpu-write", "submit", "wait",
                                 "map", "invalidate 17 1", "cpu-read", "unmap", "unmap"])

    def test_flushing_before_read_remap_preserves_other_bytes_in_atom(self):
        for coherent in (False, True):
            with self.subTest(coherent=coherent):
                result = self.run_scenario("remap-flushed-atom", coherent)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), ["map", "cpu-write", "flush 1 1", "unmap",
                                 "submit", "wait", "map", "invalidate 17 1", "cpu-read", "unmap"])

    def test_invalid_writes_release_mapping_before_stopping(self):
        for suffix in ("reversed", "end", "empty", "overflow", "subresource"):
            with self.subTest(suffix=suffix):
                result = self.run_scenario("invalid-write-" + suffix)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("invalid written range or subresource; cannot continue", result.stderr)
                self.assertEqual(result.stdout.splitlines(), ["map", "unmap"])

    def test_unmatched_unmaps_never_reach_vma(self):
        for scenario, events in (("unmatched-unmap", []), ("double-unmap", ["map", "unmap"])):
            with self.subTest(scenario=scenario):
                result = self.run_scenario(scenario)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("unmap without a successful map; cannot continue", result.stderr)
                self.assertEqual(result.stdout.splitlines(), events)

    def test_map_failures_return_null_and_balance_successful_maps(self):
        for operation, events in (("map", ["map"]),
                                  ("invalidate", ["map", "invalidate 0 96", "unmap"])):
            with self.subTest(operation=operation):
                result = self.run_scenario(operation + "-failure")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertIn({"map": "vmaMapMemory", "invalidate": "vmaInvalidateAllocation",
                               }[operation] + " failed with error code", result.stderr)
                self.assertEqual(result.stdout.splitlines(), events)

    def test_flush_failure_releases_mapping_and_stops_submission(self):
        result = self.run_scenario("flush-failure")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("vmaFlushAllocation failed (VkResult=", result.stderr)
        self.assertIn("cannot continue", result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["map", "cpu-write", "flush 0 96", "unmap"])

    def test_nested_map_failure_preserves_the_existing_mapping(self):
        for operation, events in (("map", ["map", "map", "unmap"]),
                                  ("invalidate", ["map", "map", "invalidate 0 96", "unmap", "unmap"])):
            with self.subTest(operation=operation):
                result = self.run_scenario("nested-" + operation + "-failure")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout.splitlines(), events)

    def test_shadow_upload_returns_false_when_mapping_fails(self):
        result = self.run_scenario("shadow-upload-failure")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("vmaMapMemory failed", result.stderr)
        self.assertEqual(result.stdout.splitlines(), ["map"])


if __name__ == "__main__":
    unittest.main()
