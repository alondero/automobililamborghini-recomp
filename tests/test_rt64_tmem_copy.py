"""Compare patched TMEM loads with the pinned byte loop, including texture hashes."""
import os
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tests.patched_source import patched_file

ROOT = Path(__file__).resolve().parents[1]
RT64 = ROOT / "lib/rt64"


def write_fixture(tree):
    """Also usable for cross-compiling the identical fixture for an Android device."""
    # git apply otherwise skips these relative paths if a caller puts the
    # replay tree inside the port's own checkout (for example under artifacts).
    subprocess.run(["git", "init", "-q", str(tree)], check=True, capture_output=True)
    baseline = subprocess.check_output(
        ["git", "show", "HEAD:src/hle/rt64_rdp.cpp"], cwd=RT64, text=True)
    current = patched_file(RT64, "src/hle/rt64_rdp.cpp", tree)
    if "auto reverseBytes" not in current:
        raise RuntimeError("TMEM copy patch was not replayed")
    patched_file(RT64, "src/common/rt64_tmem_hasher.h", tree)
    start = "    template<bool RGBA32 = false, bool TLUT = false>"
    end = "    void RDP::loadTileOperation"

    def helpers(source):
        first = source.index(start)
        return source[first:source.index(end, first)].replace("__forceinline", "inline")

    fixture = (ROOT / "tests/fixtures/rt64_tmem_copy.cpp").read_text(encoding="utf-8")
    fixture = fixture.replace("// BASELINE_LOADS", helpers(baseline))
    fixture = fixture.replace("// PRODUCTION_LOADS", helpers(current))
    cpp = tree / "fixture.cpp"
    cpp.write_text(fixture, encoding="utf-8")
    return cpp


@unittest.skipUnless((RT64 / ".git").exists(), "RT64 submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for TMEM fixture")
class TMEMCopyTests(unittest.TestCase):
    def test_all_word_alignments_load_sequences_and_hash_versions(self):
        with tempfile.TemporaryDirectory(prefix="lambo-tmem-") as temporary:
            tree = Path(temporary)
            cpp = write_fixture(tree)
            exe = tree / ("fixture.exe" if os.name == "nt" else "fixture")
            build = subprocess.run([
                "g++", "-std=c++17", "-O2", "-Wall", "-Wextra",
                "-I", str(tree / "src"), "-I", str(RT64 / "src"),
                "-I", str(RT64 / "src/contrib"),
                str(cpp), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("TMEM equivalence passed", result.stdout)


if __name__ == "__main__":
    unittest.main()
