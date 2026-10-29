"""Run RT64's frame-matching call pairing against the call-by-call std::multimap/std::set loop."""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.apply_patches import series_for

ROOT = Path(__file__).resolve().parents[1]
RT64 = ROOT / "lib/rt64"


def between(source, start, end):
    first = source.index(start)
    return source[first:source.index(end, first)]


@unittest.skipUnless((RT64 / ".git").exists(), "RT64 submodule not initialized")
@unittest.skipUnless(shutil.which("g++"), "g++ needed for frame matching fixture")
class FrameMatchingTests(unittest.TestCase):
    def test_joined_call_keys_add_the_call_by_call_check_pairs(self):
        with tempfile.TemporaryDirectory(prefix="lambo-matching-") as temporary:
            tree = Path(temporary)
            for name in ("rt64_game_frame.cpp", "rt64_game_frame.h"):
                dest = tree / "src/hle" / name
                dest.parent.mkdir(parents=True, exist_ok=True)
                dest.write_bytes(subprocess.check_output(
                    ["git", "show", f"HEAD:src/hle/{name}"], cwd=RT64))
            for patch in series_for("android")["lib/rt64"]:
                subprocess.run(["git", "apply", "--ignore-whitespace",
                                "--include=src/hle/rt64_game_frame.*",
                                "--include=src/hle/rt64_game_call_keys.h",
                                str(patch)], cwd=tree,
                               check=True, capture_output=True)
            source = (tree / "src/hle/rt64_game_frame.cpp").read_text(encoding="utf-8")
            # matchScene pairs calls only through the joined keys.
            self.assertIn("pairCallKeys(curCallKeys, prevCallKeys, addTransformPairs, lookAtIndex, tileCheckSet, lookAtCheckSet);", source)
            self.assertNotIn("equal_range", between(source, "void GameFrame::matchScene", "void GameFrame::matchTransform"))
            normalize = between(source, "for (std::vector<IndexPair> *checkSet", "// Compute all the differences")
            fixture = (ROOT / "tests/fixtures/rt64_frame_matching.cpp").read_text(encoding="utf-8")
            fixture = fixture.replace("// PRODUCTION_NORMALIZE", normalize)
            cpp = tree / "fixture.cpp"
            cpp.write_text(fixture, encoding="utf-8")
            exe = tree / "fixture.exe"
            build = subprocess.run(["g++", "-std=c++17", "-O1", "-I", str(tree), str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
