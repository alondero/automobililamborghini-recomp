"""Patch-composition regression: every platform's rt64 patch order must apply cleanly.

Background: the v0.8.0 release builds broke on two platforms at once while
the local Windows build stayed green. Linux failed to compile because patch
0024 referenced a Windows-only DXIL shader blob without a platform guard, and
Android failed to configure because patch 0013 (Android-only) inserts a blank
line that patch 0030's DXC hunk requires to be absent. Both slipped through
because each platform applies a different patch subset before the shared
CMake series.

The tests below replay each platform's exact rt64 order — script patches
(with ``--ignore-whitespace``, as the scripts apply them), then the CMake
series from CMakeLists.txt (plain ``git apply``, as lambo_frontend_patch
does) — inside a disposable worktree of the pinned rt64 submodule, and lock
in the platform guard for DXIL-blob uses. Platform orders mirror build.ps1
(Windows), build.sh (Linux), and scripts/build_android.py (Android); the
CMake series is shared. Plume patches (0004/0014/0023/0028) are out of scope:
they target the nested plume submodule identically on all platforms, so they
cannot cause platform divergence.
"""
import re
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path
from scripts import build_android as android
from tools.check_patch_series import SCRIPT_SERIES, CMAKE_SERIES, validate

ROOT = Path(__file__).resolve().parents[1]
PATCHES_DIR = ROOT / "patches"
RT64_DIR = ROOT / "lib" / "rt64"

class PatchWiringTests(unittest.TestCase):
    def test_replay_matrix_matches_build_inputs(self):
        validate(ROOT)


def resolve_patch(number):
    matches = sorted(PATCHES_DIR.glob(f"{number}-*.patch"))
    assert len(matches) == 1, f"expected one patch for {number}: {matches}"
    return matches[0]


