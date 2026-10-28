"""Run the present stall report on a connected Android device.

Optional: needs NDK 28.2 and one `adb` device. The host-side decoding is
covered by tests/test_stall_report.cpp on every platform.
"""
import os
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from scripts.build_android import NDK_VERSION

ROOT = Path(__file__).resolve().parents[1]
SDK = Path(os.environ.get("ANDROID_HOME") or Path.home() / "AppData/Local/Android/Sdk")
HOST = "windows-x86_64" if os.name == "nt" else "linux-x86_64"
CLANG = SDK / "ndk" / NDK_VERSION / "toolchains/llvm/prebuilt" / HOST / "bin" / (
    "clang++.exe" if os.name == "nt" else "clang++")
ADB = shutil.which("adb") or str(SDK / "platform-tools" / ("adb.exe" if os.name == "nt" else "adb"))
REMOTE = "/data/local/tmp/lambo_stall_report_probe"


def device_attached():
    try:
        output = subprocess.run([ADB, "devices"], capture_output=True, text=True, timeout=10).stdout
    except (OSError, subprocess.TimeoutExpired):
        return False
    return len([line for line in output.splitlines()[1:] if line.endswith("\tdevice")]) == 1


@unittest.skipUnless(CLANG.exists(), f"NDK {NDK_VERSION} needed for the device probe")
@unittest.skipUnless(device_attached(), "exactly one adb device needed")
class AndroidStallReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        with tempfile.TemporaryDirectory(prefix="lambo-stall-probe-") as temporary:
            binary = Path(temporary) / "probe"
            subprocess.run([str(CLANG), "--target=aarch64-linux-android28", "-std=c++20", "-static-libstdc++",
                            "-I", str(ROOT / "src"), str(ROOT / "tests/android/stall_report_probe.cpp"),
                            str(ROOT / "src/lambo_stall_report.cpp"), "-o", str(binary)],
                           check=True, capture_output=True)
            subprocess.run([ADB, "push", str(binary), REMOTE], check=True, capture_output=True)

    @classmethod
    def tearDownClass(cls):
        subprocess.run([ADB, "shell", "rm", "-f", REMOTE], capture_output=True)

    def test_report_names_priority_inheritance_holder(self):
        self.assertIn("dumpable after report 1", self.assert_names_holder(""))

    # Issue 329: a release app is non-dumpable, so the kernel gives its owner-only
    # /proc files (syscall, mem) to root and the report lost every lock word.
    def test_non_dumpable_process_still_names_holder(self):
        self.assertIn("dumpable after report 0", self.assert_names_holder("undumpable"))

    # Issue 329: the present thread waited on gralloc's lock while the graphics thread held it.
    def test_blocked_probe_names_holder(self):
        output = self.run_probe("held-probe")
        holder = re.search(r"holder tid (\d+)", output).group(1)
        self.assertIn("Held lock check: no answer after 500 ms. Probe wait:", output)
        line = next((line for line in output.splitlines() if "'Lambo Probe'" in line), "")
        self.assertIn("futex LOCK_PI", line, output)
        self.assertIn(f"owner={holder} holder: tid {holder} 'PI Holder'", line, output)
        self.assertIn("answered 0 dumpable after check 0", output)

    def test_allocator_check_answers(self):
        output = self.run_probe("allocator")
        self.assertRegex(output, r"Graphics buffer check on device \(tid \d+\): answered in \d+ ms\.")

    def run_probe(self, mode):
        result = subprocess.run([ADB, "shell", f"chmod 755 {REMOTE} && {REMOTE} {mode}"],
                                capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        return output

    def assert_names_holder(self, mode):
        result = subprocess.run([ADB, "shell", f"chmod 755 {REMOTE} && {REMOTE} {mode}"],
                                capture_output=True, text=True, timeout=30)
        output = result.stdout + result.stderr
        self.assertEqual(result.returncode, 0, output)
        holder = re.search(r"holder tid (\d+)", output).group(1)
        waiter = re.search(r"waiter tid (\d+)", output).group(1)
        self.assertIn("Present stall: no frame for 3000 ms after 1 frames.", output)
        line = next((line for line in output.splitlines() if line.strip().startswith(f"tid {waiter} ")), "")
        self.assertIn("'PI Waiter'", line, output)
        self.assertIn("futex LOCK_PI", line, output)
        self.assertIn(f"owner={holder} holder: tid {holder} 'PI Holder'", line, output)
        self.assertIn("End of present stall report.", output)
        return output


if __name__ == "__main__":
    unittest.main()
