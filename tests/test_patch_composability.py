"""Patch-composition regression: every platform's series must apply and repeat.

Background: the v0.8.0 release builds broke on two platforms at once while
the local Windows build stayed green. Linux failed to compile because patch
0024 referenced a Windows-only DXIL shader blob without a platform guard, and
Android failed to configure because patch 0013 (Android-only) inserts a blank
line that patch 0030's DXC hunk requires to be absent. Both slipped through
because each platform applies a different subset of patches/series.txt.

The tests below run the production applier, tools/apply_patches.py, against
disposable worktrees of the pinned submodules: every platform's series must
apply, a second run must recognize it, and a checkout part-way through the
series must be completed without losing unrelated local files.
"""
import shutil
import subprocess
import tempfile
import unittest
from pathlib import Path

from tools.apply_patches import PLATFORMS, PatchError, apply_series, series_for

ROOT = Path(__file__).resolve().parents[1]
PATCHES_DIR = ROOT / "patches"
RT64_DIR = ROOT / "lib" / "rt64"
PLUME = "lib/rt64/src/contrib/plume"
SUBMODULES = ("lib/N64ModernRuntime", "lib/rt64", PLUME, "lib/RecompFrontend")


def git(*args, cwd=ROOT):
    return subprocess.check_output(["git", *args], cwd=cwd, stderr=subprocess.STDOUT)


def resolve_patch(number):
    matches = sorted(PATCHES_DIR.glob(f"{number}-*.patch"))
    assert len(matches) == 1, f"expected one patch for {number}: {matches}"
    return matches[0]


def checkout(test, directory):
    """Return a detached worktree of a submodule at its pinned commit."""
    source = ROOT / directory
    parent = Path(git("rev-parse", "--show-superproject-working-tree", cwd=source).decode().strip())
    relative = source.resolve().relative_to(parent.resolve()).as_posix()
    sha = git("rev-parse", f"HEAD:{relative}", cwd=parent).decode().strip()
    temporary = tempfile.TemporaryDirectory(prefix="lambo-patch-compose-")
    test.addCleanup(temporary.cleanup)
    tree = Path(temporary.name) / Path(directory).name
    git("-C", str(source), "worktree", "add", "--detach", str(tree), sha)
    test.addCleanup(subprocess.run, ["git", "-C", str(source), "worktree", "remove", "--force", str(tree)],
                    capture_output=True)
    return tree


def reset(tree):
    git("-C", str(tree), "reset", "-q", "--hard", "HEAD")
    git("-C", str(tree), "clean", "-fdqx")


def snapshot(tree):
    """Changed and untracked files plus the raw index, to prove they are kept."""
    changed = git("-C", str(tree), "diff", "--name-only", "-z", "HEAD")
    untracked = git("-C", str(tree), "ls-files", "--others", "--exclude-standard", "-z")
    files = {name: (tree / name.decode()).read_bytes()
             for name in (changed + untracked).split(b"\0") if name}
    index = Path(git("-C", str(tree), "rev-parse", "--git-path", "index").decode().strip())
    if not index.is_absolute():
        index = tree / index
    return files, index.read_bytes()


@unittest.skipUnless(all((ROOT / directory / ".git").exists() for directory in SUBMODULES),
                     "dependency submodules not initialized")
