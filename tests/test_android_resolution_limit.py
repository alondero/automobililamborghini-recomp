"""Run RT64's actual automatic/manual scale selection without a ROM or GPU."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.test_vulkan_descriptor_pool import method
from tools.check_patch_series import CMAKE_SERIES, SCRIPT_SERIES, resolve_patch

ROOT = Path(__file__).resolve().parents[1]
RT64 = ROOT / "lib/rt64"


@unittest.skipUnless((RT64 / ".git").exists(), "RT64 submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for scale selection fixture")
class AndroidResolutionTests(unittest.TestCase):
    def test_scale_selection(self):
        with tempfile.TemporaryDirectory(prefix="lambo-resolution-") as temporary:
            tree = Path(temporary)
            for name in ("rt64_workload_queue.cpp", "rt64_workload_queue.h"):
                dest = tree / "src/hle" / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(subprocess.check_output(
                    ["git", "show", f"HEAD:src/hle/{name}"], cwd=RT64))
            for number in SCRIPT_SERIES["android"] + CMAKE_SERIES:
                subprocess.run(["git", "apply", "--ignore-whitespace",
                                "--include=src/hle/rt64_workload_queue.*",
                                str(resolve_patch(ROOT, number))], cwd=tree,
                               check=True, capture_output=True)
            source = (tree / "src/hle/rt64_workload_queue.cpp").read_text(encoding="utf-8")
            # Exercise the production switch, including the caller of the driver
            # workaround. The fixture models only the configuration boundary.
            start = source.index("// Compute the resolution scaling to be used for the frame.")
            end = source.index("uint32_t msaaSampleCount", start)
            selection = source[start:end]
            helper = ""
            for signature in ("static bool needsAdreno615ResolutionLimit(",
                              "static float automaticResolutionScale("):
                if signature in source:
                    helper += method(source, signature) + "\n"
            fixture = (ROOT / "tests/fixtures/android_resolution_limit.cpp").read_text(encoding="utf-8")
            fixture = fixture.replace("// PRODUCTION_HELPER", helper)
            fixture = fixture.replace("// PRODUCTION_SELECTION", selection + "\nreturn resolutionMultiplier;")
            cpp = tree / "fixture.cpp"
            cpp.write_text(fixture, encoding="utf-8")
            for platform in ("android", "desktop"):
                exe = tree / f"{platform}.exe"
                flags = ["-D__ANDROID__"] if platform == "android" else []
                subprocess.run(["g++", "-std=c++17", *flags, str(cpp), "-o", str(exe)],
                               check=True, capture_output=True)
                result = subprocess.run([str(exe)], capture_output=True, text=True)
                self.assertEqual(result.returncode, 0, platform + ": " + result.stderr)


if __name__ == "__main__":
    unittest.main()
