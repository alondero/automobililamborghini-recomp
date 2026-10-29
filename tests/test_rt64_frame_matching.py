"""Run RT64's frame-matching container handling against std::multimap/std::set."""
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
    def test_sorted_vectors_keep_order_without_allocating(self):
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
                                str(patch)], cwd=tree,
                               check=True, capture_output=True)
            header = (tree / "src/hle/rt64_game_frame.h").read_text(encoding="utf-8")
            source = (tree / "src/hle/rt64_game_frame.cpp").read_text(encoding="utf-8")
            # The production statements, in the order matchScene runs them.
            types = between(header, "typedef std::vector<std::pair<uint64_t, GameCallMap>> GameCallHashMap;", "\n")
            sort = between(source, "const auto hashLess", "// FIXME: Transform set needs")
            lookup = between(source, "auto prevRange =", "\n")
            normalize = between(source, "for (std::vector<IndexPair> *checkSet", "// Compute all the differences")
            fixture = (ROOT / "tests/fixtures/rt64_frame_matching.cpp").read_text(encoding="utf-8")
            fixture = fixture.replace("// PRODUCTION_TYPES", types)
            fixture = fixture.replace("// PRODUCTION_SORT", sort)
            fixture = fixture.replace("// PRODUCTION_RANGE", lookup)
            fixture = fixture.replace("// PRODUCTION_NORMALIZE", normalize)
            cpp = tree / "fixture.cpp"
            cpp.write_text(fixture, encoding="utf-8")
            exe = tree / "fixture.exe"
            build = subprocess.run(["g++", "-std=c++17", "-O1", str(cpp), "-o", str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            result = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