class SeriesReplayTests(unittest.TestCase):
    def test_every_platform_series_applies_then_repeats(self):
        for directory in SUBMODULES:
            tree = checkout(self, directory)
            for platform in PLATFORMS:
                patches = series_for(platform)[directory]
                with self.subTest(directory=directory, platform=platform):
                    reset(tree)
                    self.assertEqual(apply_series(tree, patches), len(patches))
                    self.assertEqual(apply_series(tree, patches), 0)

    def test_tree_patched_for_another_platform_is_rejected(self):
        tree = checkout(self, "lib/rt64")
        apply_series(tree, series_for("windows")["lib/rt64"])
        before = snapshot(tree)
        with self.assertRaisesRegex(PatchError, "another platform"):
            apply_series(tree, series_for("android")["lib/rt64"])
        self.assertEqual(snapshot(tree), before)

    def test_partial_series_is_completed_and_local_files_kept(self):
        # Issue 351 reported a checkout stopped before 0042; every boundary
        # between independently added groups must be completed the same way.
        tree = checkout(self, "lib/rt64")
        patches = series_for("android")["lib/rt64"]
        names = [patch.name[:4] for patch in patches]
        for applied in sorted({0, names.index("0021"), names.index("0042"), len(patches) - 1}):
            with self.subTest(applied=names[applied - 1] if applied else "none"):
                reset(tree)
                apply_series(tree, patches[:applied])
                personal = tree / "personal.txt"
                personal.write_bytes(b"keep my staged file\n")
                git("-C", str(tree), "add", "personal.txt", "src/hle/rt64_game_frame.cpp")
                personal.write_bytes(b"keep my unstaged edit\r\n")
                (tree / "notes.txt").write_bytes(b"keep my untracked notes\r\n")
                files, index = snapshot(tree)
                self.assertEqual(apply_series(tree, patches), len(patches) - applied)
                after_files, after_index = snapshot(tree)
                self.assertEqual(after_index, index)
                for name in (b"personal.txt", b"notes.txt"):
                    self.assertEqual(after_files[name], files[name])
                self.assertEqual(apply_series(tree, patches), 0)

    def test_drift_is_rejected_despite_later_patch(self):
        tree = checkout(self, "lib/rt64")
        patches = series_for("android")["lib/rt64"]
        apply_series(tree, patches)
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
                git("-C", str(tree), "add", relative)
                # Checking only the newest patch would accept this incomplete series.
                git("-C", str(tree), "apply", "--reverse", "--check", str(patches[-1]))
                before = snapshot(tree)
                with self.assertRaisesRegex(PatchError, "does not match any prefix"):
                    apply_series(tree, patches)
                self.assertEqual(snapshot(tree), before)
                path.write_bytes(original)
                git("-C", str(tree), "add", relative)


@unittest.skipUnless((ROOT / PLUME / ".git").exists(), "plume submodule not initialized")
class PlumeSeriesTests(unittest.TestCase):
    def setUp(self):
        self.tree = checkout(self, PLUME)
        self.patches = series_for("android")[PLUME]

    def test_recording_fixture_matches_applied_series(self):
        apply_series(self.tree, self.patches)
        from tests.patched_source import patched_file
        recording = self.tree.parent / "recording"
        for name in ("plume_vulkan.cpp", "plume_vulkan.h"):
            patched_file(ROOT / PLUME, name, recording)
            self.assertEqual((recording / name).read_bytes(), (self.tree / name).read_bytes(),
                             f"recording fixture differs from the applied series: {name}")

    def test_upgrade_from_earlier_mapping_patches_preserves_edits(self):
        names = [patch.name[:4] for patch in self.patches]
        for last in ("0050", "0052"):
            with self.subTest(previous_state=last):
                reset(self.tree)
                self.check_upgrade(names.index(last) + 1)

    def check_upgrade(self, applied):
        apply_series(self.tree, self.patches[:applied])
        cpp = self.tree / "plume_vulkan.cpp"
        header = self.tree / "plume_vulkan.h"
        cpp.write_bytes(cpp.read_bytes() + b"\n// Keep this unrelated developer edit.\n")
        header.write_bytes(header.read_bytes() + b"\n// Keep this unrelated header edit.\n")
        note = self.tree / "personal.txt"
        note.write_bytes(b"staged note\n")
        git("-C", str(self.tree), "add", "personal.txt")
        note.write_bytes(b"unstaged note\r\n")
        (self.tree / "untracked.txt").write_bytes(b"keep untracked note\r\n")
        index = Path(git("-C", str(self.tree), "rev-parse", "--git-path", "index").decode().strip())
        kept = {path: path.read_bytes() for path in (note, self.tree / "untracked.txt", index)}

        expected = self.tree.parent / "expected"
        shutil.rmtree(expected, ignore_errors=True)
        expected.mkdir()
        (expected / "plume_vulkan.cpp").write_bytes(cpp.read_bytes())
        (expected / "plume_vulkan.h").write_bytes(header.read_bytes())
        for patch in self.patches[applied:]:
            subprocess.run(["git", "apply", "--include=plume_vulkan.cpp", "--include=plume_vulkan.h",
                            str(patch)], cwd=expected, check=True, capture_output=True)

        self.assertEqual(apply_series(self.tree, self.patches), len(self.patches) - applied)
        self.assertEqual({path: path.read_bytes() for path in kept}, kept)
        self.assertEqual(cpp.read_bytes(), (expected / "plume_vulkan.cpp").read_bytes())
        self.assertEqual(header.read_bytes(), (expected / "plume_vulkan.h").read_bytes())
        comment = b"Invalidating read maps cannot nest; finish and flush active mappings first."
        self.assertEqual(header.read_bytes().count(comment), 1)
        self.assertEqual(apply_series(self.tree, self.patches), 0)


class DxilGuardTests(unittest.TestCase):
    def test_dxil_blob_use_arrives_with_platform_guard(self):
        # DXIL shader blobs are only generated (and declared) on Windows:
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
