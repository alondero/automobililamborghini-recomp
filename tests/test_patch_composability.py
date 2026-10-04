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
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PATCHES_DIR = ROOT / "patches"
RT64_DIR = ROOT / "lib" / "rt64"

# Script-applied rt64 patches per platform, in application order.
SCRIPT_SERIES = {
    "windows": ["0006", "0005", "0009", "0010", "0011"],
    "linux": ["0006", "0009", "0010", "0011"],
    "android": ["0006", "0009", "0010", "0011", "0013"],
}
# CMake-applied rt64 patches (CMakeLists.txt order, all platforms).
CMAKE_SERIES = ["0021", "0022", "0024", "0025", "0026", "0027", "0029", "0030"]


def resolve_patch(number):
    matches = sorted(PATCHES_DIR.glob(f"{number}-*.patch"))
    assert len(matches) == 1, f"expected one patch for {number}: {matches}"
    return matches[0]


@unittest.skipUnless(
    (ROOT / "lib" / "rt64" / ".git").exists(), "lib/rt64 submodule not initialized"
)
class PatchCompositionTests(unittest.TestCase):
    def replay(self, platform):
        """Apply a platform's full rt64 order in a throwaway worktree."""
        sha = subprocess.check_output(
            ["git", "rev-parse", f"HEAD:lib/rt64"], cwd=ROOT, text=True
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
        for number in CMAKE_SERIES:
            self.apply(tree, resolve_patch(number), [], platform)

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