@unittest.skipUnless(
    (ROOT / "lib" / "rt64" / ".git").exists(), "lib/rt64 submodule not initialized"
)
class PatchCompositionTests(unittest.TestCase):
    def replay(self, platform, cmake_series=CMAKE_SERIES):
        """Apply a platform's full rt64 order in a throwaway worktree."""
        sha = subprocess.check_output(
            ["git", "rev-parse", "HEAD:lib/rt64"], cwd=ROOT, text=True
        ).strip()
        tmp = tempfile.TemporaryDirectory(prefix="lambo-patch-compose-")
        self.addCleanup(tmp.cleanup)
        tree = Path(tmp.name) / "rt64"
        self.git("-C", str(RT64_DIR), "worktree", "add", "--detach", str(tree), sha)
        self.addCleanup(
            subprocess.run,
            ["git", "-C", str(RT64_DIR), "worktree", "remove", "--force", str(tree)],
            cwd=ROOT,
            capture_output=True,
        )
        ordered = [resolve_patch(n) for n in SCRIPT_SERIES[platform]]
        for patch in ordered:
            self.apply(tree, patch, ["--ignore-whitespace"], platform)
        for number in cmake_series:
            self.apply(tree, resolve_patch(number), [], platform)
        return tree

    def apply(self, tree, patch, flags, platform):
        check = subprocess.run(
            ["git", "-C", str(tree), "apply", *flags, "--check", str(patch)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            check.returncode,
            0,
            f"{platform}: {patch.name} does not apply onto its predecessors:\n"
            + check.stderr,
        )
        apply = subprocess.run(
            ["git", "-C", str(tree), "apply", *flags, str(patch)],
            capture_output=True,
            text=True,
        )
        self.assertEqual(
            apply.returncode, 0, f"{platform}: applying {patch.name} failed:\n" + apply.stderr
        )

    def git(self, *args):
        return subprocess.check_output(
            ["git", *args], cwd=ROOT, stderr=subprocess.STDOUT
        )

    def test_windows_series_composes(self):
        self.replay("windows")

    def test_linux_series_composes(self):
        self.replay("linux")

    def test_android_series_composes(self):
        self.replay("android")

    def snapshot(self, tree):
        changed = self.git("-C", str(tree), "diff", "--name-only", "-z", "HEAD")
        untracked = self.git("-C", str(tree), "ls-files", "--others", "--exclude-standard", "-z")
        files = {name: (tree / name.decode()).read_bytes()
                 for name in (changed + untracked).split(b"\0") if name}
        index = Path(self.git("-C", str(tree), "rev-parse", "--git-path", "index").decode().strip())
        return files, index.read_bytes()

    def prepare_android(self, tree):
        android.apply_patch_series(
            tree, [resolve_patch(n) for n in SCRIPT_SERIES["android"]],
            [resolve_patch(n) for n in CMAKE_SERIES])

    def test_android_preparation_preserves_each_composed_cmake_prefix(self):
        tree = self.replay("android", cmake_series=[])
        # Keep staged, unstaged and untracked developer files through every
        # preparation, including the pre-0042 series reported in issue 351.
        personal = tree / "personal.txt"
        personal.write_bytes(b"keep my staged file\n")
        self.git("-C", str(tree), "add", "personal.txt", "src/hle/rt64_game_frame.cpp")
        personal.write_bytes(b"keep my unstaged edit\r\n")
        (tree / "notes.txt").write_bytes(b"keep my untracked notes\r\n")
        for number in CMAKE_SERIES:
            with self.subTest(cmake_patch=number):
                self.apply(tree, resolve_patch(number), [], "android")
                before = self.snapshot(tree)
                self.prepare_android(tree)
                self.assertEqual(self.snapshot(tree), before)
        # Also exercise a second preparation of the complete current series.
        self.prepare_android(tree)
        self.assertEqual(self.snapshot(tree), before)

    def test_android_preparation_rejects_drift_despite_later_patch(self):
        tree = self.replay("android")
        for relative, old, new in (
            ("src/hle/rt64_game_frame.cpp", b"#include <chrono>", b"// local edit"),
            ("CMakeLists.txt", b"set(DXC ${LAMBO_HOST_DXC})", b"set(DXC local-dxc)"),
            ("src/hle/rt64_workload_queue.cpp", b"== 0x801f6000", b"== 0x12345678"),
        ):
            with self.subTest(file=relative):
                path = tree / relative
                original = path.read_bytes()
                self.assertIn(old, original)
                path.write_bytes(original.replace(old, new, 1))
                self.git("-C", str(tree), "add", relative)
                # A marker-only guard would accept this incomplete series.
                self.git("-C", str(tree), "apply", "--reverse", "--check", str(resolve_patch("0039")))
                before = self.snapshot(tree)
                with self.assertRaisesRegex(RuntimeError, "Patch series conflicts"):
                    self.prepare_android(tree)
                self.assertEqual(self.snapshot(tree), before)
                path.write_bytes(original)


@unittest.skipUnless(
    (ROOT / "lib" / "rt64" / "src" / "contrib" / "plume" / ".git").exists()
    and shutil.which("cmake"),
    "plume submodule or cmake unavailable",
)
class PlumeRepeatConfigureTests(unittest.TestCase):
    """build.ps1 configures twice, so the plume patch steps must run twice."""

    def plume_steps(self):
        frontend = (ROOT / "cmake" / "Frontend.cmake").read_text(encoding="utf-8")
        function = re.search(r"find_package\(Git REQUIRED\).*?endfunction\(\)", frontend, re.S)
        cmake = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
        steps = re.search(r"include\(cmake/Frontend\.cmake\)\n(.*?)\n# Later RT64 patches", cmake, re.S)
        self.assertTrue(function and steps, "patch step markers moved")
        self.assertIn("contrib/plume", steps.group(1))
        return function.group(0) + "\n" + steps.group(1) + "\n"

    def test_second_configure_accepts_applied_plume_series(self):
        sha = subprocess.check_output(
            ["git", "rev-parse", "HEAD:src/contrib/plume"], cwd=RT64_DIR, text=True
        ).strip()
        tmp = tempfile.TemporaryDirectory(prefix="lambo-plume-configure-")
        self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        shutil.copytree(PATCHES_DIR, root / "patches")
        tree = root / "lib" / "rt64" / "src" / "contrib" / "plume"
        plume = RT64_DIR / "src" / "contrib" / "plume"
        subprocess.check_call(["git", "-C", str(plume), "worktree", "add", "--detach", str(tree), sha],
                              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        self.addCleanup(subprocess.run, ["git", "-C", str(plume), "worktree", "remove", "--force", str(tree)],
                        capture_output=True)
        script = root / "plume_patches.cmake"
        script.write_text(self.plume_steps(), encoding="utf-8")
        for run in ("first", "second"):
            result = subprocess.run(["cmake", "-P", str(script)], cwd=root, capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, f"{run} configure:\n{result.stderr}")
        self.assertTrue(subprocess.check_output(["git", "-C", str(tree), "diff", "--stat"]))


class DxilGuardTests(unittest.TestCase):
    def test_dxil_blob_use_arrives_with_platform_guard(self):
        # DXIL shader blobs are only generated (and declared) on Windows —
        # rt64's CMake builds them under if(WIN32). Any patch hunk that
        # references a BlobDXIL symbol must bring its own _WIN32 guard, or
        # non-Windows builds fail with "not declared in this scope".
        patch = resolve_patch("0024")
        hunks, current = [], []
        for line in patch.read_text(encoding="utf-8").splitlines():
            if line.startswith("@@ "):
                hunks.append(current)
                current = []
            current.append(line)
        hunks.append(current)
        unguarded = [
            i for i, hunk in enumerate(hunks)
            if any("BlobDXIL" in line for line in hunk if line.startswith("+"))
            and not any("_WIN32" in line for line in hunk if line.startswith("+"))
        ]
        self.assertEqual(
            unguarded,
            [],
            "patch 0024 adds a BlobDXIL reference without a _WIN32 guard",
        )


if __name__ == "__main__":
    unittest.main()
