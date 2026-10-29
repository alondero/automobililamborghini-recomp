"""Run RT64's actual queue barrier waits without a ROM or GPU."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.test_vulkan_descriptor_pool import method
from tools.apply_patches import series_for

ROOT = Path(__file__).resolve().parents[1]
RT64 = ROOT / "lib/rt64"
SOURCES = ("rt64_present_queue.cpp", "rt64_workload_queue.cpp")
METHODS = ("void PresentQueue::advanceToNextPresent(",
           "void PresentQueue::threadAdvanceBarrier(",
           "void WorkloadQueue::advanceToNextWorkload(",
           "void WorkloadQueue::threadAdvanceBarrier(")


@unittest.skipUnless((RT64 / ".git").exists(), "RT64 submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for queue barrier fixture")
class QueueBarrierTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.temporary = tempfile.TemporaryDirectory(prefix="lambo-queue-barrier-")
        cls.addClassCleanup(cls.temporary.cleanup)
        tree = Path(cls.temporary.name)
        for name in SOURCES:
            destination = tree / "src/hle" / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            destination.write_bytes(subprocess.check_output(
                ["git", "show", f"HEAD:src/hle/{name}"], cwd=RT64))
        # Android carries every RT64 patch the desktop builds do, plus 0013.
        for patch in series_for("android")["lib/rt64"]:
            subprocess.run(["git", "apply", "--ignore-whitespace",
                            *[f"--include=src/hle/{name}" for name in SOURCES],
                            str(patch)], cwd=tree,
                           check=True, capture_output=True)
        source = "".join((tree / "src/hle" / name).read_text(encoding="utf-8") for name in SOURCES)
        production = "\n".join(method(source, signature) for signature in METHODS)
        fixture = (ROOT / "tests/fixtures/rt64_queue_barrier.cpp").read_text(encoding="utf-8")
        cpp = tree / "fixture.cpp"
        cpp.write_text(fixture.replace("// PRODUCTION_QUEUE_METHODS", production), encoding="utf-8")
        cls.exe = tree / "fixture.exe"
        result = subprocess.run(["g++", "-std=c++17", "-O1", "-pthread", str(cpp), "-o", str(cls.exe)],
                                capture_output=True, text=True)
        if result.returncode:
            raise RuntimeError(result.stderr)

    def run_queue(self, queue):
        return subprocess.run([str(self.exe), queue], capture_output=True, text=True, timeout=10)

    def test_present_producer_sleeps_until_barrier_advances(self):
        result = self.run_queue("present")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_workload_producer_sleeps_until_barrier_advances(self):
        result = self.run_queue("workload")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_present_producer_wakes_on_shutdown(self):
        result = self.run_queue("present-shutdown")
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_workload_producer_wakes_on_shutdown(self):
        result = self.run_queue("workload-shutdown")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
