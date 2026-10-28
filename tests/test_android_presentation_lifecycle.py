"""Compile production retirement and guest-clock boundaries without a ROM/GPU."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import ROOT, method, patched_file


@unittest.skipUnless(shutil.which("g++"), "g++ needed for lifecycle fixtures")
class PresentationLifecycleTests(unittest.TestCase):
    def compile_run(self, source):
        with tempfile.TemporaryDirectory(prefix="lambo-lifecycle-") as temporary:
            tree = Path(temporary)
            cpp = tree / "fixture.cpp"
            cpp.write_text(source, encoding="utf-8")
            exe = tree / "fixture.exe"
            built = subprocess.run(["g++", "-std=c++17", str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True, timeout=10)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    @unittest.skipUnless((ROOT / "lib/rt64/.git").exists(), "RT64 not initialized")
    def test_framebuffers_and_semaphores_retire_after_submitted_work(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = patched_file(ROOT / "lib/rt64", "src/hle/rt64_present_queue.cpp", temporary)
        production = method(source, "void PresentQueue::threadResize(")
        fixture = (ROOT / "tests/fixtures/present_retirement.cpp").read_text(encoding="utf-8")
        self.compile_run(fixture.replace("// PRODUCTION_RESIZE", production))

    @unittest.skipUnless((ROOT / "lib/rt64/.git").exists(), "RT64 not initialized")
    def test_teardown_abandons_resources_when_drain_does_not_establish_completion(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = patched_file(ROOT / "lib/rt64", "src/hle/rt64_present_queue.cpp", temporary)
        production = method(source, "PresentQueue::~PresentQueue(")
        if "void PresentQueue::releaseSwapChainResources(" in source:
            production += "\n" + method(source, "void PresentQueue::releaseSwapChainResources(")
        fixture = (ROOT / "tests/fixtures/present_shutdown.cpp").read_text(encoding="utf-8")
        self.compile_run(fixture.replace("// PRODUCTION_TEARDOWN", production))

    @unittest.skipUnless((ROOT / "lib/N64ModernRuntime/.git").exists(), "runtime not initialized")
    def test_suspension_freezes_guest_time_without_resume_catchup(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = patched_file(ROOT / "lib/N64ModernRuntime", "ultramodern/src/timer.cpp", temporary)
        clock = source[source.index("// Start time for the program"):source.index("// Offset of the duration")]
        functions = "\n".join(method(source, signature) for signature in (
            "std::chrono::high_resolution_clock::time_point ultramodern::get_start(",
            "std::chrono::high_resolution_clock::duration ultramodern::time_since_start(",
        ))
        self.compile_run("""
#include <chrono>
#include <mutex>
#include <thread>
#include <cassert>
namespace ultramodern {
void set_app_suspended(bool);
bool is_app_suspended();
std::chrono::high_resolution_clock::time_point get_start();
std::chrono::high_resolution_clock::duration time_since_start();
}
""" + clock + functions + """
int main() {
    using namespace std::chrono_literals;
    for (int i = 0; i < 8; ++i) {
        ultramodern::set_app_suspended(true);
        const auto frozen = ultramodern::time_since_start();
        const auto origin = ultramodern::get_start();
        std::thread reader([&] {
            for (int j = 0; j < 1000; ++j) {
                assert(ultramodern::is_app_suspended());
                assert(ultramodern::time_since_start() == frozen);
            }
        });
        std::this_thread::sleep_for(10ms);
        ultramodern::set_app_suspended(true); // Duplicate events cannot reset pause time.
        reader.join();
        assert(ultramodern::time_since_start() == frozen);
        assert(ultramodern::get_start() - origin >= 10ms);
        const auto beforeResume = std::chrono::high_resolution_clock::now();
        ultramodern::set_app_suspended(false);
        const auto resumed = ultramodern::time_since_start();
        const auto resumeElapsed = std::chrono::high_resolution_clock::now() - beforeResume;
        assert(resumed >= frozen && resumed - frozen <= resumeElapsed);
        ultramodern::set_app_suspended(false);
        std::this_thread::sleep_for(2ms);
        assert(ultramodern::time_since_start() - resumed >= 2ms);
    }
}
""")

    @unittest.skipUnless((ROOT / "lib/N64ModernRuntime/.git").exists(), "runtime not initialized")
    def test_timer_deadlines_do_not_poll_or_sleep_in_foreground(self):
        with tempfile.TemporaryDirectory() as temporary:
            source = patched_file(ROOT / "lib/N64ModernRuntime", "ultramodern/src/timer.cpp", temporary)
        production = source[source.index("// Start time for the program"):source.index("void ultramodern::init_timers(")]
        production += "\n" + "\n".join(method(source, signature) for signature in (
            "std::chrono::high_resolution_clock::time_point ultramodern::get_start(",
            "std::chrono::high_resolution_clock::duration ultramodern::time_since_start(",
        ))
        # Only host boundaries change; the timer loop, action dispatch and deadline
        # conversion execute as built. The fake queue advances the clock deterministically.
        production = production.replace("std::chrono::high_resolution_clock", "FixtureClock")
        production = production.replace("std::this_thread::sleep_for", "fixtureSleep")
        fixture = (ROOT / "tests/fixtures/runtime_timer_suspend.cpp").read_text(encoding="utf-8")
        self.compile_run(fixture.replace("// PRODUCTION_TIMER", production))


if __name__ == "__main__":
    unittest.main()
